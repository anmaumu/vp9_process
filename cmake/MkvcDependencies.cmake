#[[
Resolve build dependencies selected by the public MKVC_ENABLE_* options.

This module only discovers third-party targets and records compiler-specific
implementation choices. Target sources and link relationships remain in the
library module.
]]

if(MKVC_ENABLE_NVIDIA)
    include(FetchContent)
    FetchContent_Declare(nv_codec_headers
        URL https://github.com/FFmpeg/nv-codec-headers/archive/refs/tags/n13.1.15.0.tar.gz
        URL_HASH SHA256=2255bc74d038b95aa4be30f5f66322c2176acbdb90ada1851db6993536fbeaf7
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(nv_codec_headers)
endif()

if(MKVC_HAS_CODEC_BACKEND)
    find_package(unofficial-libwebm CONFIG REQUIRED)
    find_path(LIBWEBM_COMPAT_INCLUDE_DIR mkvmuxer/mkvmuxer.h
        PATH_SUFFIXES webm REQUIRED)
    find_package(libyuv CONFIG REQUIRED)
endif()

# libyuv's direct 24-bit conversion rows are scalar in classic MSVC x64 builds.
# Use its SIMD-enabled 32-bit conversion followed by Highway's runtime-dispatched
# alpha removal on that compiler only. Other toolchains retain the one-pass path.
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND
   MKVC_HAS_CODEC_BACKEND)
    find_package(hwy CONFIG REQUIRED)
    set(MKVC_USE_HIGHWAY_PACKER ON)
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
