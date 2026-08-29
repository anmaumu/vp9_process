cmake_minimum_required(VERSION 3.24)

project(vp9_process VERSION 0.1.0 LANGUAGES C CXX)

option(MKVC_BUILD_TESTS "Build native tests" ON)
option(MKVC_BUILD_PYTHON_TESTS "Build Python binding tests (requires NumPy)" OFF)
option(MKVC_ENABLE_CPU_VP9 "Enable the libvpx CPU VP9 backend" ON)
option(MKVC_ENABLE_CPU_AV1 "Enable SVT-AV1 encode and libaom decode" ON)
option(MKVC_ENABLE_INTEL_ONEVPL "Enable Intel oneVPL backend foundation" ON)

if(MKVC_ENABLE_CPU_VP9 OR MKVC_ENABLE_CPU_AV1)
    find_package(unofficial-libwebm CONFIG REQUIRED)
    find_package(libyuv CONFIG REQUIRED)
    find_path(LIBWEBM_COMPAT_INCLUDE_DIR mkvmuxer/mkvmuxer.h
        PATH_SUFFIXES webm REQUIRED)
endif()

if(MKVC_ENABLE_INTEL_ONEVPL)
    find_package(VPL CONFIG REQUIRED)
endif()

if(MKVC_ENABLE_CPU_VP9)
    find_package(unofficial-libvpx CONFIG REQUIRED)
endif()

if(MKVC_ENABLE_CPU_AV1)
    find_package(AOM CONFIG REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SVT_AV1 REQUIRED IMPORTED_TARGET SvtAv1Enc)
endif()

add_library(mkvcodec SHARED
    src/c_api.cpp
    src/backend_registry.cpp
    src/cpu_vp9_encoder.cpp
    src/cpu_vp9_decoder.cpp
    src/frame_conversion.cpp
    src/encoder_session.cpp
    src/cpu_av1_encoder.cpp
    src/cpu_av1_decoder.cpp
    src/intel_vpl_probe.cpp
)

target_compile_features(mkvcodec PRIVATE cxx_std_17)
target_include_directories(mkvcodec
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_definitions(mkvcodec PRIVATE MKVC_BUILDING_LIBRARY)

if(MKVC_ENABLE_CPU_VP9)
    target_compile_definitions(mkvcodec PRIVATE MKVC_HAS_CPU_VP9=1)
    # libwebm's public headers include common/... relative to include/webm.
    target_include_directories(mkvcodec PRIVATE ${LIBWEBM_COMPAT_INCLUDE_DIR})
    target_link_libraries(mkvcodec PRIVATE
        unofficial::libvpx::libvpx
        unofficial::libwebm::libwebm
        yuv
    )
endif()

if(MKVC_ENABLE_INTEL_ONEVPL)
    target_compile_definitions(mkvcodec PRIVATE MKVC_HAS_INTEL_ONEVPL=1)
    target_link_libraries(mkvcodec PRIVATE VPL::dispatcher)
endif()

if(MKVC_ENABLE_CPU_AV1)
    target_compile_definitions(mkvcodec PRIVATE MKVC_HAS_CPU_AV1=1)
    target_link_libraries(mkvcodec PRIVATE
        AOM::aom PkgConfig::SVT_AV1
        unofficial::libwebm::libwebm yuv)
endif()

if(MSVC)
    target_compile_options(mkvcodec PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(mkvcodec PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(GNUInstallDirs)
install(TARGETS mkvcodec
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_custom_target(docgen-check
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/docgen.py check
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Validating generated documentation inputs")
    add_custom_target(docs
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/docgen.py build
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Building generated HTML documentation")
endif()

if(MKVC_BUILD_TESTS)
    enable_testing()
    add_executable(mkvc_c_api_tests tests/c_api_tests.cpp)
    target_compile_features(mkvc_c_api_tests PRIVATE cxx_std_17)
    target_link_libraries(mkvc_c_api_tests PRIVATE mkvcodec)
    add_test(NAME mkvc_c_api_tests COMMAND mkvc_c_api_tests)

    if(MKVC_ENABLE_INTEL_ONEVPL)
        add_executable(mkvc_intel_vpl_probe_test
            tests/intel_vpl_probe_test.cpp src/intel_vpl_probe.cpp)
        target_compile_features(mkvc_intel_vpl_probe_test PRIVATE cxx_std_17)
        target_include_directories(mkvc_intel_vpl_probe_test PRIVATE src)
        target_compile_definitions(mkvc_intel_vpl_probe_test
            PRIVATE MKVC_HAS_INTEL_ONEVPL=1)
        target_link_libraries(mkvc_intel_vpl_probe_test PRIVATE VPL::dispatcher)
        add_test(NAME mkvc_intel_vpl_probe COMMAND mkvc_intel_vpl_probe_test)
        set_tests_properties(mkvc_intel_vpl_probe PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    if(MKVC_ENABLE_CPU_VP9)
        add_executable(mkvc_cpu_vp9_encode_test tests/cpu_vp9_encode_test.cpp)
        target_compile_features(mkvc_cpu_vp9_encode_test PRIVATE cxx_std_17)
        target_link_libraries(mkvc_cpu_vp9_encode_test PRIVATE mkvcodec)
        set(MKVC_VP9_TEST_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/cpu_vp9_test.webm")
        add_test(NAME mkvc_cpu_vp9_encode
            COMMAND mkvc_cpu_vp9_encode_test "${MKVC_VP9_TEST_OUTPUT}")
        set_tests_properties(mkvc_cpu_vp9_encode PROPERTIES
            FIXTURES_SETUP cpu_vp9_sample)

        find_program(FFMPEG_EXECUTABLE ffmpeg)
        if(FFMPEG_EXECUTABLE)
            add_test(NAME mkvc_cpu_vp9_external_decode
                COMMAND "${FFMPEG_EXECUTABLE}" -v error -i
                        "${MKVC_VP9_TEST_OUTPUT}" -f null -)
            set_tests_properties(mkvc_cpu_vp9_external_decode PROPERTIES
                FIXTURES_REQUIRED cpu_vp9_sample)
        endif()

        find_program(FFPROBE_EXECUTABLE ffprobe)
        if(FFPROBE_EXECUTABLE)
            add_test(NAME mkvc_cpu_vp9_metadata
                COMMAND ${CMAKE_COMMAND}
                    -DFFPROBE_EXECUTABLE=${FFPROBE_EXECUTABLE}
                    -DINPUT_FILE=${MKVC_VP9_TEST_OUTPUT}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_vp9_webm.cmake)
            set_tests_properties(mkvc_cpu_vp9_metadata PROPERTIES
                FIXTURES_REQUIRED cpu_vp9_sample)
        endif()
    endif()

    if(MKVC_BUILD_PYTHON_TESTS)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME mkvc_python_roundtrip
            COMMAND ${CMAKE_COMMAND} -E env
                "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python"
                "MKVC_LIBRARY_PATH=$<TARGET_FILE:mkvcodec>"
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_roundtrip.py")
        if(MKVC_ENABLE_CPU_AV1 AND FFMPEG_EXECUTABLE AND FFPROBE_EXECUTABLE)
            add_test(NAME mkvc_python_av1_encode
                COMMAND ${CMAKE_COMMAND} -E env
                    "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python"
                    "MKVC_LIBRARY_PATH=$<TARGET_FILE:mkvcodec>"
                    "FFMPEG_EXECUTABLE=${FFMPEG_EXECUTABLE}"
                    "FFPROBE_EXECUTABLE=${FFPROBE_EXECUTABLE}"
                    "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_av1_encode.py")
        endif()
    endif()
endif()
