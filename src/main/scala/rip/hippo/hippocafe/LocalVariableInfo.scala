package rip.hippo.hippocafe

import rip.hippo.hippocafe.disassembler.instruction.impl.LabelInstruction


/**
 * @author Hippo
 * @version 1.0.0, 3/13/21
 * @since 1.1.0
 */
final case class LocalVariableInfo(var name: String,
                                   var descriptor: String,
                                   var signature: Option[String],
                                   var start: LabelInstruction,
                                   var end: LabelInstruction,
                                   var index: Int)