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
find_package(Python3 3.5 QUIET)

if(Python3_EXECUTABLE)
    add_test(
        NAME blackbox_test_socketmap
        COMMAND
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/socketmap.py"
            "$<TARGET_FILE:postsrsd>" "$<BOOL:${WITH_SQLITE}>"
            "$<AND:$<BOOL:${WITH_REDIS}>,$<BOOL:${TESTS_WITH_REDIS}>>"
    )
    add_test(
        NAME blackbox_test_milter
        COMMAND
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/milter.py"
            "$<TARGET_FILE:postsrsd>" "$<BOOL:${WITH_SQLITE}>"
            "$<AND:$<BOOL:${WITH_REDIS}>,$<BOOL:${TESTS_WITH_REDIS}>>"
    )
    add_test(
        NAME blackbox_test_daemon_reload
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/reload.py"
                "$<TARGET_FILE:postsrsd>" "$<BOOL:${HAVE_SYS_INOTIFY_H}>"
    )
endif()
