/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright (c) 2012-2022 Timo Röhling <timo@gaussglocke.de>
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
#include "config.h"
#include "endpoint.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    struct config cfg;
    config_create(&cfg);
    if (!config_parse_cmdline(&cfg, argc, argv))
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
