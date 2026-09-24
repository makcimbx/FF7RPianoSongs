if(NOT FF7RP_SOURCE_DIR OR NOT FF7RP_TEST_BINARY_DIR OR NOT FF7RP_TEST_BUILD_ID)
    message(FATAL_ERROR "Missing offline configuration test inputs")
endif()

set(_dependencies
    "-DFETCHCONTENT_SOURCE_DIR_MINIAUDIO=${FF7RP_MINIAUDIO_SOURCE_DIR}"
    "-DFETCHCONTENT_SOURCE_DIR_MIDIFILE=${FF7RP_MIDIFILE_SOURCE_DIR}"
    "-DCMAKE_DISABLE_FIND_PACKAGE_Python3=ON"
    "-DFF7RP_PWSH_EXECUTABLE=FF7RP_PWSH_EXECUTABLE-NOTFOUND"
    "-DCMAKE_ASM_MASM_COMPILER=CMAKE_ASM_MASM_COMPILER-NOTFOUND")

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${FF7RP_SOURCE_DIR}"
    -B "${FF7RP_TEST_BINARY_DIR}/known" -DFF7RP_BUILD_RUNTIME=OFF
    "-DFF7RP_GAME_BUILD=${FF7RP_TEST_BUILD_ID}" ${_dependencies}
    RESULT_VARIABLE _known_result OUTPUT_VARIABLE _known_out ERROR_VARIABLE _known_err)
if(NOT _known_result EQUAL 0)
    message(FATAL_ERROR "Known offline target failed configuration: ${_known_out}\n${_known_err}")
endif()

file(READ "${FF7RP_TEST_BINARY_DIR}/known/CMakeFiles/TargetDirectories.txt" _targets)
if(NOT _targets MATCHES "/song_cache_tool\\.dir" OR
    _targets MATCHES "/(FF7RPianoSongs|check_rva_catalog|release_audit_tool|minhook)\\.dir")
    message(FATAL_ERROR "Offline target graph contains runtime dependencies or omits tools: ${_targets}")
endif()
if(EXISTS "${FF7RP_TEST_BINARY_DIR}/known/generated/release_identity.generated.h" OR
    EXISTS "${FF7RP_TEST_BINARY_DIR}/known/generated/build_identity.generated.h")
    message(FATAL_ERROR "Offline configuration generated runtime/release identity headers")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${FF7RP_SOURCE_DIR}"
    -B "${FF7RP_TEST_BINARY_DIR}/unknown" -DFF7RP_BUILD_RUNTIME=OFF
    -DFF7RP_GAME_BUILD=ff7rp-unknown-target ${_dependencies}
    RESULT_VARIABLE _unknown_result OUTPUT_VARIABLE _unknown_out ERROR_VARIABLE _unknown_err)
if(_unknown_result EQUAL 0 OR NOT "${_unknown_out}${_unknown_err}" MATCHES "is not declared in")
    message(FATAL_ERROR "Unknown offline target did not fail closed: ${_unknown_out}\n${_unknown_err}")
endif()
