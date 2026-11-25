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

set(deps "${OUTPUT_DIR}/dependencies")
file(MAKE_DIRECTORY "${deps}/classes" "${deps}/input" "${deps}/output")
file(WRITE "${deps}/module-info.java" "module sample.dependency { exports api; }\n")
file(WRITE "${deps}/Dependency.java" [=[
package api;
public class Dependency {
    public String message() { return "dependency resolved"; }
}
]=])
file(WRITE "${deps}/UsesDependency.java" [=[
public class UsesDependency extends api.Dependency {
    public static void main(String[] args) {
        System.out.println(new UsesDependency().message());
    }
}
]=])
execute_process(COMMAND "${JAVAC}" --release 17 -d "${deps}/classes"
  "${deps}/module-info.java" "${deps}/Dependency.java" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${JAVAC}" --release 17 -cp "${deps}/classes" -d "${deps}/input"
  "${deps}/UsesDependency.java" COMMAND_ERROR_IS_FATAL ANY)
set(sources "${deps}/classes")
if(JAR)
  execute_process(COMMAND "${JAR}" --create --file "${deps}/dep.jar" -C "${deps}/classes" api
    COMMAND_ERROR_IS_FATAL ANY)
  list(APPEND sources "${deps}/dep.jar")
endif()
if(JMOD)
  file(REMOVE "${deps}/dep.jmod")
  execute_process(COMMAND "${JMOD}" create --class-path "${deps}/classes" "${deps}/dep.jmod"
    COMMAND_ERROR_IS_FATAL ANY)
  list(APPEND sources "${deps}/dep.jmod")
endif()

set(input "${deps}/input/UsesDependency.class")
set(output "${deps}/output/UsesDependency.class")
execute_process(COMMAND "${EDIT}" "${input}" "${output}" "${runtime}" "${JAVA_RELEASE}"
  RESULT_VARIABLE status ERROR_VARIABLE error)
if(status EQUAL 0 OR NOT error MATCHES "api/Dependency")
  message(FATAL_ERROR "Expected a resolution error for the missing dependency: ${error}")
endif()
foreach(source IN LISTS sources)
  execute_process(COMMAND "${EDIT}" "${input}" "${output}" "${runtime}" "${JAVA_RELEASE}" "${source}"
    COMMAND_ERROR_IS_FATAL ANY)
endforeach()
execute_process(COMMAND "${JAVA}" -Xverify:all --module-path "${deps}/classes" --add-modules sample.dependency
  -cp "${deps}/output" UsesDependency
  OUTPUT_VARIABLE message OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT message STREQUAL "dependency resolved")
  message(FATAL_ERROR "Unexpected output after writing with application dependencies: ${message}")
endif()
