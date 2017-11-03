/**
 * Simple command-line options library that reinvents the wheel
 *
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

#include "cmdoptions.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

static int startswith(char* str, char* prefix)
{
    char* s = str;
    char* p = prefix;
    for (; *p; s++,p++)
        if (*s != *p) return 0;
    return 1;
}

static void parse_sscanf(char* user_input, char* format, void* ptr, int num_vars)
{
    void* pointers[5] = {0, 0, 0, 0, 0};
    if (num_vars > 5) goto err;
    int i;
    char* p = strchr(format, '%');
    for (i = 0; p != NULL && i < num_vars; i++, p = strchr(p+1, '%'))
    {
        //~ printf("%s: %p %p\n", format, ptr, &soft_film_ev);
        pointers[i] = ptr;
        int size =
            *(p+1) == 'd' ? sizeof(int) :
            *(p+1) == 'f' ? sizeof(float) :
            0;
        if (size == 0) goto err;
        ptr += size;
    }
    if (i != num_vars) goto err;

    int num = sscanf(user_input, format, pointers[0], pointers[1], pointers[2], pointers[3], pointers[4]);
    if (num != num_vars)
    {
        printf("Error parsing %s: expected %d param%s, got %d\n", format, num_vars, num_vars == 1 ? "" : "s", num);
        exit(1);
    }

    return;

err:
    printf("invalid option: %s (internal error)\n", format);
    exit(1);
}

static void print_sscanf_option(char* format, void* ptr, int num_vars, char* help)
{
    int i = 0;
    char* p;
    int len = 0;
    for (p = format; *p && i < num_vars; p++)
    {
        if (*p != '%')
        {
            len += printf("%c", *p);
        }
        else
        {
            if (*(p+1) == 'd')
            {
                len += printf("%d", *(int*)ptr);
                ptr += sizeof(float);
            }
            else if (*(p+1) == 'f')
            {
                len += printf("%g", *(float*)ptr);
                ptr += sizeof(int);
            }
            p++; i++;
        }
    }
    while (len < 20)
    {
        len += printf(" ");
    }
    printf(": %s\n", help);
}

void parse_commandline_option(char* option)
{
    struct cmd_group * g;
    for (g = options; g->name; g++)
    {
        struct cmd_option * o;
        for (o = g->options; o->option; o++)
        {
            if (strchr(o->option, '%'))
            {
                char base[100];
                snprintf(base, sizeof(base), "%s", o->option);
                char* percent = strchr(base, '%');
                if (percent)
                {
                    *percent = 0;   /* trim here */
                    if (startswith(option, base))
                    {
                        /* note that o->variable is the array where %d's or %f's are stored */
                        /* and o->value_to_assign is the number of items in that array */
                        parse_sscanf(option, o->option, o->variable, o->value_to_assign);
                        o->_active = 1;
                        return;
                    }
                }
            }
            else if (!strcmp(option, o->option))
            {
                *(o->variable) = o->value_to_assign;
                o->_active = 1;
                return;
            }
        }
    }
    printf("Unknown option: %s\n", option);
    exit(1);
}

void show_commandline_help(char* progname)
{
    struct cmd_group * g;
    for (g = options; g->name; g++)
    {
        printf("%s:\n", g->name);
        struct cmd_option * o;
        for (o = g->options; o->option; o++)
        {
            if (o->help)
            {
                printf("%-20s: %s\n", o->option, o->help);
            }
        }
        printf("\n");
    }
}

static char * first_line(char * msg)
{
    static char buf[100];
    snprintf(buf, sizeof(buf), "%s", msg);
    char* newline = strchr(buf, '\n');
    if (newline) *newline = 0;
    return buf;
}

void show_active_options()
{
    struct cmd_group * g;
    struct cmd_option * o;

    int any_active = 0;
    for (g = options; g->name; g++)
        for (o = g->options; o->option; o++)
            if (o->help && o->_active)
                any_active = 1;

    if (any_active)
    {
        printf("Active options:\n");
    }

    for (g = options; g->name; g++)
    {
        for (o = g->options; o->option; o++)
        {
            if (o->help && o->_active)
            {
                if (strchr(o->option, '%'))
                {
                    /* note that o->variable is the array where %d's or %f's are stored */
                    /* and o->value_to_assign is the number of items in that array */
                    print_sscanf_option(o->option, o->variable, o->value_to_assign, first_line(o->help));
                }
                else
                {
                    printf("%-20s: %s\n", o->option, first_line(o->help));
                }
            }
        }
    }
}
