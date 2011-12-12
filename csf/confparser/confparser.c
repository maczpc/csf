#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h> 

#include "iniparser.h"
#include "confparser.h"

#ifndef MAX_PATH
#define MAX_PATH 1024
#endif

char conf_file[MAX_PATH];

void set_conf_file(const char *conf_name)
{
	if (conf_name != NULL) {
		strlcpy(conf_file, conf_name, MAX_PATH);
    }
    else {
        printf("Configure file invalid.\n");
    }
}

dictionary *
open_conf_file(const char *conf_name)
{
	char *cn = NULL;

	if (conf_name != NULL)
		cn = (char *)conf_name;
	else
		cn = conf_file;

    return iniparser_load((char *)cn);
}

dictionary *
open_conf_mem(const char *ini, int len)
{
	if (ini != NULL)
    	return iniparser_load_mem(ini, len);
	else
		return NULL;
}


int 
parse_conf_file(dictionary *conf,
				const char *mod_name,
                CONF_INT_CONFIG cic[], 
                CONF_STR_CONFIG csc[])
{
    /* Some temporary variables to hold query results */
    int i;
    char *s;
	char buf[CONF_ITEM_LEN + 1];
    const CONF_INT_CONFIG* cicp;
	const CONF_STR_CONFIG* cscp;

	if (conf == NULL || mod_name == NULL)
		return (-1);

	/* Get the int configure */
    for (cicp = cic;
        (cicp != NULL) && (cicp->config_name != 0);
        cicp++) {
		snprintf(buf, CONF_ITEM_LEN, "%s:%s", mod_name, cicp->config_name);
        i = iniparser_getint(conf, (char *)(buf), -1);
         
        if (i != -1) {
            *(cicp->var) = i;
        } else {
        	printf("%s: can not get [%s]->%s, default used.\n", 
				__func__, mod_name, cicp->config_name);
        }
    }

	/* Get the string configure */
    for (cscp = csc;
        (cscp != NULL) && (cscp->config_name != 0);
        cscp++) {
		snprintf(buf, CONF_ITEM_LEN, "%s:%s", mod_name, cscp->config_name);
        s = iniparser_getstr(conf, (char *)(buf));
        if (s) {
            strlcpy(cscp->var, s, CONF_ITEM_LEN + 1);
        } else {
        	printf("%s: can not get [%s]->%s, default used.\n",
				__func__, mod_name, cscp->config_name);
        }
    }
    return (0);
}

void
close_conf_file(dictionary *conf)
{
    iniparser_freedict(conf);
}

int
load_conf(char *conf_name, const char *mod_name, CONF_INT_CONFIG cic[],
                CONF_STR_CONFIG csc[])
{
	dictionary	*conf;

	conf = open_conf_file(conf_name);
	if (conf == NULL) {
		return (-1);
	}
	
	parse_conf_file(conf, mod_name, cic, csc);
	close_conf_file(conf);

	return (0);
}

int
load_conf_mem(const char *ini, int len, const char *mod_name, CONF_INT_CONFIG cic[],
                CONF_STR_CONFIG csc[])
{
	dictionary	*conf;

	conf = open_conf_mem(ini,len);
	if (conf == NULL) {
		return (-1);
	}
	
	parse_conf_file(conf, mod_name, cic, csc);
	close_conf_file(conf);

	return (0);
}

