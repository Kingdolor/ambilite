cmake_minimum_required(VERSION 3.6)
project(ambilite VERSION 0.2.2 LANGUAGES C CXX)

set(_cflags_list "")
list(APPEND _cflags_list -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=neon)
list(JOIN _cflags_list " " COMMON_FLAGS)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(libyuv)
add_subdirectory(tv-native-apis)

add_compile_options(${_cflags_list} -Wall -Wextra -DSECURITY_COMPATIBILITY)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTHREAD2 REQUIRED gthread-2.0)
pkg_check_modules(PBNJSON REQUIRED pbnjson_c)
pkg_check_modules(GLIB2 REQUIRED glib-2.0)
pkg_check_modules(LS2 REQUIRED luna-service2)
pkg_check_modules(PMLOG REQUIRED PmLogLib)
include_directories(${GTHREAD2_INCLUDE_DIRS} ${PBNJSON_INCLUDE_DIRS} ${GLIB2_INCLUDE_DIRS} ${LS2_INCLUDE_DIRS} ${PMLOG_INCLUDE_DIRS})

add_subdirectory(unicapture)

configure_file(src/version.h.in ${CMAKE_BINARY_DIR}/version.h)

add_executable(ambilite
    src/main.c
    src/service.c
    src/settings.c
    src/ledproc.c
    src/serial_out.c
    src/hotkey.c
    src/effects.c
    src/httpapi.c
    src/log.c
    src/utils.c
)
set_target_properties(ambilite PROPERTIES
    BUILD_RPATH_USE_ORIGIN ON
    INSTALL_RPATH "$ORIGIN:$ORIGIN/lib"
    BUILD_RPATH "$ORIGIN:$ORIGIN/lib"
)
target_include_directories(ambilite PRIVATE ${CMAKE_BINARY_DIR} src)
target_link_libraries(ambilite pthread dl unicapture rt m ${GTHREAD2_LDFLAGS} ${PBNJSON_LDFLAGS} ${LS2_LDFLAGS} ${GLIB2_LDFLAGS} ${PMLOG_LDFLAGS})
set_property(TARGET ambilite PROPERTY ENABLE_EXPORTS 1)
