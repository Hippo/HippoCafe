package rip.hippo.hippocafe.disassembler.context

/**
 * @author Hippo
 * @version 1.0.0, 2/20/21
 * @since 1.0.0
 */
object AssemblerFlag {
  val CALCULATE_MAXES: AssemblerFlag = AssemblerFlag("CALCULATE_MAXES")
  val GENERATE_FRAMES: AssemblerFlag = AssemblerFlag("GENERATE_FRAMES (not supported)")
  val APPEND_CONSTANT_POOL: AssemblerFlag = AssemblerFlag("APPEND CONSTANT POOL")
}

sealed case class AssemblerFlag(name: String)
