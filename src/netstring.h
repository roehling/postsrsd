/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright 2012-2022 Timo Röhling <timo@gaussglocke.de>
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef NETSTRING_H
#define NETSTRING_H

#include <stdio.h>
#include <stdlib.h>

char* netstring_encode(const char* data, size_t length, char* buffer, size_t bufsize, size_t* encoded_length);
char* netstring_decode(const char* netstring, char* buffer, size_t bufsize, size_t* decoded_length);
char* netstring_read(FILE* f, char* buffer, size_t bufsize, size_t* decoded_length);
int netstring_write(FILE* f, const char* data, size_t length);

#endif
