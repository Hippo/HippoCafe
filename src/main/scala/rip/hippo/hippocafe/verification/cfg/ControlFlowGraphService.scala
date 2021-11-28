package rip.hippo.hippocafe.verification.cfg

import rip.hippo.hippocafe.verification.block.{Block, BlockService}
import rip.hippo.hippocafe.verification.cfg.BlockNode.count
import rip.hippo.hippocafe.{ClassFile, MethodInfo}

import scala.collection.mutable

/**
 * @author Hippo
 * @version 1.0.0, 10/3/21
 * @since 2.0.0
 */
final case class ControlFlowGraphService(classFile: ClassFile, methodInfo: MethodInfo) {
  private val blockService = BlockService(classFile, methodInfo)
  val blockNodes: mutable.ListBuffer[BlockNode] = blockService.blocks.map(block => new BlockNode(block))
  val head: BlockNode = blockNodes.head


  if (blockNodes.size > 1) {

    def linkJumpBlocks(blockNode: BlockNode): Unit = {
      val block = blockNode.block
      block.getEndingBranch match {
        case Some(value) =>
          blockNodes.filter(_.block.getStartingLabel.exists(value.getLabels.contains(_))).foreach(sub => {
            blockNode.nextBlocks += sub
            sub.previousBlocks += blockNode
          })
        case None =>
      }
      if (block.isTcbHandler) {
        val tcb = block.getAssociatedTcb.get
        blockNodes.filter(_.block.getStartingLabel.contains(tcb.start)).foreach(node => {
          blockNode.previousBlocks ++= node.previousBlocks
        })
      }
    }

    val iterator = blockNodes.iterator
    val parallel = blockNodes.iterator
    if (parallel.hasNext) parallel.next()

    var last = Option.empty[BlockNode]
    while (iterator.hasNext) {
      val first = iterator.next()
      if (parallel.hasNext) {
        val second = parallel.next()
        last = Option(second)

        if (!first.block.isGoto) {
          first.nextBlocks += second
          second.previousBlocks += first
        }

        linkJumpBlocks(second)
      } else {
        last match {
          case Some(value) if !value.block.isGoto =>
            value.nextBlocks += first
            first.previousBlocks += value
          case _ =>
        }
      }

      linkJumpBlocks(first)
    }

    blockNodes.foreach(node => {
      node.previousBlocks.remove(node)
      node.nextBlocks.remove(node)
    })
  }
}

final class BlockNode(val block: Block) {
  val nextBlocks: mutable.LinkedHashSet[BlockNode] = mutable.LinkedHashSet[BlockNode]()
  val previousBlocks: mutable.LinkedHashSet[BlockNode] = mutable.LinkedHashSet[BlockNode]()
  val id: Int = count
  count += 1
}

object BlockNode {
  var count = 0
}
