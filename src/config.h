/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright 2012-2022 Timo Röhling <timo@gaussglocke.de>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef CONFIG_H
#define CONFIG_H

struct config
{
    char* socketmap_endpoint;
    char* milter_endpoint;
    char* pid_file;
    char* secrets_file;
    char* tokens_db;
    int daemonize;
};

void config_create(struct config* cfg);
void config_destroy(struct config* cfg);
int config_parse_cmdline(struct config* cfg, int argc, char* const* argv);
int config_load(struct config* cfg, const char* filename);

#endif
