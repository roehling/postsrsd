# PostSRSd - Sender Rewriting Scheme daemon for Postfix
# Copyright 2012-2022 Timo Röhling <timo@gaussglocke.de>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
cmake_minimum_required(VERSION 3.14...3.24)
project(postsrsd VERSION 2.0.0 LANGUAGES C)

option(WITH_MILTER "Enable SRS rewrite via Milter (requires libmilter)" ON)
option(WITH_REDIS "Enable Redis-based storage for opaque SRS tokens (requires hiredis)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AddAutotoolsDependency)
include(FetchContent)
include(GNUInstallDirs)
include(CheckIncludeFile)
include(CheckTypeSize)
include(CheckSymbolExists)
include(TestBigEndian)

FetchContent_Declare(Confuse
    URL https://github.com/libconfuse/libconfuse/releases/download/v3.3/confuse-3.3.tar.gz
    URL_HASH MD5=f590564c6ea4879db0c8f692bf12f42a
    FIND_PACKAGE_ARGS
)

FetchContent_Declare(Hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis
    GIT_SHALLOW TRUE
    GIT_TAG b808c0c206e4b1eabdbce2ee723df4258461d4da
    FIND_PACKAGE_ARGS
)

FetchContent_Declare(sqlite3
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite3
    FIND_PACKAGE_ARGS
)

set(saved_CMAKE_REQUIRED_DEFINITIONS "${CMAKE_REQUIRED_DEFINITIONS}")
list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")

check_include_file(alloca.h HAVE_ALLOCA_H)
check_include_file(fcntl.h HAVE_FCNTL_H)
check_include_file(netdb.h HAVE_NETDB_H)
check_include_file(sys/file.h HAVE_SYS_FILE_H)
check_include_file(sys/inotify.h HAVE_SYS_INOTIFY_H)
check_include_file(sys/socket.h HAVE_SYS_SOCKET_H)
check_include_file(sys/stat.h HAVE_SYS_STAT_H)
check_include_file(sys/time.h HAVE_SYS_TIME_H)
check_include_file(sys/types.h HAVE_SYS_TYPES_H)
check_include_file(sys/un.h HAVE_SYS_UN_H)
check_include_file(time.h HAVE_TIME_H)
check_include_file(unistd.h HAVE_UNISTD_H)
check_symbol_exists(close_range unistd.h HAVE_CLOSE_RANGE)
check_symbol_exists(strcasecmp string.h HAVE_STRCASECMP)
check_symbol_exists(_stricmp string.h HAVE__STRICMP)
check_symbol_exists(strncasecmp string.h HAVE_STRNCASECMP)
check_symbol_exists(_strnicmp string.h HAVE__STRNICMP)
check_type_size("unsigned long" SIZEOF_UNSIGNED_LONG)
test_big_endian(HAVE_BIG_ENDIAN)

set(CMAKE_REQUIRED_DEFINITIONS "${saved_CMAKE_REQUIRED_DEFINITIONS}")

if(CMAKE_SYSTEM_NAME MATCHES "SunOS")
    find_library(LIBSOCKET socket)
    find_library(LIBNSL nsl)
endif()

add_autotools_dependency(Confuse
    LIBRARY_NAME confuse
    EXPORTED_TARGET Confuse::Confuse
)

if(WITH_REDIS)
    set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(Hiredis)
    if(IS_DIRECTORY "${hiredis_SOURCE_DIR}")
        set_property(DIRECTORY "${hiredis_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
    endif()
    if(TARGET hiredis::hiredis_static)
        set(HIREDIS_TARGET hiredis::hiredis_static)
    elseif(TARGET hiredis::hiredis)
        set(HIREDIS_TARGET hiredis::hiredis)
    elseif(TARGET hiredis)
        set(HIREDIS_TARGET hiredis)
    else()
        set(WITH_REDIS OFF)
    endif()
endif()

FetchContent_MakeAvailable(sqlite3)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/postsrsd_build_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/postsrsd_build_config.h"
)

add_executable(postsrsd
    src/config.c
    src/endpoint.c
    src/main.c
    src/sha1.c
    src/srs2.c
)

target_compile_definitions(postsrsd PRIVATE
    _GNU_SOURCE _FILE_OFFSET_BITS=64
)
target_include_directories(postsrsd PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(postsrsd PRIVATE c_std_99)
target_link_libraries(postsrsd PRIVATE
    Confuse::Confuse
    sqlite3::sqlite3
    $<$<BOOL:WITH_REDIS>:${HIREDIS_TARGET}>
    ${LIBSOCKET}
    ${LIBNSL}
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/data/postsrsd.conf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/postsrsd.conf"
)

install(
    TARGETS postsrsd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/postsrsd.conf"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}"
)
