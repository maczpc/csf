/* this header file should only be used in CSF */

#ifndef _MONITOR_H_
#define _MONITOR_H_

#include <stdint.h>

#include "log.h"
#include "queue.h"

#define MNT_DEF_PORT		22222
#define MAX_CLIENT 			10			/* max client allowed */
#define CMD_BUF_SIZE		200								
#define LISTEN_TIME_SPAN	1000000		/* us.time span of fucntion listen_ntimes */
#define MAX_FIELD_LENGTH    21          /* so_name item_name length limit */


/* when there is a positive number, it indicates the
 * types currently supported by the csf monitor */
#define MNT_UINT_64     0x1
#define MNT_INT_64      0x2
#define MNT_UINT_32     0x4
#define MNT_INT_32      0x8
#define MNT_UINT        0x10
#define MNT_INT         0x20
#define MNT_CHAR        0x40
#define MNT_STRING      0x80
#define MNT_FLOAT       0x100
#define MNT_DOUBLE      0x200

/* for fraction show of grouped value */
#define MNT_GROUP_STAT	0x400

/* when there is a negative, it indicates the option
 * of how csf does */

#define MNT_DISREG		-1		/* disregister the value */


void *monitor_init(void *);
void register_stat_int(const char *, const char *, int, void *);

 
#endif

