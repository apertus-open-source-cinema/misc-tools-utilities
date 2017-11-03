#ifndef _cmdoptions_h_
#define _cmdoptions_h_

/*
 * Copyright (C) 2016 a1ex
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

struct cmd_option
{
    int* variable;              /* can be float */
    int value_to_assign;        /* if the option field contains %d or %f, set this to number of %'s */
    char* option;               /* can contain %d or %f for options with values */
    char* help;
    int _active;                /* internal flag, true if the option was set from command line */
};
#define OPTION_EOL { 0, 0, 0, 0 }

struct cmd_group
{
    char* name;
    struct cmd_option * options;
};
#define OPTION_GROUP_EOL { 0, 0 }

/* you have to declare this in your program */
extern struct cmd_group options[];

/* print help for command-line options */
void show_commandline_help(char* progname);

/* parse a single option from command line */
void parse_commandline_option(char* option);

/* print active options from command line */
void show_active_options();

#endif
