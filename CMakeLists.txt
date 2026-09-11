cmake_minimum_required(VERSION 3.26)

project(vp9_process VERSION 0.1.0 LANGUAGES C CXX)

option(MKVC_BUILD_TESTS "Build native tests" ON)
option(MKVC_BUILD_PYTHON_TESTS "Build Python binding tests (requires NumPy)" OFF)
option(MKVC_BUILD_DOTNET_TESTS "Build and run the .NET P/Invoke smoke test" OFF)
option(MKVC_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)
option(MKVC_BUILD_PYTHON_DLPACK "Build the CPython stable-ABI DLPack capsule bridge" ON)
option(MKVC_ENABLE_CPU_VP9 "Enable the libvpx CPU VP9 backend" ON)
option(MKVC_ENABLE_CPU_AV1 "Enable SVT-AV1 encode and libaom decode" ON)
option(MKVC_ENABLE_INTEL_ONEVPL "Enable Intel oneVPL backend foundation" ON)
option(MKVC_ENABLE_NVIDIA "Enable NVIDIA NVDEC/NVENC backend foundation" OFF)

if(MKVC_ENABLE_CPU_VP9 OR MKVC_ENABLE_CPU_AV1 OR
   MKVC_ENABLE_INTEL_ONEVPL OR MKVC_ENABLE_NVIDIA)
    set(MKVC_HAS_CODEC_BACKEND ON)
else()
    set(MKVC_HAS_CODEC_BACKEND OFF)
endif()

if(MKVC_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MKVC_BUILD_FUZZERS requires Clang")
    endif()
    add_executable(mkvc_fuzz_container_header
        tests/fuzz_container_header.cpp src/container_ebml.cpp)
    target_compile_features(mkvc_fuzz_container_header PRIVATE cxx_std_17)
    target_include_directories(mkvc_fuzz_container_header PRIVATE src)
    target_compile_definitions(mkvc_fuzz_container_header PRIVATE NOMINMAX)
    target_compile_options(mkvc_fuzz_container_header PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(mkvc_fuzz_container_header PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
endif()

include(cmake/MkvcDependencies.cmake)
include(cmake/MkvcLibrary.cmake)

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
    include(cmake/MkvcPythonChecks.cmake)
    include(cmake/MkvcCoreTests.cmake)
    if(MKVC_ENABLE_NVIDIA)
        include(cmake/MkvcNvidiaTests.cmake)
    endif()
    if(MKVC_ENABLE_INTEL_ONEVPL)
        include(cmake/MkvcIntelTests.cmake)
    endif()
    mkvc_add_python_binding_tests()
    target_compile_definitions(mkvcodec PRIVATE MKVC_ENABLE_TEST_HOOKS=1)
    mkvc_add_repository_python_checks()
    mkvc_add_core_tests()

    if(MKVC_ENABLE_INTEL_ONEVPL)
        mkvc_add_intel_foundation_tests()
    endif()

    if(MKVC_ENABLE_NVIDIA)
        mkvc_add_nvidia_foundation_tests()
    endif()

    if(MKVC_ENABLE_INTEL_ONEVPL AND MKVC_ENABLE_CPU_VP9 AND MKVC_ENABLE_CPU_AV1)
        mkvc_add_intel_codec_integration_test()
    endif()

    mkvc_add_cpu_vp9_fixture_tests(MKVC_VP9_TEST_OUTPUT)

    if(MKVC_ENABLE_INTEL_ONEVPL AND MKVC_ENABLE_CPU_VP9)
        mkvc_add_intel_vp9_fixture_tests("${MKVC_VP9_TEST_OUTPUT}")
    endif()

    mkvc_add_python_integration_tests("${MKVC_VP9_TEST_OUTPUT}")

    mkvc_add_dotnet_tests("${MKVC_VP9_TEST_OUTPUT}")
endif()
