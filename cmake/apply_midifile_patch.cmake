foreach(required_variable GIT_EXECUTABLE MIDIFILE_SOURCE_DIR PATCH_FILE)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required to patch midifile")
    endif()
endforeach()

if(NOT EXISTS "${MIDIFILE_SOURCE_DIR}/src/MidiFile.cpp" OR NOT EXISTS "${PATCH_FILE}")
    message(FATAL_ERROR "midifile source or running-status patch is missing")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${MIDIFILE_SOURCE_DIR}"
    RESULT_VARIABLE apply_check_result
    OUTPUT_VARIABLE apply_check_output
    ERROR_VARIABLE apply_check_error
)
if(apply_check_result EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero "${PATCH_FILE}"
        WORKING_DIRECTORY "${MIDIFILE_SOURCE_DIR}"
        RESULT_VARIABLE apply_result
        OUTPUT_VARIABLE apply_output
        ERROR_VARIABLE apply_error
    )
    if(NOT apply_result EQUAL 0)
        message(FATAL_ERROR
            "midifile running-status patch passed its preflight but failed to apply:\n"
            "${apply_output}${apply_error}")
    endif()
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${MIDIFILE_SOURCE_DIR}"
    RESULT_VARIABLE reverse_check_result
    OUTPUT_VARIABLE reverse_check_output
    ERROR_VARIABLE reverse_check_error
)
if(reverse_check_result EQUAL 0)
    message(STATUS "midifile running-status patch is already applied")
    return()
endif()

message(FATAL_ERROR
    "midifile running-status patch cannot be applied and the source is not already patched.\n"
    "Apply check:\n${apply_check_output}${apply_check_error}\n"
    "Reverse check:\n${reverse_check_output}${reverse_check_error}")
