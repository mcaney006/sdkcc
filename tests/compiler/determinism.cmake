if(NOT DEFINED SDKCC OR NOT DEFINED SPEC OR NOT DEFINED WORK)
  message(FATAL_ERROR "SDKCC, SPEC, and WORK are required")
endif()
file(REMOVE_RECURSE "${WORK}")
set(first "${WORK}/first")
set(second "${WORK}/second")
execute_process(
  COMMAND "${SDKCC}" compile "${SPEC}" --output "${first}" --library minimal
          --namespace minimal --lang c,cpp --emit-ir
  RESULT_VARIABLE first_result OUTPUT_VARIABLE first_stdout
  ERROR_VARIABLE first_stderr
)
if(NOT first_result EQUAL 0)
  message(FATAL_ERROR "first generation failed:\n${first_stdout}\n${first_stderr}")
endif()
execute_process(
  COMMAND "${SDKCC}" compile "${SPEC}" --output "${second}" --library minimal
          --namespace minimal --lang c,cpp --emit-ir
  RESULT_VARIABLE second_result OUTPUT_VARIABLE second_stdout
  ERROR_VARIABLE second_stderr
)
if(NOT second_result EQUAL 0)
  message(FATAL_ERROR "second generation failed:\n${second_stdout}\n${second_stderr}")
endif()
file(GLOB_RECURSE first_files RELATIVE "${first}" "${first}/*")
file(GLOB_RECURSE second_files RELATIVE "${second}" "${second}/*")
list(SORT first_files)
list(SORT second_files)
if(NOT first_files STREQUAL second_files)
  message(FATAL_ERROR "generated file manifests differ")
endif()
foreach(relative IN LISTS first_files)
  if(NOT IS_DIRECTORY "${first}/${relative}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E compare_files
              "${first}/${relative}" "${second}/${relative}"
      RESULT_VARIABLE compare_result
    )
    if(NOT compare_result EQUAL 0)
      message(FATAL_ERROR "generated file differs: ${relative}")
    endif()
  endif()
endforeach()

