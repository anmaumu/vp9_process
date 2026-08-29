cmake_minimum_required(VERSION 3.24)

project(vp9_process VERSION 0.1.0 LANGUAGES C CXX)

option(MKVC_BUILD_TESTS "Build native tests" ON)
option(MKVC_BUILD_PYTHON_TESTS "Build Python binding tests (requires NumPy)" OFF)
option(MKVC_BUILD_DOTNET_TESTS "Build and run the .NET P/Invoke smoke test" OFF)
option(MKVC_ENABLE_CPU_VP9 "Enable the libvpx CPU VP9 backend" ON)
option(MKVC_ENABLE_CPU_AV1 "Enable SVT-AV1 encode and libaom decode" ON)
option(MKVC_ENABLE_INTEL_ONEVPL "Enable Intel oneVPL backend foundation" ON)
option(MKVC_ENABLE_NVIDIA "Enable NVIDIA NVDEC/NVENC backend foundation" OFF)

if(MKVC_ENABLE_NVIDIA)
    include(FetchContent)
    FetchContent_Declare(nv_codec_headers
        URL https://github.com/FFmpeg/nv-codec-headers/archive/refs/tags/n13.1.15.0.tar.gz
        URL_HASH SHA256=2255bc74d038b95aa4be30f5f66322c2176acbdb90ada1851db6993536fbeaf7
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(nv_codec_headers)
endif()

if(MKVC_ENABLE_CPU_VP9 OR MKVC_ENABLE_CPU_AV1 OR MKVC_ENABLE_INTEL_ONEVPL OR MKVC_ENABLE_NVIDIA)
    find_package(unofficial-libwebm CONFIG REQUIRED)
    find_path(LIBWEBM_COMPAT_INCLUDE_DIR mkvmuxer/mkvmuxer.h
        PATH_SUFFIXES webm REQUIRED)
endif()

if(MKVC_ENABLE_CPU_VP9 OR MKVC_ENABLE_CPU_AV1 OR MKVC_ENABLE_INTEL_ONEVPL OR MKVC_ENABLE_NVIDIA)
    find_package(libyuv CONFIG REQUIRED)
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
    src/frame_processor.cpp
    src/gpu/gpu_frame.cpp
    src/gpu/gpu_frame_pool.cpp
    src/gpu/intel/intel_native_handle.cpp
    src/gpu/nvidia/nvidia_native_handle.cpp
    src/encoder_session.cpp
    src/cpu_av1_encoder.cpp
    src/cpu_av1_decoder.cpp
    src/intel_vpl_probe.cpp
    src/intel_vpl_encoder.cpp
    src/intel_vpl_decoder.cpp
    src/intel_webm_encoder.cpp
    src/intel_webm_decoder.cpp
    src/nvidia_probe.cpp
    src/nvidia_webm_decoder.cpp
    src/nvidia_webm_encoder.cpp
)

target_compile_features(mkvcodec PRIVATE cxx_std_17)
target_include_directories(mkvcodec
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_definitions(mkvcodec PRIVATE MKVC_BUILDING_LIBRARY)

if(MKVC_ENABLE_NVIDIA)
    target_sources(mkvcodec PRIVATE src/gpu/nvidia/cuda_completion.cpp)
    target_compile_definitions(mkvcodec PRIVATE MKVC_HAS_NVIDIA=1)
    target_include_directories(mkvcodec PRIVATE
        ${nv_codec_headers_SOURCE_DIR}/include)
    target_link_libraries(mkvcodec PRIVATE ${CMAKE_DL_LIBS})
    target_include_directories(mkvcodec PRIVATE ${LIBWEBM_COMPAT_INCLUDE_DIR})
    target_link_libraries(mkvcodec PRIVATE unofficial::libwebm::libwebm)
    target_link_libraries(mkvcodec PRIVATE yuv)
endif()

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
    target_sources(mkvcodec PRIVATE
        src/gpu/intel/intel_completion.cpp
        src/gpu/intel/intel_surface_factory.cpp)
    target_compile_definitions(mkvcodec PRIVATE MKVC_HAS_INTEL_ONEVPL=1)
    target_include_directories(mkvcodec PRIVATE ${LIBWEBM_COMPAT_INCLUDE_DIR})
    target_link_libraries(mkvcodec PRIVATE
        VPL::dispatcher unofficial::libwebm::libwebm yuv)
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
    target_compile_definitions(mkvcodec PRIVATE MKVC_ENABLE_TEST_HOOKS=1)
    if(Python3_Interpreter_FOUND)
        add_test(NAME mkvc_compliance_source
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/compliance_gate.py" source)
        add_test(NAME mkvc_compliance_unit
            COMMAND "${Python3_EXECUTABLE}" -m unittest
                tests/test_compliance_gate.py tests/test_collect_licenses.py
                tests/test_build_wheel.py tests/test_build_nuget.py)
        set_tests_properties(mkvc_compliance_unit PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    add_executable(mkvc_c_api_tests tests/c_api_tests.cpp)
    target_compile_features(mkvc_c_api_tests PRIVATE cxx_std_17)
    target_link_libraries(mkvc_c_api_tests PRIVATE mkvcodec)
    add_test(NAME mkvc_c_api_tests COMMAND mkvc_c_api_tests)

    add_executable(mkvc_gpu_frame_test
        tests/gpu_frame_test.cpp src/gpu/gpu_frame.cpp
        src/gpu/gpu_frame_pool.cpp
        src/gpu/intel/intel_native_handle.cpp
        src/gpu/nvidia/nvidia_native_handle.cpp)
    target_compile_features(mkvc_gpu_frame_test PRIVATE cxx_std_17)
    target_include_directories(mkvc_gpu_frame_test PRIVATE
        src/gpu src/gpu/intel src/gpu/nvidia include)
    if(MSVC)
        target_compile_options(mkvc_gpu_frame_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(mkvc_gpu_frame_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME mkvc_gpu_frame COMMAND mkvc_gpu_frame_test)

    if(MKVC_ENABLE_CPU_VP9 OR MKVC_ENABLE_CPU_AV1 OR
       MKVC_ENABLE_INTEL_ONEVPL OR MKVC_ENABLE_NVIDIA)
        add_executable(mkvc_frame_processor_test
            tests/frame_processor_test.cpp src/frame_processor.cpp)
        target_compile_features(mkvc_frame_processor_test PRIVATE cxx_std_17)
        target_compile_definitions(mkvc_frame_processor_test
            PRIVATE MKVC_HAS_CPU_VP9=1)
        target_include_directories(mkvc_frame_processor_test PRIVATE src include)
        target_link_libraries(mkvc_frame_processor_test PRIVATE yuv)
        add_test(NAME mkvc_frame_processor COMMAND mkvc_frame_processor_test)
    endif()

    if(MKVC_ENABLE_CPU_VP9)
        add_executable(mkvc_async_failure_test tests/async_failure_test.cpp)
        target_compile_features(mkvc_async_failure_test PRIVATE cxx_std_17)
        target_link_libraries(mkvc_async_failure_test PRIVATE mkvcodec)
        add_test(NAME mkvc_async_failure COMMAND mkvc_async_failure_test)
        set_tests_properties(mkvc_async_failure PROPERTIES TIMEOUT 15)
    endif()

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

    if(MKVC_ENABLE_NVIDIA)
        add_executable(mkvc_nvidia_probe_test
            tests/nvidia_probe_test.cpp src/nvidia_probe.cpp)
        target_compile_features(mkvc_nvidia_probe_test PRIVATE cxx_std_17)
        target_include_directories(mkvc_nvidia_probe_test PRIVATE
            src ${nv_codec_headers_SOURCE_DIR}/include)
        target_compile_definitions(mkvc_nvidia_probe_test
            PRIVATE MKVC_HAS_NVIDIA=1)
        target_link_libraries(mkvc_nvidia_probe_test PRIVATE ${CMAKE_DL_LIBS})
        if(MSVC)
            target_compile_options(mkvc_nvidia_probe_test PRIVATE /W4 /permissive- /EHsc)
        else()
            target_compile_options(mkvc_nvidia_probe_test PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME mkvc_nvidia_probe COMMAND mkvc_nvidia_probe_test)
        set_tests_properties(mkvc_nvidia_probe PROPERTIES SKIP_RETURN_CODE 77)

        add_executable(mkvc_nvidia_webm_encoder_test
            tests/nvidia_webm_encoder_test.cpp)
        target_compile_features(mkvc_nvidia_webm_encoder_test PRIVATE cxx_std_17)
        target_link_libraries(mkvc_nvidia_webm_encoder_test PRIVATE mkvcodec)
        add_test(NAME mkvc_nvidia_webm_encode
            COMMAND mkvc_nvidia_webm_encoder_test
                    "${CMAKE_CURRENT_BINARY_DIR}/nvidia_av1_test.webm")
    endif()

    if(MKVC_ENABLE_INTEL_ONEVPL AND MKVC_ENABLE_CPU_VP9 AND MKVC_ENABLE_CPU_AV1)
        add_executable(mkvc_intel_vpl_encode_test
            tests/intel_vpl_encode_test.cpp src/intel_vpl_encoder.cpp
            src/intel_vpl_decoder.cpp)
        target_compile_features(mkvc_intel_vpl_encode_test PRIVATE cxx_std_17)
        target_include_directories(mkvc_intel_vpl_encode_test
            PRIVATE src include)
        target_compile_definitions(mkvc_intel_vpl_encode_test
            PRIVATE MKVC_HAS_INTEL_ONEVPL=1 MKVC_ENABLE_TEST_HOOKS=1)
        target_link_libraries(mkvc_intel_vpl_encode_test PRIVATE
            VPL::dispatcher unofficial::libvpx::libvpx AOM::aom yuv)
        add_test(NAME mkvc_intel_vpl_encode COMMAND mkvc_intel_vpl_encode_test)
        set_tests_properties(mkvc_intel_vpl_encode PROPERTIES SKIP_RETURN_CODE 77)
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

        if(MKVC_ENABLE_NVIDIA)
            add_executable(mkvc_nvidia_webm_decoder_test
                tests/nvidia_webm_decoder_test.cpp
                src/nvidia_webm_decoder.cpp src/nvidia_probe.cpp)
            target_compile_features(mkvc_nvidia_webm_decoder_test PRIVATE cxx_std_17)
            target_include_directories(mkvc_nvidia_webm_decoder_test PRIVATE
                src include ${nv_codec_headers_SOURCE_DIR}/include
                ${LIBWEBM_COMPAT_INCLUDE_DIR})
            target_compile_definitions(mkvc_nvidia_webm_decoder_test
                PRIVATE MKVC_HAS_NVIDIA=1)
            target_link_libraries(mkvc_nvidia_webm_decoder_test PRIVATE
                unofficial::libwebm::libwebm ${CMAKE_DL_LIBS})
            if(MSVC)
                target_compile_options(mkvc_nvidia_webm_decoder_test PRIVATE
                    /W4 /permissive- /EHsc)
            else()
                target_compile_options(mkvc_nvidia_webm_decoder_test PRIVATE
                    -Wall -Wextra -Wpedantic -Werror)
            endif()
            add_test(NAME mkvc_nvidia_webm_decode
                COMMAND mkvc_nvidia_webm_decoder_test "${MKVC_VP9_TEST_OUTPUT}")
            set_tests_properties(mkvc_nvidia_webm_decode PROPERTIES
                FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77)
        endif()

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
        add_test(NAME mkvc_python_benchmark_smoke
            COMMAND ${CMAKE_COMMAND} -E env
                "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python"
                "MKVC_LIBRARY_PATH=$<TARGET_FILE:mkvcodec>"
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_benchmark_smoke.py")
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
        if(MKVC_ENABLE_INTEL_ONEVPL)
            add_test(NAME mkvc_python_intel_roundtrip
                COMMAND ${CMAKE_COMMAND} -E env
                    "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python"
                    "MKVC_LIBRARY_PATH=$<TARGET_FILE:mkvcodec>"
                    "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_intel_roundtrip.py")
            set_tests_properties(mkvc_python_intel_roundtrip
                PROPERTIES SKIP_RETURN_CODE 77)
        endif()
    endif()

    if(MKVC_BUILD_DOTNET_TESTS)
        find_program(DOTNET_EXECUTABLE dotnet REQUIRED)
        add_test(NAME mkvc_dotnet_build
            COMMAND "${DOTNET_EXECUTABLE}" build
                "${CMAKE_CURRENT_SOURCE_DIR}/dotnet/MkvCodec.Smoke/MkvCodec.Smoke.csproj"
                --configuration Release)
        add_test(NAME mkvc_dotnet_smoke
            COMMAND ${CMAKE_COMMAND} -E env
                "MKVC_LIBRARY_PATH=$<TARGET_FILE:mkvcodec>"
                "${DOTNET_EXECUTABLE}" run
                --project "${CMAKE_CURRENT_SOURCE_DIR}/dotnet/MkvCodec.Smoke/MkvCodec.Smoke.csproj"
                --configuration Release --no-build)
        set_tests_properties(mkvc_dotnet_smoke PROPERTIES
            DEPENDS mkvc_dotnet_build)
    endif()
endif()
