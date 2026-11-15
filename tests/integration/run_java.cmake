file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(roundtrip source class_name)
  execute_process(COMMAND "${ROUNDTRIP}" "${FIXTURE_DIR}/${source}" "${OUTPUT_DIR}/${class_name}.class"
    COMMAND_ERROR_IS_FATAL ANY
  )
endfunction()

roundtrip(supplied/classes/HelloWorld.class HelloWorld)
roundtrip(generated/LegacySubroutine.class LegacySubroutine)
roundtrip(generated/DynamicConstant.class DynamicConstant)

execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${OUTPUT_DIR}" HelloWorld
  OUTPUT_VARIABLE hello OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY
)
if(NOT hello STREQUAL "Hello World")
  message(FATAL_ERROR "Unexpected HelloWorld output after round trip: ${hello}")
endif()
execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${OUTPUT_DIR}" LegacySubroutine
  COMMAND_ERROR_IS_FATAL ANY
)
execute_process(COMMAND "${JAVA}" -Xverify:all -cp "${OUTPUT_DIR}" "${CMAKE_CURRENT_LIST_DIR}/VerifyDynamic.java"
  COMMAND_ERROR_IS_FATAL ANY
)
