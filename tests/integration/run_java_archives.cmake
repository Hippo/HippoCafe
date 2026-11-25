function(run)
  execute_process(COMMAND ${ARGV}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
  )
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "${ARGV} failed (${result})\n${stdout}\n${stderr}")
  endif()
  set(command_output "${stdout}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}/resources")
file(WRITE "${OUTPUT_DIR}/resources/obsolete.txt" "remove this")

# JDK 17's jar --release validator cannot read the sealed classes in this fixture.
file(COPY "${FIXTURES}/cafe" DESTINATION "${OUTPUT_DIR}/versioned/META-INF/versions/17")
file(WRITE "${OUTPUT_DIR}/multi-release.mf" "Manifest-Version: 1.0\nMulti-Release: true\n\n")
run("${JAR}" --create --file "${OUTPUT_DIR}/input.jar" --manifest "${OUTPUT_DIR}/multi-release.mf"
  -C "${FIXTURES}" . -C "${OUTPUT_DIR}/resources" obsolete.txt
  -C "${OUTPUT_DIR}/versioned" META-INF/versions/17)
run("${JMOD}" create --class-path "${FIXTURES}" --config "${OUTPUT_DIR}/resources" "${OUTPUT_DIR}/input.jmod")
run("${REWRITE}" "${FIXTURES}" "${OUTPUT_DIR}")
run("${JAVA}" "${CMAKE_CURRENT_LIST_DIR}/VerifyArchives.java" "${OUTPUT_DIR}")

foreach(kind IN ITEMS created rewritten)
  run("${JAR}" --list --file "${OUTPUT_DIR}/${kind}.jar")
  if(NOT command_output MATCHES "cafe/fixtures/Features\\.class" OR NOT command_output MATCHES "added\\.txt")
    message(FATAL_ERROR "Unexpected JAR listing\n${command_output}")
  endif()
  run("${JMOD}" list "${OUTPUT_DIR}/${kind}.jmod")
  if(NOT command_output MATCHES "classes/module-info\\.class" OR NOT command_output MATCHES "conf/added\\.txt")
    message(FATAL_ERROR "Unexpected JMOD listing\n${command_output}")
  endif()
  run("${JMOD}" describe "${OUTPUT_DIR}/${kind}.jmod")
  if(NOT command_output MATCHES "cafe\\.fixtures")
    message(FATAL_ERROR "Unexpected JMOD descriptor\n${command_output}")
  endif()
  run("${JMOD}" extract --dir "${OUTPUT_DIR}/${kind}" "${OUTPUT_DIR}/${kind}.jmod")
  file(READ "${OUTPUT_DIR}/${kind}/conf/added.txt" config)
  if(NOT config STREQUAL "jmod configuration" OR EXISTS "${OUTPUT_DIR}/${kind}/conf/obsolete.txt")
    message(FATAL_ERROR "Unexpected extracted JMOD configuration")
  endif()
endforeach()
