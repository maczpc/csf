#ifndef _CSF_CONF_PARSER_H
#define _CSF_CONF_PARSER_H

#include "queue.h"
#include "iniparser.h"

#define CONF_ITEM_LEN	63

typedef struct conf_int_config {
    const char	*config_name;
    int			*var;
} CONF_INT_CONFIG;

typedef struct conf_str_config {
    const char	*config_name;
    char		*var;
} CONF_STR_CONFIG;

void set_conf_file(const char *conf_name);
dictionary *open_conf_file(const char *conf_name);
dictionary *open_conf_mem(const char *ini, int len);
int parse_conf_file(dictionary *, const char *, CONF_INT_CONFIG[], CONF_STR_CONFIG[]);
void close_conf_file(dictionary *);
int load_conf(char *, const char *, CONF_INT_CONFIG[], CONF_STR_CONFIG[]);
int load_conf_mem(const char *, int, const char *, CONF_INT_CONFIG cic[], CONF_STR_CONFIG csc[]);

#endif

