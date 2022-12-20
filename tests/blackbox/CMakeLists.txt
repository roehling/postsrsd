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
find_program(FAKETIME_EXECUTABLE NAMES faketime)
find_package(Python3)

if(FAKETIME_EXECUTABLE AND Python3_EXECUTABLE)
    add_test(
        NAME test_socketmap
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/blackbox.py" "${FAKETIME_EXECUTABLE}" "$<TARGET_FILE:postsrsd>" "$<BOOL:${WITH_SQLITE}>"
    )
endif()
