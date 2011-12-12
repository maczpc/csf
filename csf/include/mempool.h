#ifndef _MEMPOOL_H_
#define _MEMPOOL_H_
#ifdef __cplusplus
extern "C" {
#endif

void mp_init(size_t, size_t);
void *mp_malloc(size_t);
void *mp_calloc(size_t, size_t);
void mp_free(void *);

#ifdef __cplusplus
}
#endif

#endif

