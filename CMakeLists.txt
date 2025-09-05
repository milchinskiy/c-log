cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_CURRENT_LIST_DIR}/project.toml" _PROJECT_TOML)

# Extract name = "..." and version = "..." from TOML
string(REGEX MATCH "name[ \t]*=[ \t]*\"([^\"]+)\"" _NAME_MATCH
             "${_PROJECT_TOML}")
set(PROJECT_NAME_FROM_TOML "${CMAKE_MATCH_1}")

string(REGEX MATCH "version[ \t]*=[ \t]*\"([^\"]+)\"" _VER_MATCH
             "${_PROJECT_TOML}")
set(PROJECT_VERSION_FROM_TOML "${CMAKE_MATCH_1}")

if(NOT PROJECT_NAME_FROM_TOML)
  set(PROJECT_NAME_FROM_TOML "c-simple-osd")
endif()
if(NOT PROJECT_VERSION_FROM_TOML)
  set(PROJECT_VERSION_FROM_TOML "0.1.0")
endif()

project(
  ${PROJECT_NAME_FROM_TOML}
  VERSION ${PROJECT_VERSION_FROM_TOML}
  LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(
    -fstack-protector-strong
    -fno-common
    -Wformat=2
    -Wformat-truncation
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wstrict-prototypes
    -Wvla
    -Wall
    -Wextra
    -Wpedantic
    -Werror)
endif()

# ========= Options that map to c-log.h flags =========
set(CLOG_DEFAULT_LEVEL
    "CLOG_INFO"
    CACHE STRING "Runtime default level (e.g. CLOG_WARN or 3)")
set(CLOG_MIN_LEVEL
    ""
    CACHE STRING "Compile-time minimum (e.g. CLOG_WARN or 3); empty keeps all")

option(CLOG_THREAD_SAFE "Spinlock to serialize writes" ON)
option(CLOG_COLOR "ANSI colors when stderr is a TTY" ON)
option(CLOG_COLOR_FORCE "Force color even if not a TTY" OFF)
option(CLOG_WITH_LINE "Include [file:line]" ON)
option(CLOG_WITH_TID "Include (tid:...)" ON)
option(CLOG_WITH_BUILD_IN_PREFIX
       "Append [build:...] each line if CLOG_BUILD is set" OFF)
set(CLOG_BUILD
    "${PROJECT_NAME_FROM_TOML}_v${PROJECT_VERSION_FROM_TOML}"
    CACHE STRING "Build tag (default: <name>-<version>)")

# ========= Library (single-header impl compiled once) =========
add_library(c_log STATIC src/c-log-impl.c)
target_include_directories(
  c_log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src # for "c-log.h"
               ${CMAKE_CURRENT_SOURCE_DIR} # optional
)
set_target_properties(c_log PROPERTIES OUTPUT_NAME "c-log" C_STANDARD 11)

# Threads for pthread_self on POSIX (harmless on Windows)
find_package(Threads)
if(Threads_FOUND)
  target_link_libraries(c_log PUBLIC Threads::Threads)
endif()

# Helper to map ON/OFF to 1/0 defines
function(apply_bool_def target name enabled)
  if(${enabled})
    target_compile_definitions(${target} PUBLIC ${name}=1)
  else()
    target_compile_definitions(${target} PUBLIC ${name}=0)
  endif()
endfunction()

# Apply compile defs to the lib
target_compile_definitions(c_log
                           PUBLIC CLOG_DEFAULT_LEVEL=${CLOG_DEFAULT_LEVEL})
if(NOT "${CLOG_MIN_LEVEL}" STREQUAL "")
  target_compile_definitions(c_log PUBLIC CLOG_MIN_LEVEL=${CLOG_MIN_LEVEL})
endif()
apply_bool_def(c_log CLOG_THREAD_SAFE ${CLOG_THREAD_SAFE})
apply_bool_def(c_log CLOG_COLOR ${CLOG_COLOR})
apply_bool_def(c_log CLOG_COLOR_FORCE ${CLOG_COLOR_FORCE})
apply_bool_def(c_log CLOG_WITH_LINE ${CLOG_WITH_LINE})
apply_bool_def(c_log CLOG_WITH_TID ${CLOG_WITH_TID})
apply_bool_def(c_log CLOG_WITH_BUILD_IN_PREFIX ${CLOG_WITH_BUILD_IN_PREFIX})
if(NOT "${CLOG_BUILD}" STREQUAL "")
  target_compile_definitions(c_log PUBLIC CLOG_BUILD="${CLOG_BUILD}")
endif()

# ========= Demo =========
add_executable(c-log-demo examples/demo.c)
target_link_libraries(c-log-demo PRIVATE c_log)
set_target_properties(c-log-demo PROPERTIES C_STANDARD 11)

# ========= Tests =========
include(CTest)
enable_testing()

add_executable(c-log-tests tests/test_c-log.c)
target_link_libraries(c-log-tests PRIVATE c_log)
set_target_properties(c-log-tests PROPERTIES C_STANDARD 11)

add_test(NAME c-log-tests COMMAND c-log-tests)
set_tests_properties(c-log-tests PROPERTIES ENVIRONMENT "NO_COLOR=1")

# ========= Install =========
install(
  TARGETS c_log c-log-demo c-log-tests
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)
install(FILES src/c-log.h DESTINATION include)
