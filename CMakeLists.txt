cmake_minimum_required(VERSION 3.26)

project(vp9_process VERSION 0.1.0 LANGUAGES C CXX)

option(MKVC_BUILD_TESTS "Build native tests" ON)
option(MKVC_BUILD_PYTHON_TESTS "Build Python binding tests (requires NumPy)" OFF)
option(MKVC_BUILD_DOTNET_TESTS "Build and run the .NET P/Invoke smoke test" OFF)
option(MKVC_BUILD_PYTHON_DLPACK "Build the CPython stable-ABI DLPack capsule bridge" ON)
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
    find_package(VPL 2.10 CONFIG REQUIRED)
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
    src/cpu_frame_pool.cpp
    src/frame_conversion.cpp
    src/frame_processor.cpp
    src/gpu/gpu_frame.cpp
    src/gpu/gpu_frame_pool.cpp
    src/gpu/dlpack_adapter.cpp
    src/gpu/intel/intel_native_handle.cpp
    src/gpu/intel/va_completion.cpp
    src/gpu/intel/d3d11_completion.cpp
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
    if(WIN32)
        target_compile_definitions(mkvcodec PRIVATE NOMINMAX)
    endif()
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
    target_compile_definitions(mkvcodec PRIVATE
        MKVC_HAS_INTEL_ONEVPL=1 ONEVPL_EXPERIMENTAL=1)
    target_include_directories(mkvcodec PRIVATE ${LIBWEBM_COMPAT_INCLUDE_DIR})
    target_link_libraries(mkvcodec PRIVATE
        VPL::dispatcher unofficial::libwebm::libwebm yuv ${CMAKE_DL_LIBS})
    if(WIN32)
        target_compile_definitions(mkvcodec PRIVATE NOMINMAX)
    endif()
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
if(MKVC_BUILD_PYTHON_DLPACK)
    find_package(Python3 QUIET COMPONENTS Interpreter Development.SABIModule)
    if(TARGET Python3::SABIModule)
        Python3_add_library(mkvc_python_dlpack MODULE USE_SABI 3.9 WITH_SOABI
            python/mkvcodec/_dlpack_module.c)
        target_compile_definitions(mkvc_python_dlpack PRIVATE Py_LIMITED_API=0x03090000)
        set_target_properties(mkvc_python_dlpack PROPERTIES
            OUTPUT_NAME "_dlpack" PREFIX "")
        install(TARGETS mkvc_python_dlpack
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/python/mkvcodec
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/python/mkvcodec)
    endif()
endif()
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
    if(TARGET mkvc_python_dlpack AND Python3_Interpreter_FOUND)
        add_test(NAME mkvc_python_dlpack_capsule
            COMMAND ${CMAKE_COMMAND} -E env
                "PYTHONPATH=$<TARGET_FILE_DIR:mkvc_python_dlpack>"
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_dlpack_capsule.py")
        add_test(NAME mkvc_python_external_gpu_import
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_external_gpu_import.py"
                "$<TARGET_FILE:mkvcodec>"
                "$<TARGET_FILE_DIR:mkvc_python_dlpack>"
                "${CMAKE_CURRENT_SOURCE_DIR}/python")
        if(MKVC_ENABLE_NVIDIA)
            add_test(NAME mkvc_python_nvidia_dlpack
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_nvidia_dlpack.py"
                    "$<TARGET_FILE:mkvcodec>"
                    "$<TARGET_FILE_DIR:mkvc_python_dlpack>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/python")
            set_tests_properties(mkvc_python_nvidia_dlpack PROPERTIES
                SKIP_RETURN_CODE 77)
        endif()
    endif()
    target_compile_definitions(mkvcodec PRIVATE MKVC_ENABLE_TEST_HOOKS=1)
    if(Python3_Interpreter_FOUND)
        add_test(NAME mkvc_gpu_copy_audit_report
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gpu_copy_audit.py")
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
        src/gpu/dlpack_adapter.cpp
        src/gpu/intel/intel_native_handle.cpp
        src/gpu/intel/va_completion.cpp
        src/gpu/intel/d3d11_completion.cpp
        src/gpu/nvidia/nvidia_native_handle.cpp)
    target_compile_features(mkvc_gpu_frame_test PRIVATE cxx_std_17)
    target_compile_definitions(mkvc_gpu_frame_test PRIVATE MKVC_BUILDING_LIBRARY=1)
    target_include_directories(mkvc_gpu_frame_test PRIVATE
        src/gpu src/gpu/intel src/gpu/nvidia include)
    if(MSVC)
        target_compile_options(mkvc_gpu_frame_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(mkvc_gpu_frame_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME mkvc_gpu_frame COMMAND mkvc_gpu_frame_test)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        include(CheckCSourceCompiles)
        check_c_source_compiles("#include <features.h>
            #if !defined(__GLIBC__) || !defined(__x86_64__)
            #error unsupported audit platform
            #endif
            int main(void) { return 0; }" MKVC_HAS_GLIBC_AUDIT)
        if(MKVC_HAS_GLIBC_AUDIT AND Python3_Interpreter_FOUND)
            add_library(mkvc_gpu_copy_audit MODULE tests/gpu_copy_audit.c)
            target_compile_features(mkvc_gpu_copy_audit PRIVATE c_std_11)
            target_compile_options(mkvc_gpu_copy_audit PRIVATE -Wall -Wextra -Werror)
            add_library(mkvc_gpu_copy_audit_mock MODULE tests/gpu_copy_audit_mock.c)
            add_test(NAME mkvc_gpu_copy_audit_selftest
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_gpu_copy_audit.py"
                    --audit "$<TARGET_FILE:mkvc_gpu_copy_audit>"
                    --self-test "$<TARGET_FILE:mkvc_gpu_copy_audit_mock>")
            set_tests_properties(mkvc_gpu_copy_audit_selftest PROPERTIES TIMEOUT 30)
        endif()
    endif()

    if(WIN32)
        add_executable(mkvc_d3d11_fence_test tests/d3d11_fence_test.cpp)
        target_compile_features(mkvc_d3d11_fence_test PRIVATE cxx_std_17)
        target_compile_definitions(mkvc_d3d11_fence_test PRIVATE NOMINMAX)
        target_link_libraries(mkvc_d3d11_fence_test PRIVATE mkvcodec d3d11)
        add_test(NAME mkvc_d3d11_fence COMMAND mkvc_d3d11_fence_test)
        set_tests_properties(mkvc_d3d11_fence PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    endif()

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
        add_executable(mkvc_intel_import_contract_test tests/intel_import_contract_test.cpp)
        target_compile_features(mkvc_intel_import_contract_test PRIVATE cxx_std_17)
        target_include_directories(mkvc_intel_import_contract_test PRIVATE src)
        target_compile_definitions(mkvc_intel_import_contract_test PRIVATE ONEVPL_EXPERIMENTAL=1)
        target_link_libraries(mkvc_intel_import_contract_test PRIVATE VPL::dispatcher)
        add_test(NAME mkvc_intel_import_contract COMMAND mkvc_intel_import_contract_test)

        add_executable(mkvc_intel_vpl_probe_test
            tests/intel_vpl_probe_test.cpp src/intel_vpl_probe.cpp)
        target_compile_features(mkvc_intel_vpl_probe_test PRIVATE cxx_std_17)
        target_include_directories(mkvc_intel_vpl_probe_test PRIVATE src)
        target_compile_definitions(mkvc_intel_vpl_probe_test
            PRIVATE MKVC_HAS_INTEL_ONEVPL=1)
        target_link_libraries(mkvc_intel_vpl_probe_test PRIVATE VPL::dispatcher)
        add_test(NAME mkvc_intel_vpl_probe COMMAND mkvc_intel_vpl_probe_test)
        set_tests_properties(mkvc_intel_vpl_probe PROPERTIES SKIP_RETURN_CODE 77)

        add_executable(mkvc_intel_gpu_surface_test
            tests/intel_gpu_surface_test.cpp)
        target_compile_features(mkvc_intel_gpu_surface_test PRIVATE cxx_std_17)
        target_link_libraries(mkvc_intel_gpu_surface_test PRIVATE mkvcodec)
    endif()

    if(MKVC_ENABLE_NVIDIA)
        add_executable(mkvc_nvidia_probe_test
            tests/nvidia_probe_test.cpp src/nvidia_probe.cpp)
        target_compile_features(mkvc_nvidia_probe_test PRIVATE cxx_std_17)
        target_include_directories(mkvc_nvidia_probe_test PRIVATE
            src ${nv_codec_headers_SOURCE_DIR}/include)
        target_compile_definitions(mkvc_nvidia_probe_test
            PRIVATE MKVC_HAS_NVIDIA=1)
        if(WIN32)
            target_compile_definitions(mkvc_nvidia_probe_test PRIVATE NOMINMAX)
        endif()
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
        add_executable(mkvc_cpp_raii_test tests/cpp_raii_test.cpp)
        target_compile_features(mkvc_cpp_raii_test PRIVATE cxx_std_17)
        target_link_libraries(mkvc_cpp_raii_test PRIVATE mkvcodec)
        add_test(NAME mkvc_cpp_raii
            COMMAND mkvc_cpp_raii_test
                "${CMAKE_CURRENT_BINARY_DIR}/cpp_raii_test.webm")

        add_executable(mkvc_cpu_frame_pool_test tests/cpu_frame_pool_test.cpp)
        target_compile_features(mkvc_cpu_frame_pool_test PRIVATE cxx_std_17)
        target_link_libraries(mkvc_cpu_frame_pool_test PRIVATE mkvcodec)
        add_test(NAME mkvc_cpu_frame_pool
            COMMAND mkvc_cpu_frame_pool_test
                "${CMAKE_CURRENT_BINARY_DIR}/cpu_frame_pool_test.webm")

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
                tests/nvidia_gpu_test_support.cpp
                src/nvidia_webm_decoder.cpp src/nvidia_probe.cpp
                src/gpu/gpu_frame.cpp src/gpu/gpu_frame_pool.cpp
                src/gpu/intel/va_completion.cpp
                src/gpu/intel/d3d11_completion.cpp
                src/gpu/nvidia/cuda_completion.cpp
                src/gpu/nvidia/nvidia_native_handle.cpp)
            target_compile_features(mkvc_nvidia_webm_decoder_test PRIVATE cxx_std_17)
            target_include_directories(mkvc_nvidia_webm_decoder_test PRIVATE
                src include ${nv_codec_headers_SOURCE_DIR}/include
                ${LIBWEBM_COMPAT_INCLUDE_DIR})
            target_compile_definitions(mkvc_nvidia_webm_decoder_test
                PRIVATE MKVC_HAS_NVIDIA=1 MKVC_BUILDING_LIBRARY=1)
            if(WIN32)
                target_compile_definitions(mkvc_nvidia_webm_decoder_test
                    PRIVATE NOMINMAX)
            endif()
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

            add_executable(mkvc_nvidia_gpu_transcode_test
                tests/nvidia_gpu_transcode_test.cpp)
            target_compile_features(mkvc_nvidia_gpu_transcode_test PRIVATE cxx_std_17)
            target_link_libraries(mkvc_nvidia_gpu_transcode_test PRIVATE mkvcodec)
            add_test(NAME mkvc_nvidia_gpu_transcode
                COMMAND mkvc_nvidia_gpu_transcode_test
                    "${MKVC_VP9_TEST_OUTPUT}"
                    "${CMAKE_CURRENT_BINARY_DIR}/nvidia_gpu_transcode.webm")
            set_tests_properties(mkvc_nvidia_gpu_transcode PROPERTIES
                FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77)

            add_executable(mkvc_nvidia_cuda_event_import_test
                tests/nvidia_cuda_event_import_test.cpp)
            target_compile_features(mkvc_nvidia_cuda_event_import_test PRIVATE cxx_std_17)
            target_include_directories(mkvc_nvidia_cuda_event_import_test PRIVATE
                ${nv_codec_headers_SOURCE_DIR}/include)
            target_link_libraries(mkvc_nvidia_cuda_event_import_test PRIVATE
                mkvcodec ${CMAKE_DL_LIBS})
            add_test(NAME mkvc_nvidia_cuda_event_import
                COMMAND mkvc_nvidia_cuda_event_import_test)
            set_tests_properties(mkvc_nvidia_cuda_event_import PROPERTIES
                SKIP_RETURN_CODE 77)
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

    if(MKVC_ENABLE_INTEL_ONEVPL AND MKVC_ENABLE_CPU_VP9)
        add_test(NAME mkvc_intel_gpu_surface
            COMMAND mkvc_intel_gpu_surface_test "${MKVC_VP9_TEST_OUTPUT}"
                "${CMAKE_CURRENT_BINARY_DIR}/intel_external_va_import.webm")
        set_tests_properties(mkvc_intel_gpu_surface PROPERTIES
            FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_test(NAME mkvc_intel_va_surface_sync
                COMMAND mkvc_intel_gpu_surface_test "${MKVC_VP9_TEST_OUTPUT}"
                    "${CMAKE_CURRENT_BINARY_DIR}/intel_native_va_sync.webm" native-va)
            set_tests_properties(mkvc_intel_va_surface_sync PROPERTIES
                FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77 TIMEOUT 30)
            if(TARGET mkvc_python_dlpack AND MKVC_BUILD_PYTHON_TESTS)
                add_test(NAME mkvc_python_intel_va_import
                    COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_intel_va_import.py"
                        "$<TARGET_FILE:mkvcodec>"
                        "$<TARGET_FILE_DIR:mkvc_python_dlpack>"
                        "${CMAKE_CURRENT_SOURCE_DIR}/python" "${MKVC_VP9_TEST_OUTPUT}")
                set_tests_properties(mkvc_python_intel_va_import PROPERTIES
                    FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77 TIMEOUT 30)
                add_test(NAME mkvc_python_intel_opencl_roundtrip
                    COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_intel_opencl_roundtrip.py"
                        "$<TARGET_FILE:mkvcodec>" "$<TARGET_FILE_DIR:mkvc_python_dlpack>"
                        "${CMAKE_CURRENT_SOURCE_DIR}/python" "${MKVC_VP9_TEST_OUTPUT}")
                set_tests_properties(mkvc_python_intel_opencl_roundtrip PROPERTIES
                    FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77 TIMEOUT 60)
                add_test(NAME mkvc_intel_opencl_soak_smoke
                    COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_intel_opencl_roundtrip.py"
                        "$<TARGET_FILE:mkvcodec>" "$<TARGET_FILE_DIR:mkvc_python_dlpack>"
                        "${CMAKE_CURRENT_SOURCE_DIR}/python" "${MKVC_VP9_TEST_OUTPUT}")
                set_tests_properties(mkvc_intel_opencl_soak_smoke PROPERTIES
                    FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77 TIMEOUT 60
                    ENVIRONMENT "MKVC_OPENCL_SOAK_SECONDS=2;MKVC_OPENCL_TEST_FRAMES=32;MKVC_OPENCL_SOAK_REPORT=${CMAKE_CURRENT_BINARY_DIR}/intel_opencl_soak_smoke.json")
                if(TARGET mkvc_gpu_copy_audit)
                    add_test(NAME mkvc_intel_opencl_copy_audit
                        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_gpu_copy_audit.py"
                            --audit "$<TARGET_FILE:mkvc_gpu_copy_audit>"
                            --report "${CMAKE_CURRENT_BINARY_DIR}/intel_opencl_copy_audit.json"
                            -- "${Python3_EXECUTABLE}"
                            "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_intel_opencl_roundtrip.py"
                            "$<TARGET_FILE:mkvcodec>" "$<TARGET_FILE_DIR:mkvc_python_dlpack>"
                            "${CMAKE_CURRENT_SOURCE_DIR}/python" "${MKVC_VP9_TEST_OUTPUT}")
                    set_tests_properties(mkvc_intel_opencl_copy_audit PROPERTIES
                        FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77 TIMEOUT 100)
                endif()
            endif()
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
                    "MKVC_TEST_CPU_VP9=$<BOOL:${MKVC_ENABLE_CPU_VP9}>"
                    "MKVC_TEST_CPU_AV1=$<BOOL:${MKVC_ENABLE_CPU_AV1}>"
                    "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python"
                    "MKVC_LIBRARY_PATH=$<TARGET_FILE:mkvcodec>"
                    "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_intel_roundtrip.py")
            set_tests_properties(mkvc_python_intel_roundtrip
                PROPERTIES SKIP_RETURN_CODE 77)
            if(MKVC_ENABLE_CPU_VP9)
                add_test(NAME mkvc_python_intel_gpu_surface
                    COMMAND ${CMAKE_COMMAND} -E env
                        "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python"
                        "MKVC_LIBRARY_PATH=$<TARGET_FILE:mkvcodec>"
                        "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_intel_gpu_surface.py"
                        "${MKVC_VP9_TEST_OUTPUT}")
                set_tests_properties(mkvc_python_intel_gpu_surface PROPERTIES
                    FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77)
            endif()
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
