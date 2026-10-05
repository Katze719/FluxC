cmake_minimum_required(VERSION 3.16)

# Enable the compilation database by default, including previously empty caches.
# An explicit -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF is still respected.
if(NOT DEFINED CMAKE_EXPORT_COMPILE_COMMANDS OR CMAKE_EXPORT_COMPILE_COMMANDS STREQUAL "")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export commands for clang tooling" FORCE)
endif()

project(FluxC VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(FLUX_BUILD_TESTS "Build tests" ON)
option(FLUX_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(FLUX_BUILD_EXAMPLES "Build examples" OFF)
option(FLUX_ENABLE_POSIX_TIME "Enable POSIX monotonic clock support" ${UNIX})
option(FLUX_ENABLE_RTSAN "Instrument hot paths with Clang RealtimeSanitizer" OFF)

if(FLUX_ENABLE_RTSAN)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
        message(FATAL_ERROR "FLUX_ENABLE_RTSAN requires Clang; configure a separate build with -DCMAKE_C_COMPILER=clang and -DCMAKE_CXX_COMPILER=clang++")
    endif()
    include(CheckCSourceCompiles)
    set(flux_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -fsanitize=realtime -Werror=attributes")
    check_c_source_compiles("
        #if !__has_attribute(nonblocking)
        #error Clang nonblocking attribute is required
        #endif
        __attribute__((nonblocking)) int probe(void) { return 0; }
        int main(void) { return probe(); }
    " FLUX_C_HAS_RTSAN)
    set(CMAKE_REQUIRED_FLAGS "${flux_saved_required_flags}")
    if(NOT FLUX_C_HAS_RTSAN)
        message(FATAL_ERROR "Clang cannot compile/link RealtimeSanitizer with these flags. Install a toolchain with the RTSan runtime and use a separate sanitizer build.")
    endif()
endif()

add_library(flux src/ring.c src/pool.c src/latency.c)
add_library(flux::flux ALIAS flux)
set_target_properties(flux PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO VERSION ${PROJECT_VERSION} SOVERSION 0)
target_include_directories(flux PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(FLUX_ENABLE_RTSAN)
    target_compile_definitions(flux PRIVATE FLUX_ENABLE_RTSAN=1)
    # Propagate instrumentation and the runtime link flag to static-library users.
    target_compile_options(flux PUBLIC -fsanitize=realtime PRIVATE -fno-omit-frame-pointer)
    target_link_options(flux PUBLIC -fsanitize=realtime)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(flux PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
if(FLUX_ENABLE_POSIX_TIME)
    target_sources(flux PRIVATE src/time_posix.c)
    target_compile_definitions(flux PUBLIC FLUX_HAS_POSIX_TIME=1)
else()
    target_sources(flux PRIVATE src/time_stub.c)
    target_compile_definitions(flux PUBLIC FLUX_HAS_POSIX_TIME=0)
endif()

if(FLUX_BUILD_TESTS)
    enable_testing()
    foreach(component ring pool latency time)
        add_executable(test_${component} tests/test_${component}.c)
        target_link_libraries(test_${component} PRIVATE flux::flux)
        set_target_properties(test_${component} PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
        add_test(NAME ${component} COMMAND test_${component})
    endforeach()
    if(UNIX)
        find_package(Threads REQUIRED)
        add_executable(test_ring_threads tests/test_ring_threads.c)
        target_link_libraries(test_ring_threads PRIVATE flux::flux Threads::Threads)
        set_target_properties(test_ring_threads PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
        add_test(NAME ring_threads COMMAND test_ring_threads)
        set_tests_properties(ring_threads PROPERTIES TIMEOUT 60)
    endif()
    include(CheckLanguage)
    check_language(CXX)
    if(CMAKE_CXX_COMPILER)
        enable_language(CXX)
        if(FLUX_ENABLE_RTSAN)
            if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
                message(FATAL_ERROR "RTSan C++ tests require -DCMAKE_CXX_COMPILER=clang++")
            endif()
            include(CheckCXXSourceCompiles)
            set(flux_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
            set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -fsanitize=realtime")
            check_cxx_source_compiles("int main() { return 0; }" FLUX_CXX_HAS_RTSAN)
            set(CMAKE_REQUIRED_FLAGS "${flux_saved_required_flags}")
            if(NOT FLUX_CXX_HAS_RTSAN)
                message(FATAL_ERROR "The C++ toolchain cannot compile/link the RTSan runtime")
            endif()
        endif()
        add_executable(test_cpp tests/test_cpp.cpp)
        target_link_libraries(test_cpp PRIVATE flux::flux)
        set_target_properties(test_cpp PROPERTIES
            CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
        add_test(NAME cpp_headers COMMAND test_cpp)
    endif()
    if(FLUX_ENABLE_RTSAN)
        add_executable(test_rtsan_detection tests/test_rtsan_detection.c)
        target_link_libraries(test_rtsan_detection PRIVATE flux::flux)
        set_target_properties(test_rtsan_detection PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
        add_test(NAME rtsan_detection COMMAND ${CMAKE_COMMAND}
            "-DPROGRAM=$<TARGET_FILE:test_rtsan_detection>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_rtsan_detection.cmake")
        set_tests_properties(rtsan_detection PROPERTIES TIMEOUT 15)
    endif()
endif()

if(FLUX_BUILD_EXAMPLES)
    add_executable(flux_example examples/basic.c)
    target_link_libraries(flux_example PRIVATE flux::flux)
    set_target_properties(flux_example PROPERTIES C_STANDARD 11 C_EXTENSIONS NO)
endif()

if(FLUX_BUILD_BENCHMARKS)
    if(NOT UNIX OR NOT FLUX_ENABLE_POSIX_TIME)
        message(FATAL_ERROR "Benchmarks require POSIX threads and time support")
    endif()
    find_package(Threads REQUIRED)
    add_executable(flux_bench benchmarks/bench.c)
    target_link_libraries(flux_bench PRIVATE flux::flux Threads::Threads)
    target_compile_definitions(flux_bench PRIVATE
        FLUX_VERSION="${PROJECT_VERSION}" FLUX_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
    set_target_properties(flux_bench PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
endif()

# Developer tools are optional and never required for the library build.
find_program(FLUX_CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(FLUX_CLANG_FORMAT_EXECUTABLE)
    file(GLOB_RECURSE FLUX_FORMAT_FILES CONFIGURE_DEPENDS
        include/flux/*.h src/*.c src/*.h tests/*.c tests/*.cpp tests/*.h
        examples/*.c benchmarks/*.c)
    add_custom_target(format
        COMMAND "${FLUX_CLANG_FORMAT_EXECUTABLE}" --style=file -i ${FLUX_FORMAT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Formatting FluxC sources" VERBATIM)
    add_custom_target(format-check
        COMMAND "${FLUX_CLANG_FORMAT_EXECUTABLE}" --style=file --dry-run --Werror ${FLUX_FORMAT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking FluxC source formatting" VERBATIM)
endif()

find_program(FLUX_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
if(FLUX_CLANG_TIDY_EXECUTABLE AND CMAKE_EXPORT_COMPILE_COMMANDS
        AND CMAKE_GENERATOR MATCHES "Makefiles|Ninja")
    # Use only configured translation units, including the selected clock backend.
    get_property(FLUX_TOOL_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    set(FLUX_TIDY_FILES)
    foreach(tool_target IN LISTS FLUX_TOOL_TARGETS)
        get_target_property(tool_sources ${tool_target} SOURCES)
        foreach(tool_source IN LISTS tool_sources)
            if(tool_source MATCHES "\\.(c|cpp)$")
                list(APPEND FLUX_TIDY_FILES "${CMAKE_CURRENT_SOURCE_DIR}/${tool_source}")
            endif()
        endforeach()
    endforeach()
    add_custom_target(clang-tidy
        COMMAND "${FLUX_CLANG_TIDY_EXECUTABLE}" -p "${CMAKE_CURRENT_BINARY_DIR}" ${FLUX_TIDY_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Analyzing configured FluxC sources" VERBATIM)
endif()

include(CMakePackageConfigHelpers)
install(TARGETS flux EXPORT FluxCTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/flux DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT FluxCTargets NAMESPACE flux:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FluxC)
configure_package_config_file(cmake/FluxCConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/FluxCConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FluxC)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/FluxCConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/FluxCConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/FluxCConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FluxC)
