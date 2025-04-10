function(bootstrap_deps bootstrap_file)
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(PYTHON_EXECUTABLE "python")
    else()
        set(PYTHON_EXECUTABLE "python3")
    endif()

    if((NOT EXISTS "${CMAKE_SOURCE_DIR}/deps/.${bootstrap_file}") OR ("${CMAKE_SOURCE_DIR}/CMakeLists.txt" IS_NEWER_THAN "${CMAKE_SOURCE_DIR}/deps/.${bootstrap_file}"))
        execute_process(
            COMMAND ${PYTHON_EXECUTABLE} "bootstrap.py" --bootstrap-file "${CMAKE_SOURCE_DIR}/deps/${bootstrap_file}"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE bootstrap_result
        )
        if (NOT (${bootstrap_result} EQUAL 0))
            message(FATAL_ERROR "bootstrap.py failed: ${bootstrap_result}")
        endif()
    endif()
endfunction ()
