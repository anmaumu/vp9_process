# NVIDIA-specific native test targets. Call the fixture-dependent function only
# after the CPU VP9 sample fixture has been registered.

function(mkvc_add_nvidia_foundation_tests)
    add_executable(mkvc_nvidia_probe_test
        tests/nvidia_probe_test.cpp src/nvidia_probe.cpp)
    target_compile_features(mkvc_nvidia_probe_test PRIVATE cxx_std_17)
    target_include_directories(mkvc_nvidia_probe_test PRIVATE
        src ${nv_codec_headers_SOURCE_DIR}/include)
    target_compile_definitions(mkvc_nvidia_probe_test PRIVATE MKVC_HAS_NVIDIA=1)
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
endfunction()

function(mkvc_add_nvidia_vp9_fixture_tests vp9_sample)
    add_executable(mkvc_nvidia_webm_decoder_test
        tests/nvidia_webm_decoder_test.cpp
        tests/nvidia_gpu_test_support.cpp
        src/nvidia_webm_decoder.cpp src/nvidia_probe.cpp
        src/container_format.cpp src/webm_packet_reader.cpp
        src/gpu/gpu_frame.cpp src/gpu/gpu_frame_pool.cpp
        src/gpu/intel/va_completion.cpp
        src/gpu/intel/d3d11_completion.cpp
        src/gpu/intel/level_zero_completion.cpp
        src/gpu/nvidia/cuda_completion.cpp
        src/gpu/nvidia/dynamic_library.cpp
        src/gpu/nvidia/nvdec_api.cpp
        src/gpu/nvidia/nvdec_cpu_output.cpp
        src/gpu/nvidia/nvdec_gpu_output.cpp
        src/gpu/nvidia/nvdec_runtime_setup.cpp
        src/gpu/nvidia/nvdec_sequence.cpp
        src/gpu/nvidia/nvidia_native_handle.cpp)
    target_compile_features(mkvc_nvidia_webm_decoder_test PRIVATE cxx_std_17)
    target_include_directories(mkvc_nvidia_webm_decoder_test PRIVATE
        src include ${nv_codec_headers_SOURCE_DIR}/include
        ${LIBWEBM_COMPAT_INCLUDE_DIR})
    target_compile_definitions(mkvc_nvidia_webm_decoder_test
        PRIVATE MKVC_HAS_NVIDIA=1 MKVC_BUILDING_LIBRARY=1)
    if(WIN32)
        target_compile_definitions(mkvc_nvidia_webm_decoder_test PRIVATE NOMINMAX)
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
        COMMAND mkvc_nvidia_webm_decoder_test "${vp9_sample}")
    set_tests_properties(mkvc_nvidia_webm_decode PROPERTIES
        FIXTURES_REQUIRED cpu_vp9_sample SKIP_RETURN_CODE 77)

    add_executable(mkvc_nvidia_gpu_transcode_test
        tests/nvidia_gpu_transcode_test.cpp)
    target_compile_features(mkvc_nvidia_gpu_transcode_test PRIVATE cxx_std_17)
    target_link_libraries(mkvc_nvidia_gpu_transcode_test PRIVATE mkvcodec)
    add_test(NAME mkvc_nvidia_gpu_transcode
        COMMAND mkvc_nvidia_gpu_transcode_test "${vp9_sample}"
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
endfunction()
