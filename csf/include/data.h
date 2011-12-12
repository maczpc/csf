#ifndef _CSF_DATA_H
#define _CSF_DATA_H

#include "server.h"

int connection_made(CONN_STATE *);
int data_received(CONN_STATE *, void *, int);
int connection_lost(CONN_STATE *);

#endif

