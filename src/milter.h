/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright 2012-2026 Timo Röhling <timo@gaussglocke.de>
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
#ifndef MILTER_H
#define MILTER_H

#include "util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define MILTER_FL_ADDHDRS     0x000001
#define MILTER_FL_CHGBODY     0x000002
#define MILTER_FL_ADDRCPT     0x000004
#define MILTER_FL_DELRCPT     0x000008
#define MILTER_FL_CHGHDRS     0x000010
#define MILTER_FL_QUARANTINE  0x000020
#define MILTER_FL_CHGFROM     0x000040
#define MILTER_FL_ADDRCPT_PAR 0x000080
#define MILTER_FL_SETSYMLIST  0x000100

#define MILTER_FL_NOCONNECT 0x000001
#define MILTER_FL_NOHELO    0x000002
#define MILTER_FL_NOMAIL    0x000004
#define MILTER_FL_NORCPT    0x000008
#define MILTER_FL_NOBODY    0x000010
#define MILTER_FL_NOHDRS    0x000020
#define MILTER_FL_NOEOH     0x000040
#define MILTER_FL_NOUNKNOWN 0x000100
#define MILTER_FL_NODATA    0x000200

#define MILTER_CMD_ABORT   'A'
#define MILTER_CMD_BODY    'B'
#define MILTER_CMD_CONNECT 'C'
#define MILTER_CMD_MACRO   'D'
#define MILTER_CMD_EOM     'E'
#define MILTER_CMD_HELO    'H'
#define MILTER_CMD_HEADER  'L'
#define MILTER_CMD_MAIL    'M'
#define MILTER_CMD_EOH     'N'
#define MILTER_CMD_OPTNEG  'O'
#define MILTER_CMD_QUIT    'Q'
#define MILTER_CMD_RCPT    'R'
#define MILTER_CMD_DATA    'T'
#define MILTER_CMD_UNKNOWN 'U'

#define MILTER_DO_ACCEPT    'a'
#define MILTER_DO_CONTINUE  'c'
#define MILTER_DO_DISCARD   'd'
#define MILTER_DO_REJECT    'r'
#define MILTER_DO_TEMPFAIL  't'
#define MILTER_DO_REPLYCODE 'y'
#define MILTER_DO_SKIP      's'

#define MILTER_DO_ADDRCPT     '+'
#define MILTER_DO_DELRCPT     '-'
#define MILTER_DO_ADDRCPT_PAR '2'
#define MILTER_DO_REPLBODY    'b'
#define MILTER_DO_CHGFROM     'e'
#define MILTER_DO_ADDHEADER   'h'
#define MILTER_DO_INSHEADER   'h'
#define MILTER_DO_CHGHEADER   'm'
#define MILTER_DO_QUARANTINE  'q'
#define MILTER_DO_PROGRESS    'p'
#define MILTER_DO_SETSYMLIST  'l'

#define MILTER_PAYLOAD_SIZE 512

size_t milter_receive(FILE* fp, void* buffer, size_t size, size_t* truncated);
bool milter_send(FILE* fp, char action);
bool milter_send_bytes(FILE* fp, char action, const void* data, size_t length);
bool milter_send_str(FILE* fp, char action, const char* value);
bool milter_send_str_list(FILE* fp, char action, list_t* L);
bool milter_tempfail(FILE* fp);
bool milter_continue(FILE* fp);
bool milter_accept(FILE* fp);
bool milter_reject(FILE* fp);
void milter_parse_str_list(list_t* L, const char* data, size_t length);
bool milter_handle_optneg(FILE* fp, const void* input, size_t length);
char* milter_find_macro(const char* name, const char* data, size_t length);

#endif
