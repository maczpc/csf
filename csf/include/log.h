#ifndef _CSF_WLOG_H
#define _CSF_WLOG_H

#include <syslog.h>
#include <stdio.h>
#include <sys/types.h>

#define DEFAULT_WLOG_IDENT "csf_log"
#define PRINT(fmt, arg...) \
		printf("%s[%d]: "#fmt"\n", __func__, __LINE__, ##arg)

		
void write_log(int priority, const char *message, ...);
void *logger_init(void *, const char *, int, int);
void logger_deinit(void);


#ifdef WLOG_

#define WLOG_EMERG(fmt, arg...) \
write_log(LOG_EMERG|LOG_LOCAL6, "%s[%d]: "#fmt, __func__, __LINE__, ##arg)

#define WLOG_ALERT(fmt, arg...) \
write_log(LOG_ALERT|LOG_LOCAL6, "%s[%d]: "#fmt, __func__, __LINE__, ##arg)

#define WLOG_CRIT(fmt, arg...) \
write_log(LOG_CRIT|LOG_LOCAL6, "%s[%d]: "#fmt, __func__, __LINE__, ##arg)

#define WLOG_ERR(fmt, arg...) \
write_log(LOG_ERR|LOG_LOCAL6, "%s[%d]: "#fmt, __func__, __LINE__, ##arg)

#define WLOG_WARNING(fmt, arg...) \
write_log(LOG_WARNING|LOG_LOCAL6, "%s[%d]: "#fmt, __func__, __LINE__, ##arg)

#ifdef DEBUG_
#define WLOG_NOTICE(fmt, arg...) \
write_log(LOG_NOTICE|LOG_LOCAL6, "%s[%d]: "#fmt, __func__, __LINE__, ##arg)
#define WLOG_INFO(fmt, arg...) \
write_log(LOG_INFO|LOG_LOCAL6, "%s[%d]: "#fmt, __func__, __LINE__, ##arg)
#define WLOG_DEBUG(fmt, arg...) \
write_log(LOG_DEBUG|LOG_LOCAL6, "%s[%d]: "#fmt, __func__, __LINE__, ##arg) 
#else
#define WLOG_NOTICE(fmt, arg...)	do{}while(0)
#define WLOG_INFO(fmt, arg...)		do{}while(0)
#define WLOG_DEBUG(fmt, arg...)		do{}while(0)
#endif

#else

#define WLOG_OPEN(ident)			do{}while(0)  /* do nothing but avoid warning of compiler */
#define WLOG_CLOSE(handle)			do{}while(0)

#define WLOG_EMERG(fmt, arg...)		do{}while(0)
#define WLOG_ALERT(fmt, arg...)		do{}while(0)
#define WLOG_CRIT(fmt, arg...)		do{}while(0)
#define WLOG_ERR(fmt, arg...)		do{}while(0)
#define WLOG_WARNING(fmt, arg...)	do{}while(0)
#define WLOG_NOTICE(fmt, arg...)	do{}while(0)
#define WLOG_INFO(fmt, arg...)		do{}while(0)
#define WLOG_DEBUG(fmt, arg...)		do{}while(0)
#endif 

#define ast(e)							\
do { 									\
	if (!(e))							\
		WLOG_ERR("Error in %s(), %s:%d",__func__, __FILE__, __LINE__);	\
} while (0)

/* Assert zero return value */
#define AZ(foo)	do { ast((foo) == 0); } while (0)
#define AN(foo)	do { ast((foo) != 0); } while (0)

#endif

