execute_process(COMMAND "${PROGRAM}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 10)

if(result STREQUAL "0" OR NOT error MATCHES "RealtimeSanitizer: unsafe-library-call"
        OR NOT error MATCHES "unsafe function `malloc`")
    message(FATAL_ERROR "Expected an RTSan malloc violation; result=${result}\n${output}${error}")
endif()
