/*-
 * Copyright (C) 2007-2009 SINA Corporation, All Rights Reserved.
 *  Authors:
 *	  Zhu Yan <zhuyan@staff.sina.com.cn>
 * Pipeline stage jumper Red-Black-Tree Implementation
 */


#include <stdlib.h>
#include <string.h>

#include "tree.h"
#include "pipeline.h"
#include "pipeline_def.h"
//#include "common.h"
#include "utils.h"

RB_HEAD(STAGE_RQCB_RBT, rqcb);
struct STAGE_RQCB_RBT *stage_rqcb_rbt;

static int name_cmp(RQCB *, RQCB *);

void *stage_init(void *);
int stage_add(RQCB *);

RB_PROTOTYPE_STATIC(STAGE_RQCB_RBT, rqcb, stage_entry, name_cmp);
RB_GENERATE_STATIC(STAGE_RQCB_RBT, rqcb, stage_entry, name_cmp);

static int
name_cmp(RQCB *a, RQCB *b)
{
	int diff = strncmp(a->mod_name, b->mod_name, MAX_MOD_NAME_LEN - 1);
	if (diff == 0)
		return (0);
	else if (diff < 0)
		return (-1);
	else
		return (1);
}

void *
stage_init(void *p)
{
	if (p == NULL) {
		stage_rqcb_rbt = malloc(sizeof(struct STAGE_RQCB_RBT));
		if (stage_rqcb_rbt == NULL) {
			CRIT("No Memory");
		}

		RB_INIT(stage_rqcb_rbt);
	} else {
		stage_rqcb_rbt = p;
	}
	
	return (void *)stage_rqcb_rbt;
}

int
stage_add(RQCB *rqcbp)
{
	RQCB *p;

	p = RB_INSERT(STAGE_RQCB_RBT, stage_rqcb_rbt, rqcbp);

	if (p == NULL) {
		return (STAGE_ADD_OK);
	} else {
		return (STAGE_ADD_ERR);
	}
}

static RQCB *
stage_find(char *name)
{
	RQCB find;

	strlcpy(find.mod_name, name, MAX_MOD_NAME_LEN);
	
	return RB_FIND(STAGE_RQCB_RBT, stage_rqcb_rbt, &find);
}

int
set_next_stage(WTI *, char *);

int
set_next_stage(WTI *wtip, char *stage_name)
{
	RQCB *rqcbp;

	rqcbp = stage_find(stage_name);

	if (rqcbp == NULL) {
		return (STAGE_SET_ERR);
	}

	wtip->next_rqcbp = rqcbp;
	return (STAGE_SET_OK);
}

