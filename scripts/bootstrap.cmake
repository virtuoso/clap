function(bootstrap_deps bootstrap_file)
    if(WIN32)
            set(PYTHON_EXECUTABLE "python")
    else()
            set(PYTHON_EXECUTABLE "python3")
    endif()

    if((NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/.${bootstrap_file}") OR ("${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" IS_NEWER_THAN "${CMAKE_CURRENT_SOURCE_DIR}/deps/.${bootstrap_file}"))
            execute_process(
                    COMMAND ${PYTHON_EXECUTABLE} "bootstrap.py" --bootstrap-file "${CMAKE_CURRENT_SOURCE_DIR}/deps/${bootstrap_file}"
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    RESULT_VARIABLE bootstrap_result
            )
            if (NOT (${bootstrap_result} EQUAL 0))
                    message(FATAL_ERROR "bootstrap.py failed")
            endif()
    endif()
endfunction ()