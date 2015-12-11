#ifndef _cmdoptions_h_
#define _cmdoptions_h_

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
