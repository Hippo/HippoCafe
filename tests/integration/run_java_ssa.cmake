set(original "${OUTPUT_DIR}/original")
set(rewritten "${OUTPUT_DIR}/rewritten")
file(MAKE_DIRECTORY "${original}" "${rewritten}")
# Exercise both protected call orders, with each catch serving as a loop exit or a backedge.
set(generated_fixtures)
set(generated_sources)
foreach(first_site IN ITEMS 1 2)
  math(EXPR second_site "3 - ${first_site}")
  foreach(arithmetic_exit IN ITEMS break continue)
    if(arithmetic_exit STREQUAL "break")
      set(runtime_exit continue)
    else()
      set(runtime_exit break)
    endif()
    set(fixture "SsaProtectedLoop${first_site}_${arithmetic_exit}")
    set(source "${OUTPUT_DIR}/${fixture}.java")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/SsaProtectedLoop.java.in" "${source}" @ONLY)
    list(APPEND generated_fixtures "${fixture}")
    list(APPEND generated_sources "${source}")
  endforeach()
endforeach()
execute_process(COMMAND "${JAVAC}" --release 17 -g -d "${original}"
  "${CMAKE_CURRENT_LIST_DIR}/SsaFixture.java" "${CMAKE_CURRENT_LIST_DIR}/VerifySsa.java"
  ${generated_sources}
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
execute_process(COMMAND "${REWRITE}" "${original}" "${rewritten}" "${mode}" "${runtime}" ${generated_fixtures}
  COMMAND_ERROR_IS_FATAL ANY)
foreach(directory IN ITEMS original rewritten)
  execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${${directory}}" VerifySsa "${${directory}}/trace.txt"
    ${generated_fixtures}
    COMMAND_ERROR_IS_FATAL ANY)
  file(READ "${${directory}}/trace.txt" ${directory}_trace)
  execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${${directory}}" VerifyAnalysis COMMAND_ERROR_IS_FATAL ANY)
endforeach()
if(NOT original_trace STREQUAL rewritten_trace)
  message(FATAL_ERROR "SSA round trip changed the execution trace.\nOriginal:\n${original_trace}\nRewritten:\n${rewritten_trace}")
endif()
execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${rewritten}" VerifySsa edited COMMAND_ERROR_IS_FATAL ANY)
