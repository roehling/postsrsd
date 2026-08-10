# PostSRSd - Sender Rewriting Scheme daemon for Postfix
# Copyright 2012-2026 Timo Röhling <timo@gaussglocke.de>
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
find_program(AFL_FUZZ afl-fuzz)

add_library(
    postsrsd_fuzz
    fuzz.c
    ${PROJECT_SOURCE_DIR}/src/config.c
    ${PROJECT_SOURCE_DIR}/src/database.c
    ${PROJECT_SOURCE_DIR}/src/endpoint.c
    ${PROJECT_SOURCE_DIR}/src/main.c
    ${PROJECT_SOURCE_DIR}/src/milter.c
    ${PROJECT_SOURCE_DIR}/src/netstring.c
    ${PROJECT_SOURCE_DIR}/src/sha1.c
    ${PROJECT_SOURCE_DIR}/src/srs.c
    ${PROJECT_SOURCE_DIR}/src/srs2.c
    ${PROJECT_SOURCE_DIR}/src/util.c
)
target_compile_definitions(
    postsrsd_fuzz PRIVATE POSTSRSD_FUZZING _GNU_SOURCE _FILE_OFFSET_BITS=64
)
target_include_directories(
    postsrsd_fuzz PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}
)
target_compile_options(
    postsrsd_fuzz PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
)
target_link_options(postsrsd_fuzz PRIVATE -fsanitize=address,undefined)
target_compile_features(postsrsd_fuzz PRIVATE c_std_99)
target_link_libraries(
    postsrsd_fuzz
    PRIVATE libconfuse::confuse
            $<$<BOOL:${WITH_SQLITE}>:sqlite3::sqlite3>
            $<$<BOOL:${WITH_REDIS}>:${HIREDIS_TARGET}>
            $<$<BOOL:${WITH_SECCOMP}>:seccomp::seccomp>
            ${LIBSOCKET}
            ${LIBNSL}
            ${LIBNETWORK}
)

function(add_fuzzer_harness name)
    cmake_parse_arguments(arg "" "SEED;DICT" "" ${ARGN})
    add_executable(${name}_harness ${name}.c)
    target_compile_options(
        ${name}_harness PRIVATE -fsanitize=fuzzer,address,undefined
                                -fno-omit-frame-pointer
    )
    target_link_options(
        ${name}_harness PRIVATE -fsanitize=fuzzer,address,undefined
    )
    target_link_libraries(${name}_harness PRIVATE postsrsd_fuzz)
    file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/FUZZING/${name})
    set(fuzzer_call ${AFL_FUZZ} -o "${PROJECT_BINARY_DIR}/FUZZING/${name}")
    if(arg_SEED)
        list(APPEND fuzzer_call -i
             "${CMAKE_CURRENT_SOURCE_DIR}/seeds/${arg_SEED}"
        )
    endif()
    if(arg_DICT)
        list(APPEND fuzzer_call -x
             "${CMAKE_CURRENT_SOURCE_DIR}/dict/${arg_DICT}"
        )
    endif()
    list(APPEND fuzzer_call "--" "$<TARGET_FILE:${name}_harness>")
    message(STATUS "${fuzzer_call}")
    add_custom_target(
        ${name}
        "${fuzzer_call}"
        COMMAND_EXPAND_LISTS
    )
endfunction()

add_fuzzer_harness(fuzz_netstring SEED netstring)
add_fuzzer_harness(fuzz_srs SEED srs)
