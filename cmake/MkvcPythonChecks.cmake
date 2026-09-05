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

if(Python3_Interpreter_FOUND)
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
endif()
