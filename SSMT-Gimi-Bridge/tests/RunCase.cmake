execute_process(COMMAND "${FIXTURE}" "${RUNTIME}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 25)
message("${output}${errors}")
if(NOT "${result}" STREQUAL "0" OR NOT output MATCHES "Runtime/native mixed device and context calls completed")
    message(FATAL_ERROR "Bridge 混合设备测试未完成：${result}")
endif()
