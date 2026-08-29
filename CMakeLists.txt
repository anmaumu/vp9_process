cmake_minimum_required(VERSION 3.24)

project(vp9_process VERSION 0.1.0 LANGUAGES C CXX)

option(MKVC_BUILD_TESTS "Build native tests" ON)

add_library(mkvcodec SHARED
    src/c_api.cpp
    src/backend_registry.cpp
)

target_compile_features(mkvcodec PRIVATE cxx_std_17)
target_include_directories(mkvcodec
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_definitions(mkvcodec PRIVATE MKVC_BUILDING_LIBRARY)

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

if(MKVC_BUILD_TESTS)
    enable_testing()
    add_executable(mkvc_c_api_tests tests/c_api_tests.cpp)
    target_compile_features(mkvc_c_api_tests PRIVATE cxx_std_17)
    target_link_libraries(mkvc_c_api_tests PRIVATE mkvcodec)
    add_test(NAME mkvc_c_api_tests COMMAND mkvc_c_api_tests)
endif()

