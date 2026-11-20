set(original "${OUTPUT_DIR}/original")
file(MAKE_DIRECTORY "${original}")
if(MODE STREQUAL "debug")
  set(debug_flag -g)
else()
  set(debug_flag -g:none)
endif()
execute_process(COMMAND "${JAVAC}" --release 17 ${debug_flag} -d "${original}"
  "${CMAKE_CURRENT_LIST_DIR}/RepeatedSsaLoop.java" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${original}" VerifyRepeatedSsaLoop original
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${REWRITE}" "${OUTPUT_DIR}" "${MODE}" COMMAND_ERROR_IS_FATAL ANY)
foreach(round RANGE 1 5)
  set(dir "${OUTPUT_DIR}/${round}")
  file(COPY "${original}/VerifyRepeatedSsaLoop.class" DESTINATION "${dir}")
  execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${dir}" VerifyRepeatedSsaLoop edited
    COMMAND_ERROR_IS_FATAL ANY)
endforeach()
if(MODE STREQUAL "debug")
  set(dir "${OUTPUT_DIR}/probe")
  file(COPY "${original}/VerifyRepeatedSsaLoop.class" DESTINATION "${dir}")
  execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${dir}" VerifyRepeatedSsaLoop probe
    COMMAND_ERROR_IS_FATAL ANY)
endif()
