# PostSRSd - Sender Rewriting Scheme daemon for Postfix
# Copyright 2012-2022 Timo Röhling <timo@gaussglocke.de>
# SPDX-License-Identifier: GPL-3.0-only
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.
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

option(GENERATE_SRS_SECRET "Generate and install a postsrsd.secret" ON)
option(WITH_MILTER "Enable SRS rewrite via Milter (requires libmilter)" ON)
option(WITH_REDIS "Enable Redis-based storage for opaque SRS tokens (requires hiredis)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(utils)
include(CTest)
include(FetchContent)
include(GNUInstallDirs)
include(CheckIncludeFile)
include(CheckTypeSize)
include(CheckSymbolExists)
include(TestBigEndian)

find_systemd_unit_destination(DETECTED_SYSTEMD_UNITDIR)
set(SYSTEMD_UNITDIR "${DETECTED_SYSTEMD_UNITDIR}" CACHE PATH "Install destination for systemd service unit")

set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE ALWAYS)

FetchContent_Declare(Confuse
    URL https://github.com/libconfuse/libconfuse/releases/download/v3.3/confuse-3.3.tar.gz
    URL_HASH MD5=f590564c6ea4879db0c8f692bf12f42a
)

FetchContent_Declare(Hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis
    GIT_TAG b808c0c206e4b1eabdbce2ee723df4258461d4da
)

FetchContent_Declare(LibMilter
    GIT_REPOSITORY https://github.com/jons/libmilter
    GIT_TAG f3b7aa0336be0d0fdbca25b97e1e7fcb46e3f333
)

FetchContent_Declare(sqlite3
    URL https://sqlite.org/2022/sqlite-amalgamation-3390200.zip
    URL_HASH MD5=9edc80bacd6263d39f2cc36ba6b50e14
)

FetchContent_Declare(Check
    URL https://github.com/libcheck/check/releases/download/0.15.2/check-0.15.2.tar.gz
    URL_HASH MD5=50fcafcecde5a380415b12e9c574e0b2
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

if(WITH_MILTER)
    FetchContent_MakeAvailable(LibMilter)
    if (IS_DIRECTORY "${libmilter_SOURCE_DIR}")
        string(TOLOWER "${CMAKE_SYSTEM_NAME}" lc_system)
        set(SM_OS_H "${libmilter_SOURCE_DIR}/include/sm/os/sm_os_${lc_system}.h")
        if(EXISTS "${SM_OS_H}")
            execute_process(COMMAND "${CMAKE_COMMAND}"
                 -E copy_if_different "${SM_OS_H}" "${libmilter_SOURCE_DIR}/include/sm/sm_os.h"
            )
            add_library(milter STATIC
                "${libmilter_SOURCE_DIR}/errstring.c"
                "${libmilter_SOURCE_DIR}/strl.c"
                "${libmilter_SOURCE_DIR}/comm.c"
                "${libmilter_SOURCE_DIR}/engine.c"
                "${libmilter_SOURCE_DIR}/handler.c"
                "${libmilter_SOURCE_DIR}/listener.c"
                "${libmilter_SOURCE_DIR}/main.c"
                "${libmilter_SOURCE_DIR}/monitor.c"
                "${libmilter_SOURCE_DIR}/signal.c"
                "${libmilter_SOURCE_DIR}/smfi.c"
                "${libmilter_SOURCE_DIR}/sm_gethost.c"
                "${libmilter_SOURCE_DIR}/worker.c"
            )
            target_compile_definitions(milter PRIVATE
                NOT_SENDMAIL
                sm_printf=snprintf
                _REENTRANT
                XP_MT
            )
            target_include_directories(milter
                PRIVATE "${libmilter_SOURCE_DIR}/smapp"
                PUBLIC "${libmilter_SOURCE_DIR}/include"
                INTERFACE "${libmilter_SOURCE_DIR}/include/libmilter"
            )
            add_library(LibMilter::LibMilter ALIAS milter)
        else()
            message(STATUS "Missing support for ${CMAKE_SYSTEM_NAME} for LibMilter")
            set(WITH_MILTER OFF)
        endif()
    endif()
endif()

FetchContent_MakeAvailable(sqlite3)
if (IS_DIRECTORY "${sqlite3_SOURCE_DIR}")
    add_library(sqlite3 STATIC
        "${sqlite3_SOURCE_DIR}/sqlite3.c"
    )
    target_include_directories(sqlite3 PUBLIC "${sqlite3_SOURCE_DIR}")
    target_compile_definitions(sqlite3 PRIVATE
        SQLITE_DQS=0
        SQLITE_THREADSAFE=0
        SQLITE_DEFAULT_MEMSTATUS=0
        SQLITE_OMIT_PROGRESS_CALLBACK
    )
    add_library(sqlite3::sqlite3 ALIAS sqlite3)
    target_link_libraries(sqlite3 PUBLIC ${CMAKE_DL_LIBS})
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/postsrsd_build_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/postsrsd_build_config.h"
)

add_executable(postsrsd
    src/config.c
    src/endpoint.c
    src/main.c
    src/netstring.c
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
    $<$<BOOL:WITH_MILTER>:LibMilter::LibMilter>
    ${LIBSOCKET}
    ${LIBNSL}
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/data/postsrsd.conf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.conf"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/data/postsrsd.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.service"
    @ONLY
)

install(
    TARGETS postsrsd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.conf"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}"
)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.service"
    DESTINATION "${SYSTEMD_UNITDIR}"
)

if(GENERATE_SRS_SECRET)
    find_program(DD dd DOC "path to dd executable")
    find_program(BASE64 base64 DOC "path to base64 executable")
    find_program(OPENSSL openssl DOC "path to OpenSSL executable")
    find_file(RANDOM_SOURCE NAMES urandom random PATHS /dev)
    if(BASE64)
        set(BASE64_ENCODE "${BASE64}")
    elseif(OPENSSL)
        set(BASE64_ENCODE "${OPENSSL} base64 -e")
    endif()
    if(DD AND BASE64_ENCODE AND RANDOM_SOURCE)
        install(CODE "\
        execute_process(
            COMMAND ${DD} if=${RANDOM_SOURCE} bs=18 count=1
            COMMAND ${BASE64_ENCODE}
            OUTPUT_FILE \"${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.secret\"
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT EXISTS \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/${PROJECT_NAME}.secret\")
            file(INSTALL DESTINATION \"${CMAKE_INSTALL_FULL_SYSCONFDIR}\" FILES \"${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.secret\" RENAME \"${PROJECT_NAME}.secret\" PERMISSIONS OWNER_READ OWNER_WRITE)
        endif()
        file(REMOVE \"${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.secret\")")
    endif()
endif()

if(BUILD_TESTING)
    FetchContent_MakeAvailable(Check)
    if(IS_DIRECTORY "${check_SOURCE_DIR}")
        set_property(DIRECTORY "${check_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
    endif()
    add_subdirectory(tests)
endif()
