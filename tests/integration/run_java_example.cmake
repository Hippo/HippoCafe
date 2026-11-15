file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(runtime "${JAVA_HOME}")
if(NOT EXISTS "${JAVA_HOME}/jmods/java.base.jmod" AND
   NOT EXISTS "${JAVA_HOME}/jre/lib/rt.jar" AND NOT EXISTS "${JAVA_HOME}/lib/rt.jar")
  # Prepare a supported runtime archive from this JVM's own classes. This is a
  # test fixture; the library and examples do not read linked runtime images.
  set(runtime "${OUTPUT_DIR}/runtime-home")
  file(MAKE_DIRECTORY "${runtime}/jmods")
  execute_process(COMMAND "${JIMAGE}" extract "--dir=${OUTPUT_DIR}/runtime"
    "--include=glob:**/java.base/**.class" "${JAVA_HOME}/lib/modules"
    COMMAND_ERROR_IS_FATAL ANY)
  file(REMOVE "${runtime}/jmods/java.base.jmod")
  execute_process(COMMAND "${JMOD}" create "--class-path=${OUTPUT_DIR}/runtime/java.base"
    "${runtime}/jmods/java.base.jmod" COMMAND_ERROR_IS_FATAL ANY)
endif()

set(output "${OUTPUT_DIR}/HelloWorld.class")
execute_process(COMMAND "${EDIT}" "${INPUT}" "${output}" "${runtime}" "${JAVA_RELEASE}"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${OUTPUT_DIR}" HelloWorld
  OUTPUT_VARIABLE hello OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT hello STREQUAL "Hello World")
  message(FATAL_ERROR "Unexpected output after running the documented edits: ${hello}")
endif()

file(SHA256 "${output}" original_hash)
foreach(release IN ITEMS "0" "17trailing" "65536")
  execute_process(COMMAND "${EDIT}" "${INPUT}" "${output}" "${runtime}" "${release}"
    RESULT_VARIABLE status ERROR_VARIABLE error)
  if(status EQUAL 0 OR NOT error MATCHES "target-release")
    message(FATAL_ERROR "Invalid target release was not rejected with a diagnostic: ${release}")
  endif()
endforeach()
execute_process(COMMAND "${EDIT}" "${OUTPUT_DIR}/missing.class" "${output}" "${runtime}" "${JAVA_RELEASE}"
  RESULT_VARIABLE status ERROR_VARIABLE error)
if(status EQUAL 0 OR NOT error MATCHES "could not open input")
  message(FATAL_ERROR "Missing example input was not reported")
endif()
execute_process(COMMAND "${EDIT}" "${INPUT}" "${OUTPUT_DIR}/missing/output.class" "${runtime}" "${JAVA_RELEASE}"
  RESULT_VARIABLE status ERROR_VARIABLE error)
if(status EQUAL 0 OR NOT error MATCHES "could not write output")
  message(FATAL_ERROR "Failed example output was not reported")
endif()
file(SHA256 "${output}" final_hash)
if(NOT original_hash STREQUAL final_hash)
  message(FATAL_ERROR "A failed invocation changed the previously written class")
endif()
