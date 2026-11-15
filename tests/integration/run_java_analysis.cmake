file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(source_mode --java-home)
set(source_path "${JAVA_HOME}")
if(NOT EXISTS "${JAVA_HOME}/jmods/java.base.jmod" AND
   NOT EXISTS "${JAVA_HOME}/jre/lib/rt.jar" AND NOT EXISTS "${JAVA_HOME}/lib/rt.jar")
  # Some JDK packages omit JMODs. Extract a directory fixture from the
  # installed test JVM only. The library never opens a linked runtime image.
  set(source_mode --directory)
  set(source_path "${OUTPUT_DIR}/runtime/java.base")
  execute_process(COMMAND "${JIMAGE}" extract "--dir=${OUTPUT_DIR}/runtime" "--include=glob:**/java.base/**.class"
    "${JAVA_HOME}/lib/modules" COMMAND_ERROR_IS_FATAL ANY)
endif()

file(COPY "${FIXTURE_DIR}/" DESTINATION "${OUTPUT_DIR}")
execute_process(COMMAND "${REWRITE}" "${FIXTURE_DIR}" "${OUTPUT_DIR}" "${source_mode}" "${source_path}"
  COMMAND_ERROR_IS_FATAL ANY)
foreach(class IN ITEMS VerifyGenerated VerifyAnalysis)
  execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${OUTPUT_DIR}" "${class}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
