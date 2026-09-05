# Repository-level Python checks that do not depend on a codec test fixture.

function(mkvc_add_python_script_test test_name script_path)
    add_test(NAME "${test_name}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/${script_path}" ${ARGN})
endfunction()

function(mkvc_add_python_unittest test_name)
    add_test(NAME "${test_name}"
        COMMAND "${Python3_EXECUTABLE}" -m unittest ${ARGN})
    set_tests_properties("${test_name}" PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()

function(mkvc_add_python_binding_tests)
    if(NOT TARGET mkvc_python_dlpack OR NOT Python3_Interpreter_FOUND)
        return()
    endif()
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
endfunction()

function(mkvc_add_python_integration_tests vp9_sample)
    if(NOT MKVC_BUILD_PYTHON_TESTS)
        return()
    endif()
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
        set_tests_properties(mkvc_python_intel_roundtrip PROPERTIES
            SKIP_RETURN_CODE 77)
        if(MKVC_ENABLE_CPU_VP9)
            add_test(NAME mkvc_python_intel_gpu_surface
                COMMAND ${CMAKE_COMMAND} -E env
                    "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python"
                    "MKVC_LIBRARY_PATH=$<TARGET_FILE:mkvcodec>"
                    "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_intel_gpu_surface.py"
                    "${vp9_sample}")
            set_tests_properties(mkvc_python_intel_gpu_surface PROPERTIES
                FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77)
        endif()
    endif()
endfunction()

function(mkvc_add_repository_python_checks)
    if(NOT Python3_Interpreter_FOUND)
        return()
    endif()
    mkvc_add_python_unittest(mkvc_abi_guard tests/test_abi_guard.py)
    mkvc_add_python_unittest(mkvc_binding_guard tests/test_binding_guard.py)
    mkvc_add_python_unittest(
        mkvc_generate_bindings tests/test_generate_bindings.py)
    mkvc_add_python_script_test(
        mkvc_gpu_copy_audit_report tests/test_gpu_copy_audit.py)
    mkvc_add_python_script_test(
        mkvc_gpu_resource_monitor tests/test_gpu_resource_monitor.py)
    mkvc_add_python_script_test(mkvc_usm_soak_report tests/test_usm_soak_report.py)
    mkvc_add_python_script_test(mkvc_intel_prime_layout tests/test_intel_prime_layout.py)
    mkvc_add_python_script_test(
        mkvc_intel_kernel_trace_report tests/test_intel_kernel_trace.py)
    mkvc_add_python_script_test(mkvc_media_oracle tests/test_media_oracle.py)
    mkvc_add_python_script_test(
        mkvc_intel_userspace_trace_report tests/test_intel_userspace_trace.py)
    mkvc_add_python_script_test(
        mkvc_opencl_reuse_session tests/test_opencl_reuse_session.py)
    mkvc_add_python_script_test(
        mkvc_compliance_source tools/compliance_gate.py source)
    mkvc_add_python_unittest(
        mkvc_compliance_unit tests/test_compliance_gate.py
        tests/test_collect_licenses.py tests/test_build_wheel.py
        tests/test_build_nuget.py)
endfunction()
