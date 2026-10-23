cmake_minimum_required(VERSION 3.20)

project(FF7RPianoSongs LANGUAGES CXX)
option(FF7RP_BUILD_RUNTIME "Build the FF7 Rebirth ASI and runtime integration tests" ON)
if(FF7RP_BUILD_RUNTIME)
    enable_language(ASM_MASM)
endif()

set(FF7RP_FIRST_PARTY_CXX_FEATURE cxx_std_23)

set(FF7RP_RVA_CATALOG "${CMAKE_CURRENT_SOURCE_DIR}/src/game/rva_catalog.json")
set(FF7RP_RVA_GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_rva_catalog.py")
file(READ "${FF7RP_RVA_CATALOG}" FF7RP_RVA_CATALOG_TEXT)
string(JSON FF7RP_CATALOG_DEFAULT_BUILD ERROR_VARIABLE catalog_error
    GET "${FF7RP_RVA_CATALOG_TEXT}" "default_build")
if(catalog_error)
    message(FATAL_ERROR "rva_catalog.json field 'default_build' is missing or invalid: ${catalog_error}")
endif()
set(FF7RP_GAME_BUILD "${FF7RP_CATALOG_DEFAULT_BUILD}" CACHE STRING
    "FF7 Rebirth executable build this ASI is compiled against")
string(JSON FF7RP_CATALOG_BUILD_COUNT ERROR_VARIABLE catalog_error
    LENGTH "${FF7RP_RVA_CATALOG_TEXT}" "builds")
if(catalog_error)
    message(FATAL_ERROR "rva_catalog.json field 'builds' is missing or invalid: ${catalog_error}")
endif()
set(FF7RP_CATALOG_BUILD_IDS "")
set(FF7RP_GAME_BUILD_VERSION "")
if(FF7RP_CATALOG_BUILD_COUNT GREATER 0)
    math(EXPR FF7RP_CATALOG_BUILD_LAST "${FF7RP_CATALOG_BUILD_COUNT} - 1")
    foreach(index RANGE ${FF7RP_CATALOG_BUILD_LAST})
        string(JSON build_id GET "${FF7RP_RVA_CATALOG_TEXT}" "builds" ${index} "id")
        list(APPEND FF7RP_CATALOG_BUILD_IDS "${build_id}")
        if(build_id STREQUAL "${FF7RP_GAME_BUILD}")
            string(JSON FF7RP_GAME_BUILD_VERSION
                GET "${FF7RP_RVA_CATALOG_TEXT}" "builds" ${index} "game_version")
        endif()
    endforeach()
endif()
if(NOT FF7RP_GAME_BUILD IN_LIST FF7RP_CATALOG_BUILD_IDS)
    message(FATAL_ERROR
        "FF7RP_GAME_BUILD='${FF7RP_GAME_BUILD}' is not declared in src/game/rva_catalog.json; "
        "declared builds: ${FF7RP_CATALOG_BUILD_IDS}")
endif()
set_property(CACHE FF7RP_GAME_BUILD PROPERTY STRINGS ${FF7RP_CATALOG_BUILD_IDS})

if(FF7RP_BUILD_RUNTIME)
set(FF7RP_RELEASE_JSON "${CMAKE_CURRENT_SOURCE_DIR}/release.json")
set(FF7RP_RELEASE_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/release_identity.generated.h.in")
file(READ "${FF7RP_RELEASE_JSON}" FF7RP_RELEASE_JSON_TEXT)
foreach(field product version platform license)
    string(JSON FF7RP_RELEASE_${field} ERROR_VARIABLE release_error GET "${FF7RP_RELEASE_JSON_TEXT}" "${field}")
    if(release_error)
        message(FATAL_ERROR "release.json field '${field}' is missing or invalid: ${release_error}")
    endif()
endforeach()
set(FF7RP_RELEASE_PRODUCT "${FF7RP_RELEASE_product}")
set(FF7RP_RELEASE_VERSION "${FF7RP_RELEASE_version}")
set(FF7RP_RELEASE_PLATFORM "${FF7RP_RELEASE_platform}")
set(FF7RP_RELEASE_LICENSE "${FF7RP_RELEASE_license}")
if(NOT FF7RP_RELEASE_PRODUCT STREQUAL "FF7RPianoSongs" OR
   NOT FF7RP_RELEASE_PLATFORM STREQUAL "win64" OR NOT FF7RP_RELEASE_LICENSE STREQUAL "MIT" OR
   NOT FF7RP_RELEASE_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "release.json contains an unsupported release identity")
endif()

# One ASI ships per game build, so the release target is selected by the resolved catalog id.
string(JSON FF7RP_RELEASE_TARGET_COUNT ERROR_VARIABLE release_error
    LENGTH "${FF7RP_RELEASE_JSON_TEXT}" "targets")
if(release_error)
    message(FATAL_ERROR "release.json field 'targets' is missing or invalid: ${release_error}")
endif()
set(FF7RP_RELEASE_CATALOG "")
set(FF7RP_RELEASE_ARCHIVE_BASENAME "")
set(FF7RP_RELEASE_TARGET_CATALOG_IDS "")
if(FF7RP_RELEASE_TARGET_COUNT GREATER 0)
    math(EXPR FF7RP_RELEASE_TARGET_LAST "${FF7RP_RELEASE_TARGET_COUNT} - 1")
    foreach(index RANGE ${FF7RP_RELEASE_TARGET_LAST})
        string(JSON target_catalog_id
            GET "${FF7RP_RELEASE_JSON_TEXT}" "targets" ${index} "supported_executable_catalog_id")
        list(APPEND FF7RP_RELEASE_TARGET_CATALOG_IDS "${target_catalog_id}")
        if(target_catalog_id STREQUAL "${FF7RP_GAME_BUILD}")
            set(FF7RP_RELEASE_CATALOG "${target_catalog_id}")
            string(JSON FF7RP_RELEASE_ARCHIVE_BASENAME
                GET "${FF7RP_RELEASE_JSON_TEXT}" "targets" ${index} "archive_basename")
        endif()
    endforeach()
endif()
if(NOT FF7RP_RELEASE_CATALOG)
    message(FATAL_ERROR
        "release.json declares no target for FF7RP_GAME_BUILD='${FF7RP_GAME_BUILD}'; "
        "release targets: ${FF7RP_RELEASE_TARGET_CATALOG_IDS}")
endif()
# Checking the basename against the catalog's game version also binds release.json's notion
# of the shipped game build to the catalog build the ASI is actually compiled against.
if(NOT FF7RP_RELEASE_ARCHIVE_BASENAME STREQUAL
   "${FF7RP_RELEASE_PRODUCT}-${FF7RP_RELEASE_VERSION}-${FF7RP_RELEASE_PLATFORM}-ff7r${FF7RP_GAME_BUILD_VERSION}")
    message(FATAL_ERROR
        "release.json target '${FF7RP_RELEASE_CATALOG}' declares archive_basename "
        "'${FF7RP_RELEASE_ARCHIVE_BASENAME}', which does not name game build "
        "'${FF7RP_GAME_BUILD_VERSION}' from src/game/rva_catalog.json")
endif()
set(FF7RP_RELEASE_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${FF7RP_RELEASE_GENERATED_DIR}")
configure_file("${FF7RP_RELEASE_TEMPLATE}"
    "${FF7RP_RELEASE_GENERATED_DIR}/release_identity.generated.h" @ONLY NEWLINE_STYLE LF)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${FF7RP_RELEASE_JSON}" "${FF7RP_RELEASE_TEMPLATE}")
endif()

function(ff7rp_apply_cpp_policy target)
    cmake_parse_arguments(POLICY "OMIT_EHSC;OMIT_FP_PRECISE" "" "" ${ARGN})
    if(POLICY_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unknown C++ policy arguments for ${target}: ${POLICY_UNPARSED_ARGUMENTS}")
    endif()

    target_compile_features(${target} PRIVATE ${FF7RP_FIRST_PARTY_CXX_FEATURE})
    if(NOT target STREQUAL "ff7rp_pipeline")
        target_compile_definitions(${target} PRIVATE FF7RP_TARGET_BUILD_ID="${FF7RP_GAME_BUILD}")
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive->")
        if(NOT POLICY_OMIT_EHSC)
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
        endif()
        if(NOT POLICY_OMIT_FP_PRECISE)
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/fp:precise>)
        endif()
    endif()
endfunction()

if(FF7RP_BUILD_RUNTIME)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(FF7RP_PWSH_EXECUTABLE NAMES pwsh REQUIRED)

set(FF7RP_GENERATED_CATALOG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/generated/${FF7RP_GAME_BUILD}")
set(FF7RP_RVA_GENERATED_FILES
    "${FF7RP_GENERATED_CATALOG_DIR}/game/generated/rvas.generated.h"
    "${FF7RP_GENERATED_CATALOG_DIR}/game/generated/hook_specs.generated.inc"
    "${FF7RP_GENERATED_CATALOG_DIR}/game/generated/rva_signatures.generated.inc"
    "${FF7RP_GENERATED_CATALOG_DIR}/game/generated/runtime_locator_specs.generated.inc"
    "${FF7RP_GENERATED_CATALOG_DIR}/core/generated/build_identity.generated.h"
)
add_library(ff7rp_generated_catalog INTERFACE)
target_include_directories(ff7rp_generated_catalog INTERFACE "${FF7RP_GENERATED_CATALOG_DIR}")

# Usage requirements are appended after a target's own include directories, so the
# BEFORE entry pins the selected build's tree ahead of "src". CMake keeps the first
# occurrence of a duplicated include directory, so the path is still emitted once.
function(ff7rp_use_generated_catalog target)
    target_link_libraries(${target} PRIVATE ff7rp_generated_catalog)
    target_include_directories(${target} BEFORE PRIVATE "${FF7RP_GENERATED_CATALOG_DIR}")
endfunction()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${FF7RP_RVA_CATALOG}"
    "${FF7RP_RVA_GENERATOR}"
)
add_custom_target(check_rva_catalog
    COMMAND "${Python3_EXECUTABLE}" "${FF7RP_RVA_GENERATOR}" --check
        --temp-dir "${CMAKE_CURRENT_BINARY_DIR}/rva_catalog_check"
    DEPENDS "${FF7RP_RVA_CATALOG}" "${FF7RP_RVA_GENERATOR}" ${FF7RP_RVA_GENERATED_FILES}
    COMMENT "Validating canonical FF7 Rebirth RVA catalog"
    VERBATIM
)
endif()

include(FetchContent)
find_package(Git REQUIRED)
if(FF7RP_BUILD_RUNTIME)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
)
FetchContent_MakeAvailable(minhook)
endif()

FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 9634bedb5b5a2ca38c1ee7108a9358a4e233f14d
)
FetchContent_MakeAvailable(miniaudio)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_MIDILIBRARY_ONLY ON CACHE BOOL "Build only the midifile library" FORCE)
FetchContent_Declare(
    midifile
    GIT_REPOSITORY https://github.com/craigsapp/midifile.git
    GIT_TAG 98917df5b1bf0d6e8d4c0e5fff86d6b05343e793
    PATCH_COMMAND "${CMAKE_COMMAND}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DPATCH_SOURCE_DIR=<SOURCE_DIR>"
        "-DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/midifile-running-status.patch"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_source_patch.cmake"
)
FetchContent_MakeAvailable(midifile)

# Audio transcription for songs without song.mid, CPU only. The patch raises
# ggml's x86 baseline from Ivy Bridge to AVX2/FMA (about 8x faster on CPU).
set(MUSCRIPTOR_NATIVE OFF CACHE BOOL "" FORCE)
set(MUSCRIPTOR_VULKAN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    muscriptor
    GIT_REPOSITORY https://github.com/DamRsn/muscriptor.cpp.git
    GIT_TAG c8adc019677ed3479fa3c22c1a117d662382f347
    SOURCE_SUBDIR cpp
    PATCH_COMMAND "${CMAKE_COMMAND}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DPATCH_SOURCE_DIR=<SOURCE_DIR>"
        "-DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/muscriptor-avx2.patch"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_source_patch.cmake"
)
FetchContent_MakeAvailable(muscriptor)
FetchContent_GetProperties(ggml)
# ggml enables the generic ASM language, which would otherwise claim .asm files for cl.exe.
set_source_files_properties(
    src/game/chord_voicing_bridge.asm src/game/chord_voicing_bridge_harness.asm
    src/proxy/entry_thunk.asm src/proxy/forwarding_thunks.asm
    PROPERTIES LANGUAGE ASM_MASM)
if(FF7RP_BUILD_RUNTIME)
    # Packaged beside the DLL. A matching existing file is not downloaded again.
    file(DOWNLOAD
        "https://huggingface.co/DamRsn/muscriptor-gguf/resolve/d7045f94e8b19427f4ff9542975035e66596e51c/v1/muscriptor-small-f16.gguf"
        "${CMAKE_BINARY_DIR}/models/muscriptor-small-f16.gguf"
        EXPECTED_HASH SHA256=925f55af65a20ebc4f8b45ceaf095a12b72493d436cb112623cd0041a1af23d4)
endif()

file(GLOB_RECURSE FF7RPIANOSONGS_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
)

list(FILTER FF7RPIANOSONGS_SOURCES EXCLUDE REGEX "/tests/")
list(FILTER FF7RPIANOSONGS_SOURCES EXCLUDE REGEX "/tools/")
list(FILTER FF7RPIANOSONGS_SOURCES EXCLUDE REGEX "/pipeline/")
list(FILTER FF7RPIANOSONGS_SOURCES EXCLUDE REGEX "/proxy/")
list(FILTER FF7RPIANOSONGS_SOURCES EXCLUDE REGEX "/transcription/")

set(FF7RP_PIPELINE_SOURCES
    src/pipeline/audio_artifact_builder.cpp
    src/pipeline/audio_loudness.cpp
    src/pipeline/audio_metronome.cpp
    src/pipeline/audio_reader.cpp
    src/pipeline/audio_transcription.cpp
    src/pipeline/cache.cpp
    src/pipeline/cache_artifact_writer.cpp
    src/pipeline/cache_manifest_renderer.cpp
    src/pipeline/chart_compiler.cpp
    src/pipeline/hca/hca_encoder.cpp
    src/pipeline/hca/hca_tables.cpp
    src/pipeline/mabf_builder.cpp
    src/pipeline/midi_analysis_core.cpp
    src/pipeline/midi_chart_compilation.cpp
    src/pipeline/midi_chart_generator.cpp
    src/pipeline/midi_source_normalizer.cpp
    src/pipeline/native_asset_capabilities.cpp
    src/pipeline/native_music_export.cpp
    src/pipeline/native_iostore_source.cpp
    src/pipeline/oodle_decoder.cpp
    src/pipeline/native_asset_hash.cpp
    src/pipeline/native_zen_package.cpp
    src/pipeline/native_iostore_writer.cpp
    src/pipeline/native_audio_cohort.cpp
    src/pipeline/native_audio_cache.cpp
    src/pipeline/resolved_song_renderer.cpp
    src/pipeline/runtime_cache_codec.cpp
    src/pipeline/runtime_artifact_validator.cpp
    src/pipeline/song_json.cpp
    src/pipeline/song_repository.cpp
    src/pipeline/song_preview.cpp
    src/pipeline/song_source_template.cpp
    src/pipeline/wav_reader.cpp
)
add_library(ff7rp_pipeline STATIC ${FF7RP_PIPELINE_SOURCES})
ff7rp_apply_cpp_policy(ff7rp_pipeline)
target_compile_definitions(ff7rp_pipeline PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(ff7rp_pipeline PUBLIC
    "src" "${miniaudio_SOURCE_DIR}" "${midifile_SOURCE_DIR}/include")
target_link_libraries(ff7rp_pipeline PUBLIC midifile)
if(WIN32)
    target_link_libraries(ff7rp_pipeline PRIVATE bcrypt)
endif()

# Kept out of ff7rp_pipeline so self-tests do not link ggml.
add_library(ff7rp_transcription STATIC src/transcription/muscriptor_transcriber.cpp)
ff7rp_apply_cpp_policy(ff7rp_transcription)
target_compile_definitions(ff7rp_transcription PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(ff7rp_transcription PUBLIC ff7rp_pipeline PRIVATE muscriptor_ggml)
if(FF7RP_BUILD_RUNTIME)
set(FF7RP_PROXY_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated-proxy")
set(FF7RP_PROXY_PROFILE "${FF7RP_PROXY_GENERATED_DIR}/proxy/generated/native_proxy_profile.generated.h")
add_custom_command(OUTPUT "${FF7RP_PROXY_PROFILE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_native_proxy_profile.py"
        --catalog "${FF7RP_RVA_CATALOG}" --build "${FF7RP_GAME_BUILD}" --output "${FF7RP_PROXY_PROFILE}"
    DEPENDS "${FF7RP_RVA_CATALOG}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_native_proxy_profile.py"
    VERBATIM)
add_library(FF7RPianoSongsProxy SHARED
    ${FF7RPIANOSONGS_SOURCES} src/game/chord_voicing_bridge.asm
    src/proxy/proxy_main.cpp src/proxy/entry_bridge.cpp src/proxy/entry_thunk.asm
    src/proxy/forwarding.cpp src/proxy/forwarding_thunks.asm src/proxy/forwarding.def
    "${FF7RP_PROXY_PROFILE}")
add_dependencies(FF7RPianoSongsProxy check_rva_catalog)
ff7rp_apply_cpp_policy(FF7RPianoSongsProxy)
ff7rp_use_generated_catalog(FF7RPianoSongsProxy)
target_include_directories(FF7RPianoSongsProxy PRIVATE src "${FF7RP_PROXY_GENERATED_DIR}"
    "${FF7RP_RELEASE_GENERATED_DIR}" "${miniaudio_SOURCE_DIR}" "${midifile_SOURCE_DIR}/include")
target_compile_definitions(FF7RPianoSongsProxy PRIVATE FF7RP_OWNED_PROXY WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(FF7RPianoSongsProxy PRIVATE ff7rp_transcription ff7rp_pipeline minhook midifile user32 hid ole32 windowscodecs)
if(MSVC)
    target_link_options(FF7RPianoSongsProxy PRIVATE /INCREMENTAL:NO)
endif()
set_target_properties(FF7RPianoSongsProxy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/proxy"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
add_library(FF7RPianoSongs SHARED ${FF7RPIANOSONGS_SOURCES})
target_sources(FF7RPianoSongs PRIVATE src/game/chord_voicing_bridge.asm)
add_dependencies(FF7RPianoSongs check_rva_catalog)
set_property(TARGET FF7RPianoSongs APPEND PROPERTY LINK_DEPENDS
    "${FF7RP_RVA_CATALOG}"
    "${FF7RP_RVA_GENERATOR}"
    ${FF7RP_RVA_GENERATED_FILES}
)

ff7rp_apply_cpp_policy(FF7RPianoSongs)
target_compile_definitions(FF7RPianoSongs PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(FF7RPianoSongs PRIVATE "src" "${FF7RP_RELEASE_GENERATED_DIR}" "${FF7RP_PROXY_GENERATED_DIR}"
    "${miniaudio_SOURCE_DIR}" "${midifile_SOURCE_DIR}/include")
target_link_libraries(FF7RPianoSongs PRIVATE ff7rp_transcription ff7rp_pipeline minhook midifile user32 hid ole32 windowscodecs)
ff7rp_use_generated_catalog(FF7RPianoSongs)

add_executable(native_proxy_entry_bridge_selftest
    src/tests/native_proxy_entry_bridge_selftest.cpp src/proxy/entry_thunk.asm)
ff7rp_apply_cpp_policy(native_proxy_entry_bridge_selftest)
target_include_directories(native_proxy_entry_bridge_selftest PRIVATE src)
target_compile_definitions(native_proxy_entry_bridge_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_executable(native_proxy_forwarding_selftest
    src/tests/native_proxy_forwarding_selftest.cpp src/proxy/forwarding_thunks.asm)
ff7rp_apply_cpp_policy(native_proxy_forwarding_selftest)
target_include_directories(native_proxy_forwarding_selftest PRIVATE src)
target_compile_definitions(native_proxy_forwarding_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_executable(native_proxy_startup_diagnostic_selftest
    src/tests/native_proxy_startup_diagnostic_selftest.cpp "${FF7RP_PROXY_PROFILE}")
ff7rp_apply_cpp_policy(native_proxy_startup_diagnostic_selftest)
target_include_directories(native_proxy_startup_diagnostic_selftest PRIVATE src "${FF7RP_PROXY_GENERATED_DIR}")
target_compile_definitions(native_proxy_startup_diagnostic_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(native_proxy_startup_diagnostic_selftest PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>)
if(MSVC)
    set_source_files_properties(src/proxy/entry_bridge.cpp PROPERTIES
        COMPILE_OPTIONS "/O2;/Oi;/GS-;/GR-")
    target_compile_options(native_proxy_entry_bridge_selftest PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/O2;/Oi;/GS-;/GR->)
endif()

if(MSVC)
    target_link_options(FF7RPianoSongs PRIVATE /INCREMENTAL:NO)
endif()

set_target_properties(FF7RPianoSongs PROPERTIES
    OUTPUT_NAME "FF7RPianoSongs"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
endif()

add_executable(mabf_builder_selftest
    src/tests/mabf_builder_selftest.cpp
)
ff7rp_apply_cpp_policy(mabf_builder_selftest)
target_compile_definitions(mabf_builder_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(mabf_builder_selftest PRIVATE "src")
target_link_libraries(mabf_builder_selftest PRIVATE ff7rp_pipeline)

add_executable(pipeline_selftest
    src/tests/pipeline_selftest.cpp
)
ff7rp_apply_cpp_policy(pipeline_selftest)
target_compile_definitions(pipeline_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(pipeline_selftest PRIVATE "src")
target_link_libraries(pipeline_selftest PRIVATE ff7rp_pipeline)

add_executable(hca_encoder_selftest
    src/tests/hca_encoder_selftest.cpp
)
ff7rp_apply_cpp_policy(hca_encoder_selftest)
target_compile_definitions(hca_encoder_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(hca_encoder_selftest PRIVATE "src")
target_link_libraries(hca_encoder_selftest PRIVATE ff7rp_pipeline)

add_executable(audio_artifact_builder_selftest
    src/tests/audio_artifact_builder_selftest.cpp
)
ff7rp_apply_cpp_policy(audio_artifact_builder_selftest)
target_compile_definitions(audio_artifact_builder_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(audio_artifact_builder_selftest PRIVATE "src")
target_link_libraries(audio_artifact_builder_selftest PRIVATE ff7rp_pipeline)

add_executable(native_music_export_selftest src/tests/native_music_export_selftest.cpp)
ff7rp_apply_cpp_policy(native_music_export_selftest)
target_compile_definitions(native_music_export_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(native_music_export_selftest PRIVATE "src")
target_link_libraries(native_music_export_selftest PRIVATE ff7rp_pipeline)

add_executable(native_iostore_source_selftest src/tests/native_iostore_source_selftest.cpp)
ff7rp_apply_cpp_policy(native_iostore_source_selftest)
target_compile_definitions(native_iostore_source_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(native_iostore_source_selftest PRIVATE "src")
target_link_libraries(native_iostore_source_selftest PRIVATE ff7rp_pipeline)

add_executable(oodle_decoder_selftest src/tests/oodle_decoder_selftest.cpp)
ff7rp_apply_cpp_policy(oodle_decoder_selftest)
target_compile_definitions(oodle_decoder_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(oodle_decoder_selftest PRIVATE "src")
target_link_libraries(oodle_decoder_selftest PRIVATE ff7rp_pipeline)

foreach(native_writer_test IN ITEMS native_iostore_writer_selftest native_audio_cohort_selftest native_audio_cache_selftest native_audio_warm_selftest)
    add_executable(${native_writer_test} src/tests/${native_writer_test}.cpp)
    ff7rp_apply_cpp_policy(${native_writer_test})
    target_compile_definitions(${native_writer_test} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(${native_writer_test} PRIVATE "src")
    target_link_libraries(${native_writer_test} PRIVATE ff7rp_pipeline)
endforeach()

add_executable(wav_reader_selftest
    src/tests/wav_reader_selftest.cpp
)
ff7rp_apply_cpp_policy(wav_reader_selftest)
target_compile_definitions(wav_reader_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(wav_reader_selftest PRIVATE "src" "${miniaudio_SOURCE_DIR}")
target_link_libraries(wav_reader_selftest PRIVATE ff7rp_pipeline)

add_executable(encoded_audio_fixture_selftest src/tests/encoded_audio_fixture_selftest.cpp)
ff7rp_apply_cpp_policy(encoded_audio_fixture_selftest)
target_compile_definitions(encoded_audio_fixture_selftest PRIVATE
    FF7RP_ENCODED_AUDIO_FIXTURE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/src/tests/fixtures/encoded_audio")
target_include_directories(encoded_audio_fixture_selftest PRIVATE "src")
target_link_libraries(encoded_audio_fixture_selftest PRIVATE ff7rp_pipeline)

if(FF7RP_BUILD_RUNTIME)
add_executable(registry_selftest
    src/tests/registry_selftest.cpp
    src/game/completion_timing.cpp
    src/game/song_registry.cpp
)
ff7rp_apply_cpp_policy(registry_selftest)
target_compile_definitions(registry_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    FF7RP_SONG_REGISTRY_SELFTEST=1)
target_include_directories(registry_selftest PRIVATE "src")

add_executable(catalog_adoption_selftest
    src/tests/catalog_adoption_selftest.cpp
    src/game/catalog_adoption.cpp
    src/game/song_registry.cpp
    src/game/native_array_publication.cpp
    src/core/hooks.cpp
    src/core/logging.cpp
    src/core/pe_image.cpp
    src/game/audio_production_wiring.cpp
    src/startup/startup_cache_progress.cpp
)
ff7rp_apply_cpp_policy(catalog_adoption_selftest)
target_compile_definitions(catalog_adoption_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    FF7RP_CATALOG_ADOPTION_SELFTEST=1)
target_include_directories(catalog_adoption_selftest PRIVATE "src")
target_link_libraries(catalog_adoption_selftest PRIVATE minhook)
ff7rp_use_generated_catalog(catalog_adoption_selftest)

add_executable(release_hook_empty_catalog_selftest
    src/tests/release_hook_empty_catalog_selftest.cpp
    src/game/runtime.cpp
    src/core/hooks.cpp
    src/core/pe_image.cpp
    src/core/logging.cpp
)
ff7rp_apply_cpp_policy(release_hook_empty_catalog_selftest)
ff7rp_use_generated_catalog(release_hook_empty_catalog_selftest)
target_compile_definitions(release_hook_empty_catalog_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(release_hook_empty_catalog_selftest PRIVATE "src")
target_link_libraries(release_hook_empty_catalog_selftest PRIVATE minhook)

add_executable(piano_page_selection_guard_selftest
    src/tests/piano_page_selection_guard_selftest.cpp
    src/game/piano_page_selection_guard.cpp
)
ff7rp_apply_cpp_policy(piano_page_selection_guard_selftest)
target_compile_definitions(piano_page_selection_guard_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    FF7RP_PIANO_PAGE_SELECTION_GUARD_SELFTEST=1)
target_include_directories(piano_page_selection_guard_selftest PRIVATE "src")
ff7rp_use_generated_catalog(piano_page_selection_guard_selftest)

add_executable(list_catalog_transaction_selftest
    src/tests/list_catalog_transaction_selftest.cpp
    src/game/list_patch.cpp
    src/game/menu_theme_metrics.cpp
    src/game/menu_session_state.cpp
    src/game/song_registry.cpp
    src/game/native_array_publication.cpp
    src/core/hooks.cpp
    src/core/pe_image.cpp
    src/core/logging.cpp
)
ff7rp_apply_cpp_policy(list_catalog_transaction_selftest)
target_compile_definitions(list_catalog_transaction_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    FF7RP_LIST_CATALOG_SELFTEST=1)
target_include_directories(list_catalog_transaction_selftest PRIVATE "src")
target_link_libraries(list_catalog_transaction_selftest PRIVATE minhook)
ff7rp_use_generated_catalog(list_catalog_transaction_selftest)

add_executable(profile_list_coordinator_selftest
    src/tests/profile_list_coordinator_selftest.cpp
    src/game/profile_list_coordinator.cpp
    src/game/menu_session_state.cpp
    src/game/song_registry.cpp
)
add_executable(menu_session_authority_selftest
    src/tests/menu_session_authority_selftest.cpp
    src/game/menu_session_state.cpp
)
ff7rp_apply_cpp_policy(menu_session_authority_selftest)
target_compile_definitions(menu_session_authority_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(menu_session_authority_selftest PRIVATE "src")
ff7rp_apply_cpp_policy(profile_list_coordinator_selftest)
target_compile_definitions(profile_list_coordinator_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(profile_list_coordinator_selftest PRIVATE FF7RP_COORDINATOR_SELFTEST=1)
target_include_directories(profile_list_coordinator_selftest PRIVATE "src")

add_executable(release_audit_tool
    src/tools/release_audit_tool.cpp
    src/tests/documentation_parity.cpp
)
add_dependencies(release_audit_tool check_rva_catalog)
set_property(TARGET release_audit_tool APPEND PROPERTY LINK_DEPENDS
    "${FF7RP_RVA_CATALOG}"
    "${FF7RP_RVA_GENERATOR}"
    ${FF7RP_RVA_GENERATED_FILES}
)
ff7rp_apply_cpp_policy(release_audit_tool OMIT_FP_PRECISE)
target_compile_definitions(release_audit_tool PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(release_audit_tool PRIVATE "src" "${FF7RP_RELEASE_GENERATED_DIR}")
target_link_libraries(release_audit_tool PRIVATE ff7rp_pipeline)
ff7rp_use_generated_catalog(release_audit_tool)
endif()
add_executable(song_descriptor_builder_selftest
    src/tests/song_descriptor_builder_selftest.cpp
    src/song_descriptor_builder.cpp
    src/core/logging.cpp
)
ff7rp_apply_cpp_policy(song_descriptor_builder_selftest)
target_compile_definitions(song_descriptor_builder_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(song_descriptor_builder_selftest PRIVATE "src")
target_link_libraries(song_descriptor_builder_selftest PRIVATE ff7rp_pipeline)

add_executable(native_audio_preparation_selftest
    src/tests/native_audio_preparation_selftest.cpp
    src/startup/native_audio_preparation.cpp)
ff7rp_apply_cpp_policy(native_audio_preparation_selftest)
target_compile_definitions(native_audio_preparation_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(native_audio_preparation_selftest PRIVATE "src")
target_link_libraries(native_audio_preparation_selftest PRIVATE ff7rp_pipeline)

add_executable(native_audio_startup_latch_selftest src/tests/native_audio_startup_latch_selftest.cpp)
ff7rp_apply_cpp_policy(native_audio_startup_latch_selftest)
target_include_directories(native_audio_startup_latch_selftest PRIVATE "src")
if(FF7RP_BUILD_RUNTIME)
add_executable(music_repository_loader_selftest
    src/tests/music_repository_loader_selftest.cpp
    src/startup/music_repository_loader.cpp
    src/startup/startup_cache_progress.cpp
    src/song_descriptor_builder.cpp
    src/game/song_registry.cpp
    src/core/logging.cpp
)
ff7rp_apply_cpp_policy(music_repository_loader_selftest)
target_compile_definitions(music_repository_loader_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    FF7RP_MUSIC_REPOSITORY_SELFTEST=1)
target_include_directories(music_repository_loader_selftest PRIVATE "src")
target_link_libraries(music_repository_loader_selftest PRIVATE ff7rp_pipeline)
add_executable(startup_cache_overlay_selftest
    src/tests/startup_cache_overlay_selftest.cpp
    src/game/startup_cache_overlay.cpp
    src/game/song_preview_overlay.cpp
    src/startup/startup_cache_progress.cpp
    src/core/hooks.cpp
    src/core/logging.cpp
    src/core/pe_image.cpp
    src/game/hook_specs.cpp
)
add_dependencies(startup_cache_overlay_selftest check_rva_catalog)
ff7rp_apply_cpp_policy(startup_cache_overlay_selftest)
target_compile_definitions(startup_cache_overlay_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(startup_cache_overlay_selftest PRIVATE "src")
target_link_libraries(startup_cache_overlay_selftest PRIVATE minhook)
ff7rp_use_generated_catalog(startup_cache_overlay_selftest)
endif()
add_executable(song_cache_tool
    tools/song_cache_tool.cpp
)
ff7rp_apply_cpp_policy(song_cache_tool)
target_compile_definitions(song_cache_tool PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(song_cache_tool PRIVATE "src" "${miniaudio_SOURCE_DIR}" "${midifile_SOURCE_DIR}/include")
target_link_libraries(song_cache_tool PRIVATE ff7rp_transcription ff7rp_pipeline midifile)
add_executable(hca_parity_fixture
    tools/hca_parity_fixture.cpp
)
ff7rp_apply_cpp_policy(hca_parity_fixture)
target_compile_definitions(hca_parity_fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(hca_parity_fixture PRIVATE "src")
target_link_libraries(hca_parity_fixture PRIVATE ff7rp_pipeline)
add_executable(song_repository_benchmark
    src/tests/song_repository_benchmark.cpp
)
ff7rp_apply_cpp_policy(song_repository_benchmark)
target_compile_definitions(song_repository_benchmark PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(song_repository_benchmark PRIVATE "src" "${miniaudio_SOURCE_DIR}" "${midifile_SOURCE_DIR}/include")
target_link_libraries(song_repository_benchmark PRIVATE ff7rp_pipeline midifile)
add_executable(midi_generator_selftest
    src/tests/midi_generator_selftest.cpp
)
ff7rp_apply_cpp_policy(midi_generator_selftest)
target_compile_definitions(midi_generator_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(midi_generator_selftest PRIVATE "src" "${midifile_SOURCE_DIR}/include")
target_link_libraries(midi_generator_selftest PRIVATE ff7rp_pipeline midifile)
add_executable(midi_chart_compilation_selftest
    src/tests/midi_chart_compilation_selftest.cpp
)
ff7rp_apply_cpp_policy(midi_chart_compilation_selftest)
target_compile_definitions(midi_chart_compilation_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(midi_chart_compilation_selftest PRIVATE "src" "${midifile_SOURCE_DIR}/include")
target_link_libraries(midi_chart_compilation_selftest PRIVATE ff7rp_pipeline midifile)
add_executable(midi_analysis_core_selftest
    src/tests/midi_analysis_core_selftest.cpp
    src/pipeline/midi_analysis_core.cpp
)
ff7rp_apply_cpp_policy(midi_analysis_core_selftest)
target_include_directories(midi_analysis_core_selftest PRIVATE "src")
add_executable(song_repository_selftest
    src/tests/song_repository_selftest.cpp
)
ff7rp_apply_cpp_policy(song_repository_selftest)
target_compile_definitions(song_repository_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(song_repository_selftest PRIVATE "src" "${miniaudio_SOURCE_DIR}" "${midifile_SOURCE_DIR}/include")
target_link_libraries(song_repository_selftest PRIVATE ff7rp_pipeline midifile)

add_executable(song_preview_selftest src/tests/song_preview_selftest.cpp)
ff7rp_apply_cpp_policy(song_preview_selftest)
target_link_libraries(song_preview_selftest PRIVATE ff7rp_pipeline)
add_executable(resolved_song_renderer_selftest
    src/tests/resolved_song_renderer_selftest.cpp
)
target_include_directories(resolved_song_renderer_selftest PRIVATE "src")
target_link_libraries(resolved_song_renderer_selftest PRIVATE ff7rp_pipeline)
ff7rp_apply_cpp_policy(resolved_song_renderer_selftest)
add_executable(metronome_selftest
    src/tests/metronome_selftest.cpp
)
ff7rp_apply_cpp_policy(metronome_selftest)
target_compile_definitions(metronome_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(metronome_selftest PRIVATE "src" "${midifile_SOURCE_DIR}/include")
target_link_libraries(metronome_selftest PRIVATE ff7rp_pipeline midifile)
if(FF7RP_BUILD_RUNTIME)
add_executable(extended_chart_selftest
    src/tests/extended_chart_selftest.cpp
    src/tools/extended_chart_fixture_tool.cpp
    src/core/logging.cpp
    src/core/pe_image.cpp
    src/game/chart_patch.cpp
    src/game/hook_specs.cpp
    src/game/synthetic_extended_chart_model.cpp
)
add_dependencies(extended_chart_selftest check_rva_catalog)
ff7rp_apply_cpp_policy(extended_chart_selftest OMIT_FP_PRECISE)
target_compile_definitions(extended_chart_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    FF7RP_EXTENDED_CHART_FIXTURE_LIBRARY FF7RP_CHART_PATCH_SELFTEST)
target_include_directories(extended_chart_selftest PRIVATE "src")
ff7rp_use_generated_catalog(extended_chart_selftest)

add_executable(chord_voicing_selftest
    src/tests/chord_voicing_selftest.cpp
    src/game/chord_voicing.cpp
    src/game/chord_voicing_bridge.asm
    src/game/chord_voicing_bridge_harness.asm
    src/game/song_registry.cpp
    src/game/hook_specs.cpp
    src/core/hooks.cpp
    src/core/pe_image.cpp
    src/core/logging.cpp)
ff7rp_apply_cpp_policy(chord_voicing_selftest)
target_compile_definitions(chord_voicing_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    FF7RP_CHORD_VOICING_SELFTEST=1)
target_include_directories(chord_voicing_selftest PRIVATE "src")
target_link_libraries(chord_voicing_selftest PRIVATE ff7rp_pipeline minhook)
ff7rp_use_generated_catalog(chord_voicing_selftest)
add_dependencies(chord_voicing_selftest check_rva_catalog)
add_executable(runtime_lifecycle_selftest
    src/tests/runtime_lifecycle_selftest.cpp
    src/tests/runtime_lifecycle_foundation_tests.cpp
    src/tests/runtime_lifecycle_onmemory_tests.cpp
    src/tests/runtime_lifecycle_cleanup_retirement_tests.cpp
    src/tests/runtime_lifecycle_integration_tests.cpp
    src/core/hooks.cpp
    src/core/pe_image.cpp
    src/game/audio_cleanup_policy.cpp
    src/game/native_array_publication.cpp
    src/game/song_registry.cpp
)
ff7rp_apply_cpp_policy(runtime_lifecycle_selftest OMIT_FP_PRECISE)
target_compile_definitions(runtime_lifecycle_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(runtime_lifecycle_selftest PRIVATE "src")
target_link_libraries(runtime_lifecycle_selftest PRIVATE minhook)
add_executable(audio_native_call_policy_selftest
    src/tests/audio_native_call_policy_selftest.cpp
)
ff7rp_apply_cpp_policy(audio_native_call_policy_selftest OMIT_FP_PRECISE)
target_compile_definitions(audio_native_call_policy_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(audio_native_call_policy_selftest PRIVATE "src")
add_executable(audio_production_wiring_selftest
    src/tests/audio_production_wiring_selftest.cpp
    src/game/audio_production_wiring.cpp
    src/core/hooks.cpp
    src/core/pe_image.cpp
)
add_dependencies(audio_production_wiring_selftest check_rva_catalog)
ff7rp_apply_cpp_policy(audio_production_wiring_selftest OMIT_FP_PRECISE)
target_compile_definitions(audio_production_wiring_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(audio_production_wiring_selftest PRIVATE "src")
target_link_libraries(audio_production_wiring_selftest PRIVATE minhook)
ff7rp_use_generated_catalog(audio_production_wiring_selftest)
add_executable(audio_memory_read_policy_selftest
    src/tests/audio_memory_read_policy_selftest.cpp
)
ff7rp_apply_cpp_policy(audio_memory_read_policy_selftest OMIT_FP_PRECISE)
target_compile_definitions(audio_memory_read_policy_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(audio_memory_read_policy_selftest PRIVATE "src")
add_executable(pause_resume_policy_selftest
    src/tests/pause_resume_policy_selftest.cpp
)
ff7rp_apply_cpp_policy(pause_resume_policy_selftest OMIT_FP_PRECISE)
target_compile_definitions(pause_resume_policy_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(pause_resume_policy_selftest PRIVATE "src")
add_executable(selection_substrate_policy_selftest
    src/tests/selection_substrate_policy_selftest.cpp
)
ff7rp_apply_cpp_policy(selection_substrate_policy_selftest OMIT_FP_PRECISE)
target_compile_definitions(selection_substrate_policy_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(selection_substrate_policy_selftest PRIVATE "src")
add_executable(bgm_playback_aggregate_policy_selftest
    src/tests/bgm_playback_aggregate_policy_selftest.cpp
)
ff7rp_apply_cpp_policy(bgm_playback_aggregate_policy_selftest OMIT_FP_PRECISE)
target_compile_definitions(bgm_playback_aggregate_policy_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(bgm_playback_aggregate_policy_selftest PRIVATE "src")
add_executable(mabf_sidecar_loader_selftest
    src/tests/mabf_sidecar_loader_selftest.cpp
    src/game/mabf_sidecar_loader.cpp
    src/core/logging.cpp
)
ff7rp_apply_cpp_policy(mabf_sidecar_loader_selftest)
target_compile_definitions(mabf_sidecar_loader_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX FF7RP_AUDIO_SIDECAR_SELFTEST=1)
target_include_directories(mabf_sidecar_loader_selftest PRIVATE "src")
target_link_libraries(mabf_sidecar_loader_selftest PRIVATE ff7rp_pipeline)
add_executable(audio_pointer_patch_runtime_selftest
    src/tests/audio_pointer_patch_runtime_selftest.cpp
    src/game/audio_pointer_patch_runtime.cpp
    src/game/audio_cleanup_policy.cpp
    src/core/logging.cpp
)
ff7rp_apply_cpp_policy(audio_pointer_patch_runtime_selftest OMIT_FP_PRECISE)
target_compile_definitions(audio_pointer_patch_runtime_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(audio_pointer_patch_runtime_selftest PRIVATE "src")
add_executable(audio_play_setup_diagnostics_selftest
    src/tests/audio_play_setup_diagnostics_selftest.cpp
    src/game/audio_play_setup_diagnostics.cpp
    src/game/audio_cleanup_policy.cpp
    src/core/logging.cpp
)
ff7rp_apply_cpp_policy(audio_play_setup_diagnostics_selftest OMIT_FP_PRECISE)
target_compile_definitions(audio_play_setup_diagnostics_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(audio_play_setup_diagnostics_selftest PRIVATE "src")
add_executable(scoreinfo_result_policy_selftest
    src/tests/scoreinfo_result_policy_selftest.cpp
)
ff7rp_apply_cpp_policy(scoreinfo_result_policy_selftest OMIT_FP_PRECISE)
target_compile_definitions(scoreinfo_result_policy_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(scoreinfo_result_policy_selftest PRIVATE "src")
add_executable(startup_thread_selftest
    src/tests/startup_thread_selftest.cpp
    src/core/startup_thread.cpp
)
ff7rp_apply_cpp_policy(startup_thread_selftest OMIT_FP_PRECISE)
target_compile_definitions(startup_thread_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(startup_thread_selftest PRIVATE "src")
add_executable(uobject_identity_selftest
    src/tests/uobject_identity_selftest.cpp
    src/game/uobject_locator_core.cpp
    src/game/uobject_identity_lifecycle.cpp
)
ff7rp_apply_cpp_policy(uobject_identity_selftest OMIT_FP_PRECISE)
target_compile_definitions(uobject_identity_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(uobject_identity_selftest PRIVATE "src")
add_executable(native_audio_decoder_selftest src/tests/native_audio_decoder_selftest.cpp)
ff7rp_apply_cpp_policy(native_audio_decoder_selftest OMIT_FP_PRECISE)
ff7rp_use_generated_catalog(native_audio_decoder_selftest)
target_compile_definitions(native_audio_decoder_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(native_audio_decoder_selftest PRIVATE "src")
target_link_libraries(native_audio_decoder_selftest PRIVATE ff7rp_pipeline)

add_executable(native_audio_bindings_selftest
    src/tests/native_audio_bindings_selftest.cpp
    src/game/native_audio_bindings.cpp
    src/game/uobject_locator_core.cpp
)
ff7rp_apply_cpp_policy(native_audio_bindings_selftest OMIT_FP_PRECISE)
ff7rp_use_generated_catalog(native_audio_bindings_selftest)
target_compile_definitions(native_audio_bindings_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
# This mapped-memory fixture uses assert expressions to populate/check identities.
target_compile_options(native_audio_bindings_selftest PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>")
target_include_directories(native_audio_bindings_selftest PRIVATE "src")
add_executable(native_audio_runtime_selftest
    src/tests/native_audio_runtime_selftest.cpp
    src/game/native_audio_bootstrap.cpp
    src/game/native_audio_bindings.cpp
    src/game/native_audio_mount.cpp
    src/game/uobject_locator_core.cpp
    src/game/hook_specs.cpp
    src/core/hooks.cpp
    src/core/logging.cpp
    src/core/pe_image.cpp
)
ff7rp_apply_cpp_policy(native_audio_runtime_selftest OMIT_FP_PRECISE)
ff7rp_use_generated_catalog(native_audio_runtime_selftest)
target_compile_definitions(native_audio_runtime_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(native_audio_runtime_selftest PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>")
target_include_directories(native_audio_runtime_selftest PRIVATE "src")
target_link_libraries(native_audio_runtime_selftest PRIVATE ff7rp_pipeline minhook)
add_executable(native_audio_adapter_selftest
    src/tests/native_audio_adapter_selftest.cpp
    src/game/menu_session_state.cpp
    src/game/native_audio_bootstrap.cpp
    src/game/native_audio_bindings.cpp
    src/game/native_audio_mount.cpp
    src/game/uobject_locator_core.cpp
    src/game/hook_specs.cpp
    src/game/song_registry.cpp
    src/game/audio_production_wiring.cpp
    src/core/hooks.cpp
    src/core/logging.cpp
    src/core/pe_image.cpp
)
ff7rp_apply_cpp_policy(native_audio_adapter_selftest OMIT_FP_PRECISE)
ff7rp_use_generated_catalog(native_audio_adapter_selftest)
target_compile_definitions(native_audio_adapter_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(native_audio_adapter_selftest PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>")
target_include_directories(native_audio_adapter_selftest PRIVATE "src")
target_link_libraries(native_audio_adapter_selftest PRIVATE ff7rp_pipeline minhook)

add_executable(native_audio_extended_composition_selftest
    src/tests/native_audio_extended_composition_selftest.cpp
    src/tests/native_audio_extended_source_fixture.cpp
    src/game/menu_session_state.cpp
    src/game/native_audio_bootstrap.cpp
    src/game/native_audio_bindings.cpp
    src/game/native_audio_mount.cpp
    src/game/uobject_locator_core.cpp
    src/game/hook_specs.cpp
    src/game/song_registry.cpp
    src/game/audio_production_wiring.cpp
    src/core/hooks.cpp
    src/core/logging.cpp
    src/core/pe_image.cpp
)
ff7rp_apply_cpp_policy(native_audio_extended_composition_selftest OMIT_FP_PRECISE)
ff7rp_use_generated_catalog(native_audio_extended_composition_selftest)
target_compile_definitions(native_audio_extended_composition_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(native_audio_extended_composition_selftest PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG;/Gy>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>")
target_link_options(native_audio_extended_composition_selftest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/OPT:REF;/INCREMENTAL:NO>")
target_include_directories(native_audio_extended_composition_selftest PRIVATE "src")
target_link_libraries(native_audio_extended_composition_selftest PRIVATE ff7rp_pipeline minhook Psapi bcrypt)

add_executable(native_proxy_bootstrap_selftest
    src/tests/native_proxy_bootstrap_selftest.cpp
    src/game/native_audio_bootstrap.cpp
    src/game/native_audio_bindings.cpp
    src/game/native_audio_mount.cpp
    src/game/uobject_locator_core.cpp
    src/game/hook_specs.cpp
    src/core/hooks.cpp src/core/logging.cpp src/core/pe_image.cpp)
ff7rp_apply_cpp_policy(native_proxy_bootstrap_selftest OMIT_FP_PRECISE)
ff7rp_use_generated_catalog(native_proxy_bootstrap_selftest)
target_compile_definitions(native_proxy_bootstrap_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(native_proxy_bootstrap_selftest PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>")
target_include_directories(native_proxy_bootstrap_selftest PRIVATE src "${FF7RP_GENERATED_PROXY_DIR}")
target_link_libraries(native_proxy_bootstrap_selftest PRIVATE ff7rp_pipeline minhook)
add_executable(native_proxy_unavailable_selftest
    src/tests/native_proxy_bootstrap_selftest.cpp src/core/pe_image.cpp)
ff7rp_apply_cpp_policy(native_proxy_unavailable_selftest)
ff7rp_use_generated_catalog(native_proxy_unavailable_selftest)
target_include_directories(native_proxy_unavailable_selftest PRIVATE src)
target_compile_definitions(native_proxy_unavailable_selftest PRIVATE
    FF7RP_NATIVE_PROXY_UNAVAILABLE_SELFTEST WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(native_proxy_unavailable_selftest PRIVATE bcrypt)
target_compile_options(native_proxy_unavailable_selftest PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>)
foreach(target IN ITEMS native_audio_runtime_selftest native_audio_adapter_selftest
        native_audio_extended_composition_selftest)
    target_sources(${target} PRIVATE src/game/native_proxy_bootstrap.cpp)
    target_include_directories(${target} PRIVATE "${FF7RP_GENERATED_PROXY_DIR}")
endforeach()

add_executable(chart_selected_row_selftest
    src/tests/chart_selected_row_selftest.cpp
    src/game/song_registry.cpp
    src/core/pe_image.cpp
    src/core/logging.cpp)
ff7rp_apply_cpp_policy(chart_selected_row_selftest)
ff7rp_use_generated_catalog(chart_selected_row_selftest)
target_compile_definitions(chart_selected_row_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(chart_selected_row_selftest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/Gy>")
target_link_options(chart_selected_row_selftest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/OPT:REF;/INCREMENTAL:NO>")
target_include_directories(chart_selected_row_selftest PRIVATE "src")
target_link_libraries(chart_selected_row_selftest PRIVATE ff7rp_pipeline)

add_executable(scoreinfo_overlay_producer_selftest
    src/tests/scoreinfo_overlay_producer_selftest.cpp
    src/game/song_registry.cpp
    src/game/hook_specs.cpp
    src/core/hooks.cpp
    src/core/pe_image.cpp
    src/core/logging.cpp
)
ff7rp_apply_cpp_policy(scoreinfo_overlay_producer_selftest)
ff7rp_use_generated_catalog(scoreinfo_overlay_producer_selftest)
target_compile_definitions(scoreinfo_overlay_producer_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(scoreinfo_overlay_producer_selftest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/Gy>")
target_link_options(scoreinfo_overlay_producer_selftest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/OPT:REF;/INCREMENTAL:NO>")
target_include_directories(scoreinfo_overlay_producer_selftest PRIVATE "src")
target_link_libraries(scoreinfo_overlay_producer_selftest PRIVATE minhook Psapi)

add_executable(config_selftest src/tests/config_selftest.cpp src/core/config.cpp src/core/theme.cpp)
ff7rp_apply_cpp_policy(config_selftest)
target_compile_definitions(config_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(config_selftest PRIVATE "src")
target_link_libraries(config_selftest PRIVATE ole32 windowscodecs)

add_executable(theme_selftest src/tests/theme_selftest.cpp src/core/theme.cpp)
ff7rp_apply_cpp_policy(theme_selftest)
target_compile_definitions(theme_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(theme_selftest PRIVATE "src")
target_link_libraries(theme_selftest PRIVATE ole32 windowscodecs)

add_executable(menu_theme_plan_selftest src/tests/menu_theme_plan_selftest.cpp
    src/game/menu_theme_plan.cpp src/core/theme.cpp)
ff7rp_apply_cpp_policy(menu_theme_plan_selftest)
target_compile_definitions(menu_theme_plan_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(menu_theme_plan_selftest PRIVATE "src")
target_link_libraries(menu_theme_plan_selftest PRIVATE ole32 windowscodecs)

add_executable(menu_theme_native_selftest src/tests/menu_theme_native_selftest.cpp
    src/core/pe_image.cpp src/core/version_gate.cpp src/game/uobject_locator_core.cpp
    src/game/menu_theme_metrics.cpp)
add_dependencies(menu_theme_native_selftest check_rva_catalog)
ff7rp_apply_cpp_policy(menu_theme_native_selftest)
ff7rp_use_generated_catalog(menu_theme_native_selftest)
target_compile_definitions(menu_theme_native_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(menu_theme_native_selftest PRIVATE "src")

add_executable(menu_theme_observation_selftest src/tests/menu_theme_observation_selftest.cpp
    src/core/pe_image.cpp src/core/version_gate.cpp src/game/uobject_locator_core.cpp
    src/game/menu_theme_metrics.cpp)
add_dependencies(menu_theme_observation_selftest check_rva_catalog)
ff7rp_apply_cpp_policy(menu_theme_observation_selftest)
ff7rp_use_generated_catalog(menu_theme_observation_selftest)
target_compile_definitions(menu_theme_observation_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(menu_theme_observation_selftest PRIVATE "src")

add_executable(menu_theme_runtime_selftest src/tests/menu_theme_runtime_selftest.cpp
    src/core/hooks.cpp src/game/uobject_locator_core.cpp src/game/menu_theme_metrics.cpp)
add_dependencies(menu_theme_runtime_selftest check_rva_catalog)
ff7rp_apply_cpp_policy(menu_theme_runtime_selftest)
ff7rp_use_generated_catalog(menu_theme_runtime_selftest)
target_compile_definitions(menu_theme_runtime_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(menu_theme_runtime_selftest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/Gy>")
target_link_options(menu_theme_runtime_selftest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/OPT:REF;/INCREMENTAL:NO>")
target_include_directories(menu_theme_runtime_selftest PRIVATE "src")
target_link_libraries(menu_theme_runtime_selftest PRIVATE minhook User32)

add_executable(menu_theme_palette_selftest src/tests/menu_theme_palette_selftest.cpp
    src/core/hooks.cpp src/core/pe_image.cpp src/game/menu_theme_metrics.cpp)
ff7rp_apply_cpp_policy(menu_theme_palette_selftest)
ff7rp_use_generated_catalog(menu_theme_palette_selftest)
target_compile_definitions(menu_theme_palette_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(menu_theme_palette_selftest PRIVATE "src"
    "$<TARGET_PROPERTY:minhook,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(menu_theme_palette_selftest PRIVATE User32)

add_executable(menu_theme_metrics_selftest src/tests/menu_theme_metrics_selftest.cpp
    src/game/menu_theme_metrics.cpp)
ff7rp_apply_cpp_policy(menu_theme_metrics_selftest)
target_compile_definitions(menu_theme_metrics_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    FF7RP_MENU_THEME_METRICS_SELFTEST=1)
target_include_directories(menu_theme_metrics_selftest PRIVATE "src")

add_executable(executable_mapping_selftest src/tests/executable_mapping_selftest.cpp)
ff7rp_apply_cpp_policy(executable_mapping_selftest)
target_compile_definitions(executable_mapping_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(executable_mapping_selftest PRIVATE "src")

add_executable(toggle_feedback_selftest src/tests/toggle_feedback_selftest.cpp)
ff7rp_apply_cpp_policy(toggle_feedback_selftest)
target_compile_definitions(toggle_feedback_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(toggle_feedback_selftest PRIVATE "src")

foreach(preview_test IN ITEMS song_preview_policy_selftest preview_audio_selftest song_preview_runtime_selftest song_preview_overlay_selftest song_preview_anchor_selftest)
    add_executable(${preview_test} src/tests/${preview_test}.cpp)
    ff7rp_apply_cpp_policy(${preview_test})
    target_compile_definitions(${preview_test} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(${preview_test} PRIVATE "src")
endforeach()
target_sources(song_preview_runtime_selftest PRIVATE src/game/song_registry.cpp)
add_executable(preview_game_volume_selftest src/tests/preview_game_volume_selftest.cpp
    src/game/preview_game_volume.cpp src/core/pe_image.cpp src/core/version_gate.cpp)
ff7rp_apply_cpp_policy(preview_game_volume_selftest)
ff7rp_use_generated_catalog(preview_game_volume_selftest)
target_compile_definitions(preview_game_volume_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(preview_game_volume_selftest PRIVATE "src")
add_test(NAME preview_game_volume_selftest COMMAND preview_game_volume_selftest)
target_sources(song_preview_overlay_selftest PRIVATE src/game/song_preview_overlay.cpp)
target_sources(song_preview_anchor_selftest PRIVATE src/game/song_preview_anchor.cpp src/game/song_preview_overlay.cpp)

add_executable(autoplay_runtime_selftest
    src/tests/autoplay_runtime_selftest.cpp
    src/game/hook_specs.cpp src/core/pe_image.cpp src/core/hooks.cpp src/core/logging.cpp)
ff7rp_apply_cpp_policy(autoplay_runtime_selftest)
ff7rp_use_generated_catalog(autoplay_runtime_selftest)
target_compile_definitions(autoplay_runtime_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(autoplay_runtime_selftest PRIVATE "src")
target_link_libraries(autoplay_runtime_selftest PRIVATE minhook Psapi)

add_executable(autoplay_progress_selftest
    src/tests/autoplay_progress_selftest.cpp src/game/song_registry.cpp
    src/core/pe_image.cpp src/core/hooks.cpp src/core/logging.cpp src/game/hook_specs.cpp)
ff7rp_apply_cpp_policy(autoplay_progress_selftest)
ff7rp_use_generated_catalog(autoplay_progress_selftest)
target_compile_definitions(autoplay_progress_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(autoplay_progress_selftest PRIVATE "src")
target_link_libraries(autoplay_progress_selftest PRIVATE minhook Psapi)

add_executable(title_input_service_selftest
    src/tests/title_input_service_selftest.cpp
    src/game/menu_theme_metrics.cpp
    src/game/profile_list_coordinator.cpp
    src/game/song_registry.cpp
    src/game/hook_specs.cpp
    src/core/pe_image.cpp
    src/core/hooks.cpp
    src/core/logging.cpp)
ff7rp_apply_cpp_policy(title_input_service_selftest)
ff7rp_use_generated_catalog(title_input_service_selftest)
target_compile_definitions(title_input_service_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(title_input_service_selftest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/Gy>")
target_link_options(title_input_service_selftest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/OPT:REF;/INCREMENTAL:NO>")
target_include_directories(title_input_service_selftest PRIVATE "src")
target_link_libraries(title_input_service_selftest PRIVATE minhook User32 Hid Psapi)
endif()
enable_testing()
if(NOT FF7RP_BUILD_RUNTIME)
    add_test(NAME offline_configuration_selftest
        COMMAND ${CMAKE_COMMAND}
            -DFF7RP_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DFF7RP_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/offline-configuration-selftest
            -DFF7RP_TEST_BUILD_ID=${FF7RP_GAME_BUILD}
            -DFF7RP_MINIAUDIO_SOURCE_DIR=${miniaudio_SOURCE_DIR}
            -DFF7RP_MIDIFILE_SOURCE_DIR=${midifile_SOURCE_DIR}
            -DFF7RP_MUSCRIPTOR_SOURCE_DIR=${muscriptor_SOURCE_DIR}
            -DFF7RP_GGML_SOURCE_DIR=${ggml_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/src/tests/offline_configuration_selftest.cmake)
endif()
if(FF7RP_BUILD_RUNTIME)
add_test(NAME config_selftest COMMAND config_selftest)
add_test(NAME theme_selftest COMMAND theme_selftest)
set_tests_properties(theme_selftest PROPERTIES TIMEOUT 60)
add_test(NAME menu_theme_plan_selftest COMMAND menu_theme_plan_selftest)
set_tests_properties(menu_theme_plan_selftest PROPERTIES TIMEOUT 60)
add_test(NAME menu_theme_native_selftest COMMAND menu_theme_native_selftest)
set_tests_properties(menu_theme_native_selftest PROPERTIES TIMEOUT 60)
add_test(NAME menu_theme_metrics_selftest COMMAND menu_theme_metrics_selftest)
set_tests_properties(menu_theme_metrics_selftest PROPERTIES TIMEOUT 60 LABELS functional)
add_test(NAME executable_mapping_selftest COMMAND executable_mapping_selftest)
set_tests_properties(executable_mapping_selftest PROPERTIES TIMEOUT 60 LABELS functional)
add_test(NAME menu_theme_observation_selftest COMMAND menu_theme_observation_selftest)
set_tests_properties(menu_theme_observation_selftest PROPERTIES TIMEOUT 60)
add_test(NAME menu_theme_runtime_selftest COMMAND menu_theme_runtime_selftest)
set_tests_properties(menu_theme_runtime_selftest PROPERTIES TIMEOUT 60)
add_test(NAME menu_theme_palette_selftest COMMAND menu_theme_palette_selftest)
set_tests_properties(menu_theme_palette_selftest PROPERTIES TIMEOUT 60)
add_test(NAME autoplay_runtime_selftest COMMAND autoplay_runtime_selftest)
add_test(NAME autoplay_progress_selftest COMMAND autoplay_progress_selftest)
set_tests_properties(autoplay_progress_selftest PROPERTIES TIMEOUT 60)
set_tests_properties(autoplay_runtime_selftest PROPERTIES TIMEOUT 60)
set_tests_properties(config_selftest PROPERTIES TIMEOUT 60)
add_test(NAME toggle_feedback_selftest COMMAND toggle_feedback_selftest)
set_tests_properties(toggle_feedback_selftest PROPERTIES TIMEOUT 60)
add_test(NAME song_preview_policy_selftest COMMAND song_preview_policy_selftest)
add_test(NAME preview_audio_selftest COMMAND preview_audio_selftest)
add_test(NAME song_preview_runtime_selftest COMMAND song_preview_runtime_selftest)
add_test(NAME song_preview_overlay_selftest COMMAND song_preview_overlay_selftest)
add_test(NAME song_preview_anchor_selftest COMMAND song_preview_anchor_selftest)
set_tests_properties(song_preview_policy_selftest preview_audio_selftest song_preview_runtime_selftest song_preview_overlay_selftest song_preview_anchor_selftest
    PROPERTIES TIMEOUT 60)
add_test(NAME rva_catalog_check
    COMMAND "${Python3_EXECUTABLE}" "${FF7RP_RVA_GENERATOR}" --check
        --temp-dir "${CMAKE_CURRENT_BINARY_DIR}/rva_catalog_ctest"
)
add_test(NAME rva_catalog_generator_selftest
    COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_generate_rva_catalog.py"
)
add_test(NAME native_cached_resource_catalog_selftest
    COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_native_cached_resource_catalog.py"
)
endif()
add_test(NAME mabf_builder_selftest COMMAND mabf_builder_selftest)
add_test(NAME pipeline_selftest COMMAND pipeline_selftest)
add_test(NAME hca_encoder_selftest COMMAND hca_encoder_selftest)
add_test(NAME audio_artifact_builder_selftest COMMAND audio_artifact_builder_selftest)
add_test(NAME native_music_export_selftest COMMAND native_music_export_selftest)
add_test(NAME native_iostore_source_selftest COMMAND native_iostore_source_selftest)
add_test(NAME oodle_decoder_selftest COMMAND oodle_decoder_selftest)
add_test(NAME native_iostore_writer_selftest COMMAND native_iostore_writer_selftest)
add_test(NAME native_audio_cohort_selftest COMMAND native_audio_cohort_selftest)
add_test(NAME native_audio_cache_selftest COMMAND native_audio_cache_selftest)
add_test(NAME native_audio_warm_selftest COMMAND native_audio_warm_selftest)
add_test(NAME wav_reader_selftest COMMAND wav_reader_selftest)
add_test(NAME encoded_audio_fixture_selftest COMMAND encoded_audio_fixture_selftest)
if(FF7RP_BUILD_RUNTIME)
add_test(NAME registry_selftest COMMAND registry_selftest)
add_test(NAME catalog_adoption_selftest COMMAND catalog_adoption_selftest)
add_test(NAME release_hook_empty_catalog_selftest COMMAND release_hook_empty_catalog_selftest)
add_test(NAME piano_page_selection_guard_selftest COMMAND piano_page_selection_guard_selftest)
add_test(NAME list_catalog_transaction_selftest COMMAND list_catalog_transaction_selftest)
add_test(NAME profile_list_coordinator_selftest COMMAND profile_list_coordinator_selftest)
add_test(NAME menu_session_authority_selftest COMMAND menu_session_authority_selftest)
endif()
add_test(NAME song_descriptor_builder_selftest COMMAND song_descriptor_builder_selftest)
add_test(NAME native_audio_preparation_selftest COMMAND native_audio_preparation_selftest)
add_test(NAME native_audio_startup_latch_selftest COMMAND native_audio_startup_latch_selftest)
if(FF7RP_BUILD_RUNTIME)
add_test(NAME music_repository_loader_selftest COMMAND music_repository_loader_selftest)
add_test(NAME startup_cache_overlay_selftest COMMAND startup_cache_overlay_selftest)
endif()
add_test(NAME midi_generator_selftest COMMAND midi_generator_selftest)
add_test(NAME midi_chart_compilation_selftest COMMAND midi_chart_compilation_selftest)
add_test(NAME midi_analysis_core_selftest COMMAND midi_analysis_core_selftest)
add_test(NAME song_repository_selftest COMMAND song_repository_selftest)
add_test(NAME song_preview_selftest COMMAND song_preview_selftest)
add_test(NAME resolved_song_renderer_selftest COMMAND resolved_song_renderer_selftest)

add_executable(runtime_cache_codec_selftest
    src/tests/runtime_cache_codec_selftest.cpp
)
target_link_libraries(runtime_cache_codec_selftest PRIVATE ff7rp_pipeline)
ff7rp_apply_cpp_policy(runtime_cache_codec_selftest)
add_test(NAME runtime_cache_codec_selftest COMMAND runtime_cache_codec_selftest)
add_executable(cache_manifest_renderer_selftest
    src/tests/cache_manifest_renderer_selftest.cpp
)
target_link_libraries(cache_manifest_renderer_selftest PRIVATE ff7rp_pipeline)
ff7rp_apply_cpp_policy(cache_manifest_renderer_selftest)
add_test(NAME cache_manifest_renderer_selftest COMMAND cache_manifest_renderer_selftest)
add_executable(runtime_artifact_validator_selftest
    src/tests/runtime_artifact_validator_selftest.cpp
)
target_link_libraries(runtime_artifact_validator_selftest PRIVATE ff7rp_pipeline)
ff7rp_apply_cpp_policy(runtime_artifact_validator_selftest)
add_test(NAME runtime_artifact_validator_selftest COMMAND runtime_artifact_validator_selftest)
add_executable(cache_artifact_writer_selftest
    src/tests/cache_artifact_writer_selftest.cpp
)
target_link_libraries(cache_artifact_writer_selftest PRIVATE ff7rp_pipeline)
ff7rp_apply_cpp_policy(cache_artifact_writer_selftest)
add_test(NAME cache_artifact_writer_selftest COMMAND cache_artifact_writer_selftest)
add_test(NAME metronome_selftest COMMAND metronome_selftest)
if(FF7RP_BUILD_RUNTIME)
add_test(NAME extended_chart_selftest COMMAND extended_chart_selftest)
add_test(NAME chord_voicing_selftest COMMAND chord_voicing_selftest)
add_test(NAME runtime_lifecycle_selftest COMMAND runtime_lifecycle_selftest)
add_test(NAME audio_native_call_policy_selftest COMMAND audio_native_call_policy_selftest)
add_test(NAME audio_production_wiring_selftest COMMAND audio_production_wiring_selftest)
add_test(NAME audio_memory_read_policy_selftest COMMAND audio_memory_read_policy_selftest)
add_test(NAME pause_resume_policy_selftest COMMAND pause_resume_policy_selftest)
add_test(NAME selection_substrate_policy_selftest COMMAND selection_substrate_policy_selftest)
add_test(NAME bgm_playback_aggregate_policy_selftest COMMAND bgm_playback_aggregate_policy_selftest)
add_test(NAME mabf_sidecar_loader_selftest COMMAND mabf_sidecar_loader_selftest)
add_test(NAME audio_pointer_patch_runtime_selftest COMMAND audio_pointer_patch_runtime_selftest)
add_test(NAME audio_play_setup_diagnostics_selftest COMMAND audio_play_setup_diagnostics_selftest)
add_test(NAME scoreinfo_result_policy_selftest COMMAND scoreinfo_result_policy_selftest)
add_test(NAME startup_thread_selftest COMMAND startup_thread_selftest)
add_test(NAME uobject_identity_selftest COMMAND uobject_identity_selftest)
add_test(NAME native_audio_decoder_selftest COMMAND native_audio_decoder_selftest)
add_test(NAME native_audio_bindings_selftest COMMAND native_audio_bindings_selftest)
add_test(NAME native_audio_runtime_selftest COMMAND native_audio_runtime_selftest)
add_test(NAME native_proxy_entry_bridge_selftest COMMAND native_proxy_entry_bridge_selftest)
add_test(NAME native_proxy_forwarding_selftest COMMAND native_proxy_forwarding_selftest)
add_test(NAME native_proxy_startup_diagnostic_selftest COMMAND native_proxy_startup_diagnostic_selftest)
add_test(NAME native_proxy_bootstrap_selftest COMMAND native_proxy_bootstrap_selftest)
add_test(NAME native_proxy_unavailable_selftest COMMAND native_proxy_unavailable_selftest)
add_test(NAME native_proxy_profile_selftest COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_native_proxy_profile.py")
add_test(NAME native_proxy_callback_selftest COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_native_proxy_callback.py" --selftest)
if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    add_test(NAME native_proxy_callback_artifact_check COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_native_proxy_callback.py"
        --object "${CMAKE_BINARY_DIR}/FF7RPianoSongsProxy.dir/$<CONFIG>/main.obj"
        --image "$<TARGET_FILE:FF7RPianoSongsProxy>")
    set_tests_properties(native_proxy_callback_artifact_check PROPERTIES TIMEOUT 60 LABELS "runtime;artifact")
endif()
if(MSVC)
    get_filename_component(FF7RP_COMPILER_DIRECTORY "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(FF7RP_VCVARS64
        "${FF7RP_COMPILER_DIRECTORY}/../../../../../../Auxiliary/Build/vcvars64.bat" ABSOLUTE)
    add_test(NAME native_proxy_process_selftest COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tests/native_proxy_process_selftest.py" --vcvars "${FF7RP_VCVARS64}")
    set_tests_properties(native_proxy_process_selftest PROPERTIES TIMEOUT 180 LABELS "runtime;integration")
endif()
add_test(NAME native_audio_adapter_selftest COMMAND native_audio_adapter_selftest)
add_test(NAME native_audio_extended_composition_selftest COMMAND native_audio_extended_composition_selftest)
add_test(NAME chart_selected_row_selftest COMMAND chart_selected_row_selftest)
add_test(NAME scoreinfo_overlay_producer_selftest COMMAND scoreinfo_overlay_producer_selftest)
add_test(NAME title_input_service_selftest COMMAND title_input_service_selftest)
endif()
if(FF7RP_BUILD_RUNTIME)
add_test(NAME runtime_gate_selftest
    COMMAND "${FF7RP_PWSH_EXECUTABLE}" -NoProfile -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/runtime_gate_selftest.ps1"
        -RuntimeGateScript "${CMAKE_CURRENT_SOURCE_DIR}/tools/runtime_gate.ps1"
)
add_test(NAME build_provenance_selftest
    COMMAND "${FF7RP_PWSH_EXECUTABLE}" -NoProfile -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_provenance_selftest.ps1"
)
add_test(NAME build_catalog_selection_selftest
    COMMAND "${FF7RP_PWSH_EXECUTABLE}" -NoProfile -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_catalog_selection_selftest.ps1"
)
add_test(NAME release_package_selftest
    COMMAND "${FF7RP_PWSH_EXECUTABLE}" -NoProfile -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/release_package_selftest.ps1"
)
add_test(NAME tracked_binary_audit
    COMMAND "${FF7RP_PWSH_EXECUTABLE}" -NoProfile -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/audit_tracked_binaries.ps1"
)
add_test(NAME documentation_audit
    COMMAND release_audit_tool "${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME menu_theme_perf_report_selftest
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_menu_theme_perf_report.py"
        --metrics-selftest "$<TARGET_FILE:menu_theme_metrics_selftest>"
)
set_tests_properties(menu_theme_perf_report_selftest PROPERTIES TIMEOUT 60 LABELS "tooling")
endif()

set(FF7RP_BOUNDED_SELFTESTS
    rva_catalog_check
    rva_catalog_generator_selftest
    native_cached_resource_catalog_selftest
    mabf_builder_selftest
    pipeline_selftest
    runtime_cache_codec_selftest
    cache_manifest_renderer_selftest
    runtime_artifact_validator_selftest
    cache_artifact_writer_selftest
    audio_artifact_builder_selftest
    native_music_export_selftest
    native_iostore_source_selftest
    oodle_decoder_selftest
    native_iostore_writer_selftest
    native_audio_cohort_selftest
    native_audio_cache_selftest
    native_audio_warm_selftest
    hca_encoder_selftest
    wav_reader_selftest
    encoded_audio_fixture_selftest
    registry_selftest
    profile_list_coordinator_selftest
    menu_session_authority_selftest
    song_descriptor_builder_selftest
    native_audio_preparation_selftest
    native_audio_startup_latch_selftest
    music_repository_loader_selftest
    startup_cache_overlay_selftest
    midi_generator_selftest
    midi_chart_compilation_selftest
    midi_analysis_core_selftest
    song_repository_selftest
    song_preview_selftest
    resolved_song_renderer_selftest
    metronome_selftest
    extended_chart_selftest
    runtime_lifecycle_selftest
    audio_native_call_policy_selftest
    audio_production_wiring_selftest
    audio_memory_read_policy_selftest
    pause_resume_policy_selftest
    selection_substrate_policy_selftest
    bgm_playback_aggregate_policy_selftest
    mabf_sidecar_loader_selftest
    audio_pointer_patch_runtime_selftest
    audio_play_setup_diagnostics_selftest
    scoreinfo_result_policy_selftest
    startup_thread_selftest
    uobject_identity_selftest
    native_audio_decoder_selftest
    native_audio_bindings_selftest
    native_audio_runtime_selftest
    native_proxy_entry_bridge_selftest
    native_proxy_forwarding_selftest
    native_proxy_startup_diagnostic_selftest
    native_proxy_bootstrap_selftest
    native_proxy_unavailable_selftest
    native_proxy_profile_selftest
    native_proxy_callback_selftest
    native_audio_adapter_selftest
    native_audio_extended_composition_selftest
    chart_selected_row_selftest
    scoreinfo_overlay_producer_selftest
    title_input_service_selftest
    runtime_gate_selftest
    build_provenance_selftest
    build_catalog_selection_selftest
    release_package_selftest
    tracked_binary_audit
    documentation_audit
)
if(NOT FF7RP_BUILD_RUNTIME)
    list(REMOVE_ITEM FF7RP_BOUNDED_SELFTESTS
        rva_catalog_check rva_catalog_generator_selftest native_cached_resource_catalog_selftest registry_selftest
        profile_list_coordinator_selftest menu_session_authority_selftest
        music_repository_loader_selftest startup_cache_overlay_selftest extended_chart_selftest runtime_lifecycle_selftest
        audio_native_call_policy_selftest audio_production_wiring_selftest
        audio_memory_read_policy_selftest pause_resume_policy_selftest
        selection_substrate_policy_selftest bgm_playback_aggregate_policy_selftest
        mabf_sidecar_loader_selftest audio_pointer_patch_runtime_selftest
        audio_play_setup_diagnostics_selftest scoreinfo_result_policy_selftest
        startup_thread_selftest uobject_identity_selftest native_audio_decoder_selftest native_audio_bindings_selftest native_audio_runtime_selftest native_proxy_entry_bridge_selftest native_proxy_forwarding_selftest native_proxy_bootstrap_selftest native_audio_adapter_selftest native_audio_extended_composition_selftest chart_selected_row_selftest scoreinfo_overlay_producer_selftest title_input_service_selftest runtime_gate_selftest
        native_proxy_profile_selftest
        native_proxy_callback_selftest
        native_proxy_startup_diagnostic_selftest
        native_proxy_unavailable_selftest
        build_provenance_selftest build_catalog_selection_selftest release_package_selftest tracked_binary_audit documentation_audit)
endif()
set_tests_properties(${FF7RP_BOUNDED_SELFTESTS} PROPERTIES TIMEOUT 60)
if(FF7RP_BUILD_RUNTIME)
set_tests_properties(
    registry_selftest
    runtime_lifecycle_selftest
    audio_native_call_policy_selftest
    audio_production_wiring_selftest
    audio_memory_read_policy_selftest
    pause_resume_policy_selftest
    selection_substrate_policy_selftest
    bgm_playback_aggregate_policy_selftest
    mabf_sidecar_loader_selftest
    audio_pointer_patch_runtime_selftest
    audio_play_setup_diagnostics_selftest
    scoreinfo_result_policy_selftest
    startup_thread_selftest
    uobject_identity_selftest
    native_audio_decoder_selftest
    native_audio_bindings_selftest
    native_audio_runtime_selftest
    native_audio_adapter_selftest
    native_audio_extended_composition_selftest
    chart_selected_row_selftest
    scoreinfo_overlay_producer_selftest
    title_input_service_selftest
    runtime_gate_selftest
    PROPERTIES
    LABELS "runtime;context-regression")
endif()
set_tests_properties(midi_generator_selftest PROPERTIES
    TIMEOUT 180
    RUN_SERIAL TRUE
    PROCESSORS 16
    RESOURCE_LOCK midi_fixture_generation
)
# Includes repeated complete 2000-onset searches under both physical row policies.
set_tests_properties(midi_chart_compilation_selftest PROPERTIES
    TIMEOUT 360
    RUN_SERIAL TRUE
    PROCESSORS 16
    RESOURCE_LOCK midi_fixture_generation
)
# Complete cold/warm corpus needs headroom for variable host load (observed >6 minutes).
set_tests_properties(song_repository_selftest PROPERTIES
    TIMEOUT 600
    PROCESSORS 4
    RESOURCE_LOCK midi_fixture_generation
)

# Use CMake's configured registrations, not the spelling of add_test calls.
get_property(FF7RP_REGISTERED_TESTS DIRECTORY PROPERTY TESTS)
set(FF7RP_AUDIT_TESTS rva_catalog_check tracked_binary_audit documentation_audit)
set(FF7RP_TOOLING_TESTS rva_catalog_generator_selftest native_cached_resource_catalog_selftest runtime_gate_selftest
    build_provenance_selftest build_catalog_selection_selftest release_package_selftest)
foreach(test IN LISTS FF7RP_REGISTERED_TESTS)
    if(test IN_LIST FF7RP_AUDIT_TESTS)
        set_property(TEST ${test} APPEND PROPERTY LABELS audit)
    elseif(test IN_LIST FF7RP_TOOLING_TESTS)
        set_property(TEST ${test} APPEND PROPERTY LABELS tooling)
    else()
        set_property(TEST ${test} APPEND PROPERTY LABELS functional)
    endif()
endforeach()
list(SORT FF7RP_REGISTERED_TESTS)
string(JOIN "\",\n    \"" FF7RP_CTEST_INVENTORY ${FF7RP_REGISTERED_TESTS})
if(FF7RP_BUILD_RUNTIME)
file(CONFIGURE OUTPUT "${FF7RP_RELEASE_GENERATED_DIR}/ctest_inventory.generated.h"
    CONTENT "#pragma once\ninline constexpr const char* kConfiguredCTestNames[] = {\n    \"@FF7RP_CTEST_INVENTORY@\"\n};\n"
    @ONLY NEWLINE_STYLE LF)
endif()

if(FF7RP_BUILD_RUNTIME)
add_executable(extended_chart_fixture_tool
    src/tools/extended_chart_fixture_tool.cpp
)
ff7rp_apply_cpp_policy(extended_chart_fixture_tool OMIT_EHSC OMIT_FP_PRECISE)
target_include_directories(extended_chart_fixture_tool PRIVATE "src")
endif()
