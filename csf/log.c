/*-
 * Copyright (c) 2007-2009 SINA Corporation, All Rights Reserved.
 *	  
 * CSF logger: Zhang shuo <zhangshuo@staff.sina.com.cn>
 *
 * An logger implementation of csf.
 */

#include <dlfcn.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <syslog.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>


#include "utils.h"
#include "log.h"
#include "confparser.h"

#define DEFAULT_WLOG_METHOD	  	"syslog"
#define DEFAULT_WLOG_PATH		"./"
#define DEFAULT_NAME_PATTERN	"csflog_"
#define DEFAULT_TIMEZONE		"UTC"
#define DEFAULT_TPF			 	"hour"
#define MAX_PATH_LEN			256
#define MAX_MSG_LEN			 	512
#define HOST_NAME_MAXLEN		255

#define CSF_LOGGER  	1
#define OS_SYSLOG   	0
#define WLOG_UTC	 	0
#define WLOG_LOCAL   	1

#define WLOG_HOUR		0
#define WLOG_DAY	 	1
#define WLOG_MONTH   	2

#define LOG_CREATE_FAILED	-1
#define LOG_CREATE_OK		0

typedef struct log_handle
{
	int csf_log_method;
	int csf_log_timezone;
	int csf_log_tpf;
	int is_syslog;
	pid_t pid;
	FILE *log_fd;
	
	char *log_path;
	char *log_filenameprefix;
	char *log_ident;
	char *host_name;

	uint32_t current_time_point;
	
} LOG_HANDLE;


static LOG_HANDLE *local_handle = NULL;

/* fucntion delaration */
static inline struct tm *get_time(void);
static inline int create_logfile(void);
static inline void check_timepoint(void);


/* global variables */
static int csf_log_method = OS_SYSLOG;
static int csf_log_timezone = WLOG_UTC;
static int csf_log_tpf = WLOG_HOUR;
FILE *log_fd = NULL;
static int is_syslog = 1;

/* 
 * current time point
 * 0x00_00_00_00
 * first 00: month number; DEC: 0-11
 * second 00: monthday number;  DEC: 1-31
 * third 00: hour number; DEC: 0-23
 * forth 00: reserved.
 */
//static uint32_t current_time_point = 0x00000000;
static pid_t pid;

static char log_method[CONF_ITEM_LEN + 1] = DEFAULT_WLOG_METHOD;
static char log_path[CONF_ITEM_LEN + 1] = DEFAULT_WLOG_PATH;
static char log_filenameprefix[CONF_ITEM_LEN + 1] = DEFAULT_NAME_PATTERN;
static char log_timezone[CONF_ITEM_LEN + 1] = DEFAULT_TIMEZONE;
static char log_timeperfile[CONF_ITEM_LEN + 1] = DEFAULT_TPF;
static char log_ident[MAX_MSG_LEN + 1];
static char host_name[HOST_NAME_MAXLEN + 1];

static struct conf_str_config conf_str_array[] = {
	{"log_method", log_method},
	{"log_path", log_path},
	{"log_filenameprefix", log_filenameprefix},
	{"log_timezone", log_timezone},
	{"log_timeperfile", log_timeperfile},
	{NULL, NULL}
};

static struct conf_int_config conf_int_array[] = {
	{NULL, NULL}
};


void * 
logger_init(void *handle, const char *ident, int logopt, int facility)
{
	int res, _res;
	char *hp;
	LOG_HANDLE *lhp;
	
	if (handle == NULL) {
	
		/* parse the config file */
		res = load_conf(NULL, "server", NULL, conf_str_array);
		_res = load_conf(NULL, "server", conf_int_array, NULL);

		if (res != 0 || _res != 0) {
			PRINT("ERROR! Can not parse the logger configuration.");
			return NULL;
		}	

		if (log_fd != NULL)
			fclose(log_fd);
		
		lhp = (LOG_HANDLE *)malloc(sizeof(LOG_HANDLE));
	
		if (lhp == NULL) {
			PRINT("logger init failed. not enough memory.");
			return NULL;
		}	
		local_handle = lhp;
		
		lhp->log_path = log_path;
		lhp->log_filenameprefix = log_filenameprefix;
		lhp->log_ident = log_ident;
		lhp->host_name = host_name;

		if (strncmp("csflogger", log_method, 1) == 0) {
			csf_log_method = CSF_LOGGER;

			/* set the log ident */
			memset(log_ident, '\0', MAX_MSG_LEN + 1);
			strlcpy(log_ident, ident, MAX_MSG_LEN + 1);

			/* get the current pid */
			pid = getpid();

			/* get the current host name(not full domain name) */
			memset(host_name, '\0', HOST_NAME_MAXLEN + 1);
		
			if (gethostname(host_name, HOST_NAME_MAXLEN) == 0 &&
				host_name != NULL) {

				hp = host_name;
				while (*hp != '\0') {
					if (*hp == '.') {
						*hp = '\0';
						break;
					}
					hp++;
				}
			} else {
				memmove(host_name, "NULL", 5);
			}

			/* get time per file */
			switch (log_timeperfile[0]) {
				case 'm': csf_log_tpf = WLOG_MONTH; break;
				case 'd': csf_log_tpf = WLOG_DAY; break;
				case 'h': csf_log_tpf = WLOG_HOUR; break;
				default : csf_log_tpf = WLOG_HOUR; break;
			}

			/* get the log time zone */
			if (log_timezone[0] == 'U') {
				csf_log_timezone = WLOG_UTC;
			} else {
				csf_log_timezone = WLOG_LOCAL;
			}

			/* point to current logger function */
			is_syslog = 0;

			/* create and open the csf build-in log file */
			if (create_logfile() != LOG_CREATE_OK) {
				PRINT("ERROR! Can not create log file.");
				return NULL;
			}
		} else {
			/* point to current logger function */
			is_syslog = 1;

			/* open the syslog */
			openlog(ident, logopt, facility);
		}
	
		lhp->csf_log_method = csf_log_method;
		lhp->csf_log_timezone = csf_log_timezone;
		lhp->csf_log_tpf = csf_log_tpf;
		lhp->is_syslog = is_syslog;
		lhp->pid = pid;
	}
	else {
		local_handle = (LOG_HANDLE *)handle;
	}

	return local_handle;
}


/* 
 * the log entry. which is used to write the log on the disk
 */
void 
write_log(int priority, const char *message, ...)
{
	char msg[MAX_MSG_LEN + 1];
	int printed_size = 0;
	size_t msg_len = 0;
	char *asc_time;	
	va_list ap;

    if (local_handle == NULL) {
        PRINT("logger handle is NULL.");
        return;
    }
	
	if (local_handle->is_syslog) {
		va_start(ap, message);
		vsnprintf(msg, MAX_MSG_LEN, message, ap);
		va_end(ap);
		syslog(priority, "%s", msg);
	} else {   
		/* check the timepoint determine wether create new log file */
		check_timepoint();
		
		asc_time = asctime(get_time());
		asc_time[strlen(asc_time) - 1] = '\0';  /* delete the last '\n' */
		
		printed_size = snprintf(msg, MAX_MSG_LEN, "%s %s %s[%u]: ", asc_time, 
								local_handle->host_name, local_handle->log_ident, 
								(unsigned int)(local_handle->pid));
		va_start(ap, message);
		vsnprintf(msg + printed_size, MAX_MSG_LEN, message, ap);
		va_end(ap);

		/* add an \n behind every record */
		msg_len = strlen(msg);
		if (msg_len > 0 && msg_len < MAX_MSG_LEN)
		{
			msg[msg_len] = '\n';
		}
		
		//write(log_fd, msg, msg_len + 1);

		fwrite(msg, msg_len + 1, 1, local_handle->log_fd);
		fflush(local_handle->log_fd);
	}
}


/*
 * check the time point and determine weather to create a new log file
 * the time point is an uint32_t, which indicates the create time stamp
 * of current used log file.
 */
static inline void
check_timepoint()
{
	struct tm *timefields;
	timefields = get_time();
	uint32_t tp_month = 0x0;
	uint32_t tp_mday = 0x0;
	uint32_t tp_hour = 0x0;
	
	switch (local_handle->csf_log_tpf) {
		case WLOG_MONTH : 
			tp_month = ((uint32_t)timefields->tm_mon) << 24;
			if ((local_handle->current_time_point & 0xFF000000) != tp_month) {
				fclose(local_handle->log_fd);
				create_logfile();
			}
			break;

		case WLOG_DAY : 
			tp_mday = ((uint32_t)timefields->tm_mday) << 16;
			if ((local_handle->current_time_point & 0x00FF0000) != tp_mday) {
				fclose(local_handle->log_fd);
				create_logfile();
			}
			break;

		case WLOG_HOUR : 
			tp_hour = ((uint32_t)timefields->tm_hour) << 8;
			if ((local_handle->current_time_point & 0x0000FF00) != tp_hour) {
				fclose(local_handle->log_fd);
				create_logfile();
			}
			break;
	}
}


/*
 * this function is used to generate the file name and then create log file
 */
static inline int 
create_logfile()
{
	char filepath[MAX_PATH_LEN + 1];
	char weekday[10];
	struct tm *timefields;
	uint32_t tp_month = 0x0;
	uint32_t tp_mday = 0x0;
	uint32_t tp_hour = 0x0;

	timefields = get_time();

	switch (timefields->tm_wday) {
		case 0 : memmove(weekday, "Sun", 4); break;
		case 1 : memmove(weekday, "Mon", 4); break;
		case 2 : memmove(weekday, "Tue", 4); break;
		case 3 : memmove(weekday, "Wen", 4); break;
		case 4 : memmove(weekday, "Thr", 4); break;
		case 5 : memmove(weekday, "Fri", 4); break;
		case 6 : memmove(weekday, "Sat", 4); break;
		default : memmove(weekday, "Err", 4); break;
	}

	/* set the time point for creating new file */
	tp_month = ((uint32_t)timefields->tm_mon) << 24;
	tp_mday = ((uint32_t)timefields->tm_mday) << 16;
	tp_hour = ((uint32_t)timefields->tm_hour) << 8;
	
	local_handle->current_time_point = tp_month | tp_mday | tp_hour;

	/* contribute the file path */
	snprintf(filepath, MAX_PATH_LEN, "%s%s_%d%02d%02d_%s_%d.log", local_handle->log_path,
			local_handle->log_filenameprefix, timefields->tm_year + 1900, timefields->tm_mon + 1, 
			timefields->tm_mday, weekday, timefields->tm_hour);

	local_handle->log_fd = fopen(filepath, "a"); 
	
	//log_fd = open(filepath, O_SYNC | O_WRONLY | O_APPEND | O_CREAT);

	if (local_handle->log_fd == NULL)
		return LOG_CREATE_FAILED;
	else
		return LOG_CREATE_OK;
}


void 
logger_deinit()
{
	if (local_handle->is_syslog) 
		closelog();
	else
		fclose(local_handle->log_fd);
}

/* free the timefields struct? */

static inline struct tm *  
get_time()
{
	time_t timeticks;
	struct tm *timefields;

	timeticks = time(NULL);
	
	if (csf_log_timezone == WLOG_UTC) {
		timefields = gmtime(&timeticks);
	} else {
		timefields = localtime(&timeticks);
	}

	return timefields;
}




