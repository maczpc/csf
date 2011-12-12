#ifndef _CSF_UTILS_H
#define _CSF_UTILS_H

#define smalloc(n) _smalloc(__func__, __LINE__, n)
#define sfree(ptr) _sfree(__func__, __LINE__, ptr)

#define CSF_UNUSED_ARG(a) (void)(a)

void *_smalloc(const char*, int, int);
void _sfree(const char*, int, void *);

#define GET_ENTRY(ptr, type, member) \
	((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))
	
void _crit(const char*, int, const char *);
#define CRIT(msg) _crit(__func__, __LINE__, msg)

int daemonize(int, int);

#define CSF_OK		0
#define CSF_ERR		-1

#endif

