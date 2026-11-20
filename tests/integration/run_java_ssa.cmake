set(original "${OUTPUT_DIR}/original")
set(rewritten "${OUTPUT_DIR}/rewritten")
file(MAKE_DIRECTORY "${original}" "${rewritten}")
execute_process(COMMAND "${JAVAC}" --release 17 -g -d "${original}"
  "${CMAKE_CURRENT_LIST_DIR}/SsaFixture.java" "${CMAKE_CURRENT_LIST_DIR}/VerifySsa.java"
  COMMAND_ERROR_IS_FATAL ANY)
file(COPY "${original}/VerifySsa.class" "${original}/SsaFixture$Mark.class" DESTINATION "${rewritten}")
file(GLOB analysis_classes "${ANALYSIS_DIR}/AnalysisFixture*.class" "${ANALYSIS_DIR}/VerifyAnalysis.class")
file(COPY ${analysis_classes} DESTINATION "${original}")
file(COPY ${analysis_classes} DESTINATION "${rewritten}")

set(mode --java-home)
set(runtime "${JAVA_HOME}")
if(NOT EXISTS "${JAVA_HOME}/jmods/java.base.jmod" AND
   NOT EXISTS "${JAVA_HOME}/jre/lib/rt.jar" AND NOT EXISTS "${JAVA_HOME}/lib/rt.jar")
  set(mode --directory)
  set(runtime "${OUTPUT_DIR}/runtime/java.base")
  execute_process(COMMAND "${JIMAGE}" extract "--dir=${OUTPUT_DIR}/runtime" "--include=glob:**/java.base/**.class"
    "${JAVA_HOME}/lib/modules" COMMAND_ERROR_IS_FATAL ANY)
endif()
execute_process(COMMAND "${REWRITE}" "${original}" "${rewritten}" "${mode}" "${runtime}" COMMAND_ERROR_IS_FATAL ANY)
foreach(directory IN ITEMS original rewritten)
  execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${${directory}}" VerifySsa "${${directory}}/trace.txt"
    COMMAND_ERROR_IS_FATAL ANY)
  file(READ "${${directory}}/trace.txt" ${directory}_trace)
  execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${${directory}}" VerifyAnalysis COMMAND_ERROR_IS_FATAL ANY)
endforeach()
if(NOT original_trace STREQUAL rewritten_trace)
  message(FATAL_ERROR "SSA round trip changed the execution trace.\nOriginal:\n${original_trace}\nRewritten:\n${rewritten_trace}")
endif()
execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${rewritten}" VerifySsa edited COMMAND_ERROR_IS_FATAL ANY)
