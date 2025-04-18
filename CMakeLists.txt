# PostSRSd - Sender Rewriting Scheme daemon for Postfix
# Copyright 2012-2023 Timo Röhling <timo@gaussglocke.de>
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
cmake_minimum_required(VERSION 3.14...3.29)
project(
    postsrsd
    VERSION 2.0.11
    LANGUAGES C
    DESCRIPTION "Sender Rewriting Scheme daemon for Postfix"
    HOMEPAGE_URL "https://github.com/roehling/postsrsd"
)

if(CMAKE_VERSION VERSION_LESS 3.21.0)
    # Workaround for https://gitlab.kitware.com/cmake/cmake/-/issues/22234
    if(POLICY CMP0082)
        cmake_policy(SET CMP0082 OLD)
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(utils)
include(CTest)
include(FeatureSummary)
include(FetchContent)
include(GNUInstallDirs)
include(CheckIncludeFile)
include(CheckTypeSize)
include(CheckSymbolExists)
include(TestBigEndian)

set(POSTSRSD_CONFIGDIR
    "${CMAKE_INSTALL_FULL_SYSCONFDIR}"
    CACHE
        PATH
        "The default directory where PostSRSd should look for configuration files"
)
set(POSTSRSD_DATADIR
    "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/lib/${PROJECT_NAME}"
    CACHE PATH "The default directory where PostSRSd should put runtime data"
)
set(POSTSRSD_CHROOTDIR
    "${POSTSRSD_DATADIR}"
    CACHE PATH "The default chroot directory where PostSRSd should jail itself"
)
set(POSTSRSD_USER
    "nobody"
    CACHE STRING "The default unprivileged user as which PostSRSd will run"
)

option(WITH_MILTER "Enable SRS rewrite via Milter (requires libmilter)" OFF)
add_feature_info(WITH_MILTER WITH_MILTER "run PostSRSd as milter")
option(WITH_SQLITE
       "Enable sqlite-based storage for opaque SRS tokens (requires sqlite3)"
       OFF
)
add_feature_info(WITH_SQLITE WITH_SQLITE "use SQLite as database backend")
option(WITH_REDIS
       "Enable Redis-based storage for opaque SRS tokens (requires hiredis)"
       OFF
)
add_feature_info(WITH_REDIS WITH_REDIS "use Redis as database backend")
option(TESTS_WITH_ASAN "Run test suite with AddressSanitizer" ON)
option(DEVELOPER_BUILD "Add strict compiler options for development only" OFF)
option(GENERATE_SRS_SECRET "Generate and install a postsrsd.secret" ON)
add_feature_info(
    GENERATE_SRS_SECRET
    GENERATE_SRS_SECRET
    "generate missing ${CMAKE_INSTALL_FULL_SYSCONFDIR}/${PROJECT_NAME}.secret on install"
)
option(INSTALL_SYSTEMD_SERVICE "Install the systemd postsrsd.service unit" ON)
add_feature_info(
    INSTALL_SYSTEMD_SERVICE INSTALL_SYSTEMD_SERVICE
    "install the systemd postsrsd.service unit"
)
find_systemd_unit_destination(DETECTED_SYSTEMD_UNITDIR)
set(SYSTEMD_UNITDIR
    "${DETECTED_SYSTEMD_UNITDIR}"
    CACHE PATH "Install destination for the systemd postsrsd.service unit"
)
find_systemd_sysusers_destination(DETECTED_SYSTEMD_SYSUSERSDIR)
set(SYSTEMD_SYSUSERSDIR
    "${DETECTED_SYSTEMD_SYSUSERSDIR}"
    CACHE PATH "Install destination for the sysusers.d configuration"
)

if(POSTSRSD_CHROOTDIR)
    file(RELATIVE_PATH CHROOTABLE_DATADIR "${POSTSRSD_CHROOTDIR}"
         "${POSTSRSD_DATADIR}"
    )
    if(CHROOTABLE_DATADIR MATCHES "^\.\.")
        message(
            FATAL_ERROR
                "POSTSRSD_DATADIR (${POSTSRSD_DATADIR}) must be below POSTSRSD_CHROOTDIR (${POSTSRSD_CHROOTDIR})"
        )
    endif()
    if(CHROOTABLE_DATADIR STREQUAL "")
        set(CHROOTABLE_DATADIR ".")
    endif()
else()
    set(CHROOTABLE_DATADIR "${POSTSRSD_DATADIR}")
endif()

mark_as_advanced(
    TESTS_WITH_ASAN
    GENERATE_SRS_SECRET
    DEVELOPER_BUILD
    SYSTEMD_UNITDIR
    POSTSRSD_CONFIGDIR
    POSTSRSD_CHROOTDIR
    POSTSRSD_DATADIR
    POSTSRSD_USER
)

FetchContent_Declare(
    Confuse
    URL https://github.com/libconfuse/libconfuse/releases/download/v3.3/confuse-3.3.tar.gz
    URL_HASH
        SHA3_256=da895d91a7755941872e73ff6522fd16810f1599862990df569459a0eee94515
)

FetchContent_Declare(
    Hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis
    GIT_TAG c14775b4e48334e0262c9f168887578f4a368b5d
)

FetchContent_Declare(
    LibMilter
    GIT_REPOSITORY https://github.com/roehling/libmilter
    GIT_TAG 3661f3c5ac5e47205f26775031d2ac276d6d47ca
)

FetchContent_Declare(
    sqlite3
    URL https://sqlite.org/2023/sqlite-amalgamation-3410200.zip
    URL_HASH
        SHA3_256=c51ca72411b8453c64e0980be23bc9b9530bdc3ec1513e06fbf022ed0fd02463
)

FetchContent_Declare(
    Check
    URL https://github.com/libcheck/check/releases/download/0.15.2/check-0.15.2.tar.gz
    URL_HASH
        SHA3_256=bfb856a68c0ea4d930803f6bd16c1eed38910a231c9e0f0009e69310e35e7a5d
)

set(saved_CMAKE_REQUIRED_DEFINITIONS "${CMAKE_REQUIRED_DEFINITIONS}")
list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE" "-D_FILE_OFFSET_BITS=64")

check_include_file(alloca.h HAVE_ALLOCA_H)
check_include_file(errno.h HAVE_ERRNO_H)
check_include_file(fcntl.h HAVE_FCNTL_H)
check_include_file(grp.h HAVE_GRP_H)
check_include_file(netdb.h HAVE_NETDB_H)
check_include_file(poll.h HAVE_POLL_H)
check_include_file(pwd.h HAVE_PWD_H)
check_include_file(signal.h HAVE_SIGNAL_H)
check_include_file(sys/file.h HAVE_SYS_FILE_H)
check_include_file(sys/inotify.h HAVE_SYS_INOTIFY_H)
check_include_file(sys/socket.h HAVE_SYS_SOCKET_H)
check_include_file(sys/stat.h HAVE_SYS_STAT_H)
check_include_file(sys/time.h HAVE_SYS_TIME_H)
check_include_file(sys/types.h HAVE_SYS_TYPES_H)
check_include_file(sys/un.h HAVE_SYS_UN_H)
check_include_file(sys/wait.h HAVE_SYS_WAIT_H)
check_include_file(syslog.h HAVE_SYSLOG_H)
check_include_file(time.h HAVE_TIME_H)
check_include_file(unistd.h HAVE_UNISTD_H)
check_symbol_exists(chroot unistd.h HAVE_CHROOT)
check_symbol_exists(close_range unistd.h HAVE_CLOSE_RANGE)
check_symbol_exists(setgroups grp.h HAVE_SETGROUPS)
check_symbol_exists(strcasecmp strings.h HAVE_STRCASECMP)
check_symbol_exists(_stricmp string.h HAVE__STRICMP)
check_symbol_exists(strncasecmp strings.h HAVE_STRNCASECMP)
check_symbol_exists(_strnicmp string.h HAVE__STRNICMP)
check_type_size("unsigned long" SIZEOF_UNSIGNED_LONG)
test_big_endian(HAVE_BIG_ENDIAN)

set(CMAKE_REQUIRED_DEFINITIONS "${saved_CMAKE_REQUIRED_DEFINITIONS}")

if(CMAKE_SYSTEM_NAME MATCHES "Solaris|SunOS")
    find_library(LIBSOCKET socket)
    find_library(LIBNSL nsl)
endif()

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads QUIET)

add_autotools_dependency(
    Confuse
    LIBRARY_NAME confuse
    EXPORTED_TARGET Confuse::Confuse
)

if(WITH_REDIS)
    set(DISABLE_TESTS
        ON
        CACHE BOOL "" FORCE
    )
    set(BUILD_SHARED_LIBS
        OFF
        CACHE BOOL "" FORCE
    )
    FetchContent_MakeAvailable(Hiredis)
    if(IS_DIRECTORY "${hiredis_SOURCE_DIR}")
        set_property(
            DIRECTORY "${hiredis_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE
        )
    endif()
    if(TARGET hiredis::hiredis_static)
        set(HIREDIS_TARGET hiredis::hiredis_static)
    elseif(TARGET hiredis::hiredis)
        set(HIREDIS_TARGET hiredis::hiredis)
    elseif(TARGET hiredis)
        set(HIREDIS_TARGET hiredis)
    else()
        message(
            FATAL_ERROR
                "Cannot link against hiredis: no suitable CMake target found"
        )
    endif()
endif()

if(WITH_MILTER)
    FetchContent_MakeAvailable(LibMilter)
    if(IS_DIRECTORY "${libmilter_SOURCE_DIR}")
        string(TOLOWER "${CMAKE_SYSTEM_NAME}" lc_system)
        set(SM_OS_H
            "${libmilter_SOURCE_DIR}/include/sm/os/sm_os_${lc_system}.h"
        )
        if(EXISTS "${SM_OS_H}")
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SM_OS_H}"
                        "${libmilter_SOURCE_DIR}/include/sm/sm_os.h"
            )
            add_library(
                milter STATIC
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
            target_compile_definitions(
                milter PRIVATE NOT_SENDMAIL=1 sm_snprintf=snprintf
            )
            target_include_directories(
                milter
                PRIVATE "${libmilter_SOURCE_DIR}/smapp"
                PUBLIC "${libmilter_SOURCE_DIR}/include"
                INTERFACE "${libmilter_SOURCE_DIR}/include/libmilter"
            )
            if(TARGET Threads::Threads)
                target_link_libraries(milter PRIVATE Threads::Threads)
            endif()
            add_library(LibMilter::LibMilter ALIAS milter)
        else()
            message(
                FATAL_ERROR
                    "Missing support for ${CMAKE_SYSTEM_NAME} for LibMilter"
            )
        endif()
    endif()
endif()

if(WITH_SQLITE)
    FetchContent_MakeAvailable(sqlite3)
    if(IS_DIRECTORY "${sqlite3_SOURCE_DIR}")
        add_library(sqlite3 STATIC "${sqlite3_SOURCE_DIR}/sqlite3.c")
        target_include_directories(sqlite3 PUBLIC "${sqlite3_SOURCE_DIR}")
        target_compile_definitions(
            sqlite3
            PRIVATE SQLITE_DQS=0
                    SQLITE_THREADSAFE=$<IF:$<BOOL:${WITH_MILTER}>,2,0>
                    SQLITE_DEFAULT_MEMSTATUS=0
                    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
                    SQLITE_LIKE_DOESNT_MATCH_BLOBS
                    SQLITE_MAX_EXPR_DEPTH=0
                    SQLITE_OMIT_DECLTYPE
                    SQLITE_OMIT_DEPRECATED
                    SQLITE_OMIT_PROGRESS_CALLBACK
                    SQLITE_OMIT_SHARED_CACHE
                    SQLITE_USE_ALLOCA
        )
        if(WITH_MILTER AND TARGET Threads::Threads)
            target_link_libraries(sqlite3 PRIVATE Threads::Threads)
        endif()
        add_library(sqlite3::sqlite3 ALIAS sqlite3)
        target_link_libraries(sqlite3 PUBLIC ${CMAKE_DL_LIBS})
    endif()
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/postsrsd_build_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/postsrsd_build_config.h"
)

if(DEVELOPER_BUILD AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror)
endif()

add_executable(
    postsrsd
    src/config.c
    src/database.c
    src/endpoint.c
    src/main.c
    src/milter.c
    src/netstring.c
    src/sha1.c
    src/srs.c
    src/srs2.c
    src/util.c
)

target_compile_definitions(postsrsd PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
target_include_directories(postsrsd PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(postsrsd PRIVATE c_std_99)
target_link_libraries(
    postsrsd
    PRIVATE Confuse::Confuse
            $<$<BOOL:${WITH_SQLITE}>:sqlite3::sqlite3>
            $<$<BOOL:${WITH_REDIS}>:${HIREDIS_TARGET}>
            $<$<BOOL:${WITH_MILTER}>:LibMilter::LibMilter>
            ${LIBSOCKET}
            ${LIBNSL}
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/data/postsrsd.conf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.conf" @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/data/postsrsd.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.service" @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/data/sysusers.conf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/sysusers.d/${PROJECT_NAME}.conf" @ONLY
)

install(TARGETS postsrsd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.conf"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${PROJECT_NAME}"
)
if(INSTALL_SYSTEMD_SERVICE)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.service"
            DESTINATION "${SYSTEMD_UNITDIR}"
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sysusers.d/${PROJECT_NAME}.conf"
            DESTINATION "${SYSTEMD_SYSUSERSDIR}"
    )
endif()

if(GENERATE_SRS_SECRET)
    find_program(DD dd DOC "path to dd executable")
    find_program(BASE64 base64 DOC "path to base64 executable")
    find_program(OPENSSL openssl DOC "path to OpenSSL executable")
    find_file(
        RANDOM_SOURCE
        NAMES urandom random
        PATHS /dev
    )
    if(BASE64)
        set(BASE64_ENCODE "${BASE64}")
    elseif(OPENSSL)
        set(BASE64_ENCODE "${OPENSSL} base64 -e")
    endif()
    if(DD
       AND BASE64_ENCODE
       AND RANDOM_SOURCE
    )
        install(
            CODE "\
        if(NOT EXISTS \"\$ENV{DESTDIR}${POSTSRSD_CONFIGDIR}/${PROJECT_NAME}.secret\")
            message(STATUS \"Generating: \$ENV{DESTDIR}${POSTSRSD_CONFIGDIR}/${PROJECT_NAME}.secret\")
            execute_process(
                COMMAND ${DD} if=${RANDOM_SOURCE} bs=18 count=1
                COMMAND ${BASE64_ENCODE}
                OUTPUT_FILE \"${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.secret\"
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            file(INSTALL DESTINATION \"${POSTSRSD_CONFIGDIR}\" FILES \"${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.secret\" RENAME \"${PROJECT_NAME}.secret\" PERMISSIONS OWNER_READ OWNER_WRITE)
            file(REMOVE \"${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.secret\")
        endif()"
        )
    endif()
endif()
# Create directory for chroot jail on install
install(CODE "\
    message(STATUS \"Installing: \$ENV{DESTDIR}${POSTSRSD_CHROOTDIR}\")
    file(MAKE_DIRECTORY \"\$ENV{DESTDIR}${POSTSRSD_CHROOTDIR}\")"
)

if(BUILD_TESTING)
    FetchContent_MakeAvailable(Check)
    if(IS_DIRECTORY "${check_SOURCE_DIR}")
        # Workaround for https://github.com/roehling/postsrsd/issues/161
        file(REMOVE "${check_SOURCE_DIR}/src/check.h")
        set_property(
            DIRECTORY "${check_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE
        )
        if(TARGET Threads::Threads)
            target_link_libraries(check PRIVATE Threads::Threads)
        endif()
    endif()
    add_subdirectory(tests)
endif()

feature_summary(WHAT ENABLED_FEATURES DISABLED_FEATURES)

set(CPACK_SET_DESTDIR ON)
set(CPACK_PACKAGE_NAME "postsrsd")
set(CPACK_PACKAGE_VENDOR "Timo Röhling")
set(CPACK_PACKAGE_RELOCATABLE OFF)
set(CPACK_PACKAGE_DESCRIPTION
    "\
The Sender Rewriting Scheme (SRS) is a technique to forward mails from domains
which deploy the Sender Policy Framework (SPF) to prohibit other Mail Transfer
Agents (MTAs) from sending mails on their behalf.

PostSRSd implements SRS for the Postfix MTA."
)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Timo Röhling <timo@gaussglocke.de>")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_RELEASE 1)
set(CPACK_DEBIAN_PACKAGE_SECTION "mail")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
set(CPACK_RPM_PACKAGE_LICENSE "GPLv3")
set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)
include(CPack)
