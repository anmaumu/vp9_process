#[[
Define backend-neutral native tests, CPU VP9 fixtures, and .NET smoke tests.

Hardware-specific suites remain in MkvcIntelTests.cmake and
MkvcNvidiaTests.cmake. Repository-level Python checks remain in
MkvcPythonChecks.cmake.
]]

function(mkvc_add_core_tests)
    add_executable(mkvc_c_api_tests tests/c_api_tests.cpp)
    target_compile_features(mkvc_c_api_tests PRIVATE cxx_std_17)
    target_link_libraries(mkvc_c_api_tests PRIVATE mkvcodec)
    add_test(NAME mkvc_c_api_tests COMMAND mkvc_c_api_tests)

    add_executable(mkvc_dynamic_library_test
        tests/dynamic_library_test.cpp src/gpu/nvidia/dynamic_library.cpp)
    target_compile_features(mkvc_dynamic_library_test PRIVATE cxx_std_17)
    target_include_directories(mkvc_dynamic_library_test PRIVATE src)
    target_link_libraries(mkvc_dynamic_library_test PRIVATE ${CMAKE_DL_LIBS})
    if(MSVC)
        target_compile_options(mkvc_dynamic_library_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(mkvc_dynamic_library_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME mkvc_dynamic_library COMMAND mkvc_dynamic_library_test)

    add_executable(mkvc_gpu_frame_test
        tests/gpu_frame_test.cpp src/gpu/gpu_external_import.cpp
        src/gpu/gpu_external_import_intel.cpp src/gpu/gpu_external_import_nvidia.cpp
        src/gpu/gpu_frame.cpp src/gpu/gpu_frame_c_api.cpp
        src/gpu/gpu_frame_pool.cpp
        src/gpu/dlpack_adapter.cpp
        src/gpu/intel/intel_native_handle.cpp
        src/gpu/intel/va_completion.cpp
        src/gpu/intel/d3d11_completion.cpp
        src/gpu/intel/level_zero_completion.cpp
        src/gpu/nvidia/nvenc_gpu_frame_validation.cpp
        src/gpu/nvidia/nvidia_native_handle.cpp)
    target_compile_features(mkvc_gpu_frame_test PRIVATE cxx_std_17)
    target_compile_definitions(mkvc_gpu_frame_test PRIVATE MKVC_BUILDING_LIBRARY=1)
    target_include_directories(mkvc_gpu_frame_test PRIVATE
        src src/gpu src/gpu/intel src/gpu/nvidia include)
    target_link_libraries(mkvc_gpu_frame_test PRIVATE ${CMAKE_DL_LIBS})
    if(MSVC)
        target_compile_options(mkvc_gpu_frame_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(mkvc_gpu_frame_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME mkvc_gpu_frame COMMAND mkvc_gpu_frame_test)

    add_executable(mkvc_gpu_resource_pool_test tests/gpu_resource_pool_test.cpp)
    target_link_libraries(mkvc_gpu_resource_pool_test PRIVATE mkvcodec)
    add_test(NAME mkvc_gpu_resource_pool COMMAND mkvc_gpu_resource_pool_test)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        include(CheckCSourceCompiles)
        check_c_source_compiles("#include <features.h>
            #if !defined(__GLIBC__) || !defined(__x86_64__)
            #error unsupported audit platform
            #endif
            int main(void) { return 0;}" MKVC_HAS_GLIBC_AUDIT)
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

    if(MKVC_HAS_CODEC_BACKEND)
        add_executable(mkvc_frame_processor_test
            tests/frame_processor_test.cpp src/frame_processor.cpp)
        target_compile_features(mkvc_frame_processor_test PRIVATE cxx_std_17)
        target_compile_definitions(mkvc_frame_processor_test PRIVATE MKVC_HAS_CPU_VP9=1)
        target_sources(mkvc_frame_processor_test PRIVATE src/frame_processor_i420.cpp)
        target_include_directories(mkvc_frame_processor_test PRIVATE src include)
        target_link_libraries(mkvc_frame_processor_test PRIVATE yuv)
        add_test(NAME mkvc_frame_processor COMMAND mkvc_frame_processor_test)

        add_executable(mkvc_frame_conversion_test
            tests/frame_conversion_test.cpp src/cpu_conversion_workers.cpp
            src/frame_conversion.cpp)
        target_compile_features(mkvc_frame_conversion_test PRIVATE cxx_std_17)
        target_compile_definitions(mkvc_frame_conversion_test PRIVATE MKVC_HAS_CPU_VP9=1)
        target_include_directories(mkvc_frame_conversion_test PRIVATE src include)
        target_link_libraries(mkvc_frame_conversion_test PRIVATE yuv)
        if(MKVC_USE_HIGHWAY_PACKER)
            target_sources(mkvc_frame_conversion_test PRIVATE src/packed_pixel_conversion.cpp)
            target_compile_definitions(mkvc_frame_conversion_test
                PRIVATE MKVC_USE_HIGHWAY_PACKER=1)
            target_link_libraries(mkvc_frame_conversion_test PRIVATE hwy::hwy)
        endif()
        if(MSVC)
            target_compile_options(mkvc_frame_conversion_test PRIVATE
                /W4 /permissive- /EHsc /UNDEBUG)
        else()
            target_compile_options(mkvc_frame_conversion_test PRIVATE
                -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
        endif()
        add_test(NAME mkvc_frame_conversion COMMAND mkvc_frame_conversion_test)
    endif()

    if(MKVC_ENABLE_CPU_VP9 OR MKVC_ENABLE_CPU_AV1)
        add_executable(mkvc_cpu_frame_to_i420_test
            tests/cpu_frame_to_i420_test.cpp
            src/encoder/cpu_frame_to_i420.cpp
            src/encoder/frame_timing.cpp)
        target_compile_features(mkvc_cpu_frame_to_i420_test PRIVATE cxx_std_17)
        target_include_directories(mkvc_cpu_frame_to_i420_test PRIVATE src include)
        target_link_libraries(mkvc_cpu_frame_to_i420_test PRIVATE yuv)
        if(MSVC)
            target_compile_options(mkvc_cpu_frame_to_i420_test PRIVATE /W4 /permissive- /EHsc)
        else()
            target_compile_options(mkvc_cpu_frame_to_i420_test PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME mkvc_cpu_frame_to_i420 COMMAND mkvc_cpu_frame_to_i420_test)
    endif()

    if(MKVC_ENABLE_CPU_VP9)
        add_executable(mkvc_async_failure_test tests/async_failure_test.cpp)
        target_compile_features(mkvc_async_failure_test PRIVATE cxx_std_17)
        target_link_libraries(mkvc_async_failure_test PRIVATE mkvcodec)
        add_test(NAME mkvc_async_failure COMMAND mkvc_async_failure_test)
        set_tests_properties(mkvc_async_failure PROPERTIES TIMEOUT 15)
    endif()
endfunction()

function(mkvc_add_cpu_vp9_fixture_tests output_variable)
    set(output "")
    if(MKVC_ENABLE_CPU_VP9)
        add_executable(mkvc_cpp_raii_test tests/cpp_raii_test.cpp)
        target_compile_features(mkvc_cpp_raii_test PRIVATE cxx_std_17)
        target_link_libraries(mkvc_cpp_raii_test PRIVATE mkvcodec)
        add_test(NAME mkvc_cpp_raii
            COMMAND mkvc_cpp_raii_test "${CMAKE_CURRENT_BINARY_DIR}/cpp_raii_test.webm")

        add_executable(mkvc_cpu_frame_pool_test tests/cpu_frame_pool_test.cpp)
        target_compile_features(mkvc_cpu_frame_pool_test PRIVATE cxx_std_17)
        target_link_libraries(mkvc_cpu_frame_pool_test PRIVATE mkvcodec)
        add_test(NAME mkvc_cpu_frame_pool
            COMMAND mkvc_cpu_frame_pool_test
                "${CMAKE_CURRENT_BINARY_DIR}/cpu_frame_pool_test.webm")

        add_executable(mkvc_cpu_vp9_encode_test tests/cpu_vp9_encode_test.cpp)
        target_compile_features(mkvc_cpu_vp9_encode_test PRIVATE cxx_std_17)
        target_link_libraries(mkvc_cpu_vp9_encode_test PRIVATE mkvcodec)
        set(output "${CMAKE_CURRENT_BINARY_DIR}/cpu_vp9_test.webm")
        add_test(NAME mkvc_cpu_vp9_encode COMMAND mkvc_cpu_vp9_encode_test "${output}")
        set_tests_properties(mkvc_cpu_vp9_encode PROPERTIES FIXTURES_SETUP cpu_vp9_sample)

        if(MKVC_ENABLE_NVIDIA)
            mkvc_add_nvidia_vp9_fixture_tests("${output}")
        endif()

        find_program(FFMPEG_EXECUTABLE ffmpeg)
        if(FFMPEG_EXECUTABLE)
            add_test(NAME mkvc_cpu_vp9_external_decode
                COMMAND "${FFMPEG_EXECUTABLE}" -v error -i "${output}" -f null -)
            set_tests_properties(mkvc_cpu_vp9_external_decode PROPERTIES
                FIXTURES_REQUIRED cpu_vp9_sample)
        endif()

        find_program(FFPROBE_EXECUTABLE ffprobe)
        if(FFPROBE_EXECUTABLE)
            add_test(NAME mkvc_cpu_vp9_metadata
                COMMAND ${CMAKE_COMMAND}
                    -DFFPROBE_EXECUTABLE=${FFPROBE_EXECUTABLE}
                    -DINPUT_FILE=${output}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_vp9_webm.cmake)
            set_tests_properties(mkvc_cpu_vp9_metadata PROPERTIES
                FIXTURES_REQUIRED cpu_vp9_sample)
        endif()
    endif()
    set(${output_variable} "${output}" PARENT_SCOPE)
endfunction()

function(mkvc_add_dotnet_tests)
    if(NOT MKVC_BUILD_DOTNET_TESTS)
        return()
    endif()
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
    set_tests_properties(mkvc_dotnet_smoke PROPERTIES DEPENDS mkvc_dotnet_build)
endfunction()
