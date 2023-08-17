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
macro(add_postsrsd_test name)
    add_executable(${name}_executable ${name}.c ${ARGN})
    target_include_directories(
        ${name}_executable PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src
    )
    target_compile_definitions(
        ${name}_executable PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64
    )
    if(HAVE_CLOSE_RANGE_GNU AND NOT HAVE_CLOSE_RANGE)
        target_compile_definitions(${name}_executable PRIVATE _GNU_SOURCE)
    endif()
    target_link_libraries(${name}_executable PRIVATE Check::check)
    target_compile_features(${name}_executable PRIVATE c_std_99)
    if(TESTS_WITH_ASAN)
        target_compile_options(
            ${name}_executable PRIVATE -fsanitize=address,undefined
        )
        target_link_options(
            ${name}_executable PRIVATE -fsanitize=address,undefined
        )
    endif()
    add_test(NAME ${name} COMMAND ${name}_executable)
endmacro()

set(SRCDIR ../../src)

add_postsrsd_test(test_netstring ${SRCDIR}/netstring.c)
add_postsrsd_test(test_sha1 ${SRCDIR}/sha1.c)
add_postsrsd_test(test_util ${SRCDIR}/util.c)
add_postsrsd_test(test_database ${SRCDIR}/database.c ${SRCDIR}/util.c)
target_link_libraries(
    test_database_executable PRIVATE $<$<BOOL:${WITH_SQLITE}>:sqlite3::sqlite3>
                                     $<$<BOOL:${WITH_REDIS}>:${HIREDIS_TARGET}>
)
add_postsrsd_test(test_srs2 ${SRCDIR}/srs2.c ${SRCDIR}/sha1.c)
