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
macro(add_postsrsd_test name)
    add_executable(${name}_executable ${name}.c ${ARGN})
    target_include_directories(${name}_executable PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${name}_executable PRIVATE Check::check)
    target_compile_features(${name}_executable PRIVATE c_std_99)
    add_test(NAME ${name} COMMAND ${name}_executable)
endmacro()

add_postsrsd_test(test_netstring ../src/netstring.c)
