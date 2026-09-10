#[[
Define the mkvcodec shared library and attach each optional codec backend.

Dependency discovery is intentionally kept in MkvcDependencies.cmake so this
module describes only source ownership, compile contracts, and link topology.
]]

add_library(mkvcodec SHARED
    src/c_api.cpp
    src/c_api_cpu.cpp
    src/c_api_decoder.cpp
    src/decoder/decoder_c_api_support.cpp
    src/c_api_encoder.cpp
    src/c_api_frame.cpp
    src/backend_registry.cpp
    src/container_format.cpp
    src/container_ebml.cpp
    src/decoder/decoder_pipeline.cpp
    src/cpu_vp9_encoder.cpp
    src/cpu_vp9_encoder_runtime.cpp
    src/cpu_vp9_decoder.cpp
    src/cpu_frame_pool.cpp
    src/cpu_conversion_workers.cpp
    src/frame_conversion.cpp
    src/frame_processor.cpp
    src/frame_processor_i420.cpp
    src/gpu/gpu_external_import.cpp
    src/gpu/gpu_external_import_intel.cpp
    src/gpu/gpu_external_import_nvidia.cpp
    src/gpu/gpu_frame.cpp
    src/gpu/gpu_frame_c_api.cpp
    src/gpu/gpu_frame_pool.cpp
    src/gpu/gpu_resource_pool.cpp
    src/gpu/gpu_resource_pool_c_api.cpp
    src/gpu/dlpack_adapter.cpp
    src/gpu/intel/intel_native_handle.cpp
    src/gpu/intel/va_completion.cpp
    src/gpu/intel/d3d11_completion.cpp
    src/gpu/intel/level_zero_completion.cpp
    src/gpu/nvidia/nvidia_native_handle.cpp
    src/encoder/cpu_frame_copy.cpp
    src/encoder/cpu_submission.cpp
    src/encoder/encoder_backend_execution.cpp
    src/encoder/encoder_backend_factory.cpp
    src/encoder/encoder_c_api_support.cpp
    src/encoder/encoder_queue_control.cpp
    src/encoder/encoder_worker.cpp
    src/encoder_session.cpp
    src/cpu_av1_encoder.cpp
    src/cpu_av1_encoder_runtime.cpp
    src/cpu_av1_decoder.cpp
    src/intel_vpl_probe.cpp
    src/intel_vpl_encoder.cpp
    src/intel_vpl_decoder.cpp
    src/intel_webm_encoder.cpp
    src/intel_webm_decoder.cpp
    src/nvidia_probe.cpp
    src/nvidia_webm_decoder.cpp
    src/nvidia_webm_encoder.cpp)

if(MKVC_USE_HIGHWAY_PACKER)
    target_sources(mkvcodec PRIVATE src/packed_pixel_conversion.cpp)
    target_compile_definitions(mkvcodec PRIVATE MKVC_USE_HIGHWAY_PACKER=1)
    target_link_libraries(mkvcodec PRIVATE hwy::hwy)
endif()

if(MKVC_HAS_CODEC_BACKEND)
    target_sources(mkvcodec PRIVATE
        src/encoder/cpu_frame_to_i420.cpp
        src/encoder/cpu_frame_to_nv12.cpp
        src/encoder/frame_timing.cpp
        src/webm_muxer.cpp
        src/webm_packet_reader.cpp)
endif()

target_compile_features(mkvcodec PRIVATE cxx_std_17)
target_include_directories(mkvcodec
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(mkvcodec PRIVATE MKVC_BUILDING_LIBRARY)
target_link_libraries(mkvcodec PRIVATE ${CMAKE_DL_LIBS})

if(MKVC_ENABLE_NVIDIA)
    target_sources(mkvcodec PRIVATE
        src/gpu/nvidia/cuda_completion.cpp
        src/gpu/nvidia/dynamic_library.cpp
        src/gpu/nvidia/nvdec_api.cpp
        src/gpu/nvidia/nvdec_callbacks.cpp
        src/gpu/nvidia/nvdec_cpu_output.cpp
        src/gpu/nvidia/nvdec_gpu_output.cpp
        src/gpu/nvidia/nvdec_packet_pump.cpp
        src/gpu/nvidia/nvdec_runtime_cleanup.cpp
        src/gpu/nvidia/nvdec_runtime_owner.cpp
        src/gpu/nvidia/nvdec_runtime_setup.cpp
        src/gpu/nvidia/nvdec_sequence.cpp
        src/gpu/nvidia/nvenc_api.cpp
        src/gpu/nvidia/nvenc_cpu_conversion.cpp
        src/gpu/nvidia/nvenc_cpu_submission.cpp
        src/gpu/nvidia/nvenc_encoder_submission.cpp
        src/gpu/nvidia/nvenc_gpu_frame_validation.cpp
        src/gpu/nvidia/nvenc_gpu_submission.cpp
        src/gpu/nvidia/nvenc_packet_io.cpp
        src/gpu/nvidia/nvenc_session.cpp)
    target_compile_definitions(mkvcodec PRIVATE MKVC_HAS_NVIDIA=1)
    target_include_directories(mkvcodec PRIVATE
        ${nv_codec_headers_SOURCE_DIR}/include
        ${LIBWEBM_COMPAT_INCLUDE_DIR})
    target_link_libraries(mkvcodec PRIVATE
        unofficial::libwebm::libwebm yuv ${CMAKE_DL_LIBS})
    if(WIN32)
        target_compile_definitions(mkvcodec PRIVATE NOMINMAX)
    endif()
endif()

if(MKVC_ENABLE_CPU_VP9)
    target_compile_definitions(mkvcodec PRIVATE MKVC_HAS_CPU_VP9=1)
    # libwebm's public headers include common/... relative to include/webm.
    target_include_directories(mkvcodec PRIVATE ${LIBWEBM_COMPAT_INCLUDE_DIR})
    target_link_libraries(mkvcodec PRIVATE
        unofficial::libvpx::libvpx unofficial::libwebm::libwebm yuv)
endif()

if(MKVC_ENABLE_INTEL_ONEVPL)
    target_sources(mkvcodec PRIVATE
        src/gpu/intel/intel_completion.cpp
        src/gpu/intel/intel_surface_factory.cpp
        src/gpu/intel/vpl_bitstream.cpp
        src/gpu/intel/vpl_cpu_input.cpp
        src/gpu/intel/vpl_decoder_cpu_output.cpp
        src/gpu/intel/vpl_decoder_gpu_output.cpp
        src/gpu/intel/vpl_decoder_pump.cpp
        src/gpu/intel/vpl_decoder_queue.cpp
        src/gpu/intel/vpl_decoder_runtime.cpp
        src/gpu/intel/vpl_encoder_sequence.cpp
        src/gpu/intel/vpl_encoder_runtime.cpp
        src/gpu/intel/vpl_encoder_queue.cpp
        src/gpu/intel/vpl_gpu_submission.cpp
        src/gpu/intel/vpl_imported_surface_tracker.cpp
        src/gpu/intel/vpl_packet_muxer.cpp
        src/gpu/intel/vpl_surface_import.cpp)
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
        AOM::aom PkgConfig::SVT_AV1 unofficial::libwebm::libwebm yuv)
endif()

if(MSVC)
    target_compile_options(mkvcodec PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(mkvcodec PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
