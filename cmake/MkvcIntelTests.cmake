# Intel oneVPL-specific native and GPU integration tests. Fixture-dependent
# tests are registered only after the shared CPU VP9 sample exists.

function(mkvc_add_intel_foundation_tests)
    add_executable(mkvc_intel_vpl_bitstream_test
        tests/intel_vpl_bitstream_test.cpp
        src/gpu/intel/vpl_bitstream.cpp)
    target_compile_features(mkvc_intel_vpl_bitstream_test PRIVATE cxx_std_17)
    target_include_directories(mkvc_intel_vpl_bitstream_test PRIVATE src include)
    target_link_libraries(mkvc_intel_vpl_bitstream_test PRIVATE VPL::dispatcher)
    if(MSVC)
        target_compile_options(mkvc_intel_vpl_bitstream_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(mkvc_intel_vpl_bitstream_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME mkvc_intel_vpl_bitstream COMMAND mkvc_intel_vpl_bitstream_test)

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
endfunction()

function(mkvc_add_intel_codec_integration_test)
    add_executable(mkvc_intel_vpl_encode_test
        tests/intel_vpl_encode_test.cpp src/intel_vpl_encoder.cpp
        src/intel_vpl_decoder.cpp src/gpu/intel/vpl_bitstream.cpp
        src/gpu/intel/vpl_decoder_queue.cpp
        src/gpu/intel/vpl_encoder_runtime.cpp
        src/gpu/intel/vpl_encoder_queue.cpp
        src/gpu/intel/vpl_surface_import.cpp)
    target_compile_features(mkvc_intel_vpl_encode_test PRIVATE cxx_std_17)
    target_include_directories(mkvc_intel_vpl_encode_test PRIVATE src include)
    target_compile_definitions(mkvc_intel_vpl_encode_test
        PRIVATE MKVC_HAS_INTEL_ONEVPL=1 MKVC_ENABLE_TEST_HOOKS=1)
    target_link_libraries(mkvc_intel_vpl_encode_test PRIVATE
        VPL::dispatcher unofficial::libvpx::libvpx AOM::aom yuv)
    add_test(NAME mkvc_intel_vpl_encode COMMAND mkvc_intel_vpl_encode_test)
    set_tests_properties(mkvc_intel_vpl_encode PROPERTIES SKIP_RETURN_CODE 77)
endfunction()

function(mkvc_add_intel_vp9_fixture_tests vp9_sample)
    add_test(NAME mkvc_intel_gpu_surface
        COMMAND mkvc_intel_gpu_surface_test "${vp9_sample}"
            "${CMAKE_CURRENT_BINARY_DIR}/intel_external_va_import.webm")
    set_tests_properties(mkvc_intel_gpu_surface PROPERTIES
        FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77)

    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        return()
    endif()

    add_test(NAME mkvc_intel_va_surface_sync
        COMMAND mkvc_intel_gpu_surface_test "${vp9_sample}"
            "${CMAKE_CURRENT_BINARY_DIR}/intel_native_va_sync.webm" native-va)
    set_tests_properties(mkvc_intel_va_surface_sync PROPERTIES
        FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77 TIMEOUT 30)

    if(NOT TARGET mkvc_python_dlpack OR NOT MKVC_BUILD_PYTHON_TESTS)
        return()
    endif()

    add_test(NAME mkvc_python_intel_va_import
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_intel_va_import.py"
            "$<TARGET_FILE:mkvcodec>"
            "$<TARGET_FILE_DIR:mkvc_python_dlpack>"
            "${CMAKE_CURRENT_SOURCE_DIR}/python" "${vp9_sample}")
    set_tests_properties(mkvc_python_intel_va_import PROPERTIES
        FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77 TIMEOUT 30)

    add_test(NAME mkvc_python_intel_opencl_roundtrip
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_intel_opencl_roundtrip.py"
            "$<TARGET_FILE:mkvcodec>" "$<TARGET_FILE_DIR:mkvc_python_dlpack>"
            "${CMAKE_CURRENT_SOURCE_DIR}/python" "${vp9_sample}")
    set_tests_properties(mkvc_python_intel_opencl_roundtrip PROPERTIES
        FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77 TIMEOUT 60)

    if(FFMPEG_EXECUTABLE AND FFPROBE_EXECUTABLE)
        set(intel_av1_source "${CMAKE_CURRENT_BINARY_DIR}/intel_av1_source.webm")
        add_test(NAME mkvc_intel_av1_source
            COMMAND "${FFMPEG_EXECUTABLE}" -v error -f lavfi
                -i testsrc2=size=128x128:rate=30 -frames:v 2 -c:v libvpx-vp9
                -y "${intel_av1_source}")
        set_tests_properties(mkvc_intel_av1_source PROPERTIES
            FIXTURES_SETUP intel_av1_source TIMEOUT 30)

        foreach(mode IN ITEMS default reuse recreate)
            if(mode STREQUAL "default")
                set(test_name mkvc_intel_opencl_av1_roundtrip)
                set(test_environment
                    "MKVC_OPENCL_OUTPUT_CODEC=av1;MKVC_OPENCL_TEST_FRAMES=32")
            elseif(mode STREQUAL "reuse")
                set(test_name mkvc_intel_opencl_reuse_av1_roundtrip)
                set(test_environment
                    "MKVC_OPENCL_OUTPUT_CODEC=av1;MKVC_OPENCL_TEST_FRAMES=32;MKVC_OPENCL_REUSE_PROGRAM=1")
            else()
                set(test_name mkvc_intel_opencl_recreate_av1_roundtrip)
                set(test_environment
                    "MKVC_OPENCL_OUTPUT_CODEC=av1;MKVC_OPENCL_TEST_FRAMES=32;MKVC_OPENCL_REUSE_PROGRAM=0")
            endif()
            add_test(NAME ${test_name}
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_intel_opencl_roundtrip.py"
                    "$<TARGET_FILE:mkvcodec>" "$<TARGET_FILE_DIR:mkvc_python_dlpack>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/python" "${intel_av1_source}")
            set_tests_properties(${test_name} PROPERTIES
                FIXTURES_REQUIRED intel_av1_source SKIP_RETURN_CODE 77 TIMEOUT 120
                ENVIRONMENT "${test_environment}")
        endforeach()
    endif()

    add_test(NAME mkvc_intel_opencl_soak_smoke
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_intel_opencl_roundtrip.py"
            "$<TARGET_FILE:mkvcodec>" "$<TARGET_FILE_DIR:mkvc_python_dlpack>"
            "${CMAKE_CURRENT_SOURCE_DIR}/python" "${vp9_sample}")
    set_tests_properties(mkvc_intel_opencl_soak_smoke PROPERTIES
        FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77 TIMEOUT 60
        ENVIRONMENT "MKVC_OPENCL_SOAK_SECONDS=2;MKVC_OPENCL_TEST_FRAMES=32;MKVC_OPENCL_SOAK_REPORT=${CMAKE_CURRENT_BINARY_DIR}/intel_opencl_soak_smoke.json")

    if(TARGET mkvc_gpu_copy_audit)
        add_test(NAME mkvc_intel_opencl_copy_audit
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_gpu_copy_audit.py"
                --audit "$<TARGET_FILE:mkvc_gpu_copy_audit>"
                --report "${CMAKE_CURRENT_BINARY_DIR}/intel_opencl_copy_audit.json"
                -- "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_intel_opencl_roundtrip.py"
                "$<TARGET_FILE:mkvcodec>" "$<TARGET_FILE_DIR:mkvc_python_dlpack>"
                "${CMAKE_CURRENT_SOURCE_DIR}/python" "${vp9_sample}")
        set_tests_properties(mkvc_intel_opencl_copy_audit PROPERTIES
            FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77 TIMEOUT 100)
    endif()
endfunction()
