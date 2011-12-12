/*-
 * Copyright (c) 2007-2008 SINA Corporation, All Rights Reserved.
 *  Authors:
 *      Zhu Yan <zhuyan@staff.sina.com.cn>
 *
 *
 * Memory Pool experimental implementation
 * Notice: This Memory Pool is *NOT* thread-safe now.
 * This pool uses a Red-Black Tree and single linked list to save the memory.
 
           +--+
           +--+
           /  \
          /    \
      +--+      +--+
      +--+      +--+
      /  \          \
     /    \          \
 +--+      +--+       +--+
 +--+      +--+       +--+
                      |    Memory list
                      +-+--+-+--+-+--+-+
                      +-+  +-+  +-+  +-+

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "queue.h"
#include "tree.h"
#include "mempool.h"


struct MP_MEM_ENTRY {
	SLIST_ENTRY(MP_MEM_ENTRY) mem_entries;
	long	size;
};

struct MP_TREE_ENTRY {
	int	mem_size;
	unsigned int total_item;
	SLIST_HEAD(, MP_MEM_ENTRY)	mem_head;
	RB_ENTRY (MP_TREE_ENTRY)	mp_tree_entries;
};

static int
size_cmp(struct MP_TREE_ENTRY *a, struct MP_TREE_ENTRY *b)
{
	int diff = a->mem_size - b->mem_size;

	if (diff == 0)
		return (0);
	else if (diff < 0)
		return (-1);
	else
		return (1);
}

RB_HEAD(MP_TREE, MP_TREE_ENTRY) mp_tree = RB_INITIALIZER(_head);

RB_PROTOTYPE_STATIC(MP_TREE, MP_TREE_ENTRY, mp_tree_entries, size_cmp);
RB_GENERATE_STATIC(MP_TREE, MP_TREE_ENTRY, mp_tree_entries, size_cmp);

static unsigned int mp_initialized = 0;
static unsigned int g_pre_alloc_num = 10;
static unsigned int g_pre_free_num = 20;

void
mp_init(size_t pre_alloc_num, size_t pre_free_num)
{
	if (pre_alloc_num != 0 && pre_free_num != 0) {
		g_pre_alloc_num = pre_alloc_num;
		if (pre_free_num < pre_alloc_num)
			pre_free_num = pre_alloc_num;
		g_pre_free_num = pre_free_num;
	}
	mp_initialized = 1;
}

static int
mp_init_block(struct MP_TREE_ENTRY *tree_entry, long size,unsigned int alloc_num)
{
	struct MP_MEM_ENTRY *mem_entry;
	struct MP_MEM_ENTRY *new_mem_entry;
	unsigned int i;

	for (i = 0; i < alloc_num; i++) {
		new_mem_entry = (struct MP_MEM_ENTRY *)malloc(
						sizeof(struct MP_MEM_ENTRY) + size);
		if (new_mem_entry == NULL) {
			while (!SLIST_EMPTY(&(tree_entry->mem_head))) {
				mem_entry = SLIST_FIRST(&(tree_entry->mem_head));
				SLIST_REMOVE_HEAD(&(tree_entry->mem_head), mem_entries);
				free(mem_entry);
			}

			free(new_mem_entry);
			return (-1);
		}

		new_mem_entry->size = size;
		SLIST_INSERT_HEAD(&(tree_entry->mem_head), new_mem_entry, mem_entries);
		tree_entry->total_item++;
	}
	return (0);
}

static void *
mp_alloc(long size) 
{
	struct MP_TREE_ENTRY find;
	struct MP_TREE_ENTRY *cur_tree_entry;
	struct MP_TREE_ENTRY *new_tree_entry;
	struct MP_MEM_ENTRY *new_mem_entry;
	struct MP_MEM_ENTRY *mem_entry;
	int retval;

	if (!mp_initialized)
		return NULL;

	if (size <= 0)
		return NULL;

	find.mem_size = size;
	cur_tree_entry = RB_FIND(MP_TREE, &mp_tree, &find);

	if (cur_tree_entry == NULL) {
		new_tree_entry = (struct MP_TREE_ENTRY *)calloc(1,
						sizeof(struct MP_TREE_ENTRY));
		if (new_tree_entry == NULL)
			return (NULL);
		new_tree_entry->mem_size = size;
		new_tree_entry->total_item = 0;
		SLIST_INIT(&(new_tree_entry->mem_head));
		if (mp_init_block(new_tree_entry, size, g_pre_alloc_num - 1) < 0)
			return (NULL);
		RB_INSERT(MP_TREE, &mp_tree, new_tree_entry);
	} else {
		if (cur_tree_entry->total_item == 0) {
			SLIST_INIT(&(cur_tree_entry->mem_head));
			retval = mp_init_block(cur_tree_entry,
					cur_tree_entry->mem_size, 
					g_pre_alloc_num - 1);
			if (retval < 0)
				return (NULL);
		} else {
			if (!SLIST_EMPTY(&(cur_tree_entry->mem_head))) {
				mem_entry = SLIST_FIRST(&(cur_tree_entry->mem_head));
				SLIST_REMOVE_HEAD(&(cur_tree_entry->mem_head), mem_entries);
				cur_tree_entry->total_item--;
				return ((void *)(++mem_entry));
			} else {
				printf("alloc: FATAL ERROR!\n");
				return (NULL);
			}
		}
	}
	new_mem_entry = (struct MP_MEM_ENTRY *)calloc(1,
					sizeof(struct MP_MEM_ENTRY) + size);
	if (new_mem_entry == NULL)
		return (NULL);

	new_mem_entry->size = size;
	return ((void *)(++new_mem_entry));
}

static void
mp_pfree(void *ptr)
{
	struct MP_TREE_ENTRY find;
	struct MP_TREE_ENTRY *cur_tree_entry;
	struct MP_MEM_ENTRY *cur_mem_entry;
	struct MP_MEM_ENTRY *mem_entry;

	if (!mp_initialized)
		return;

	if (ptr == NULL)
		return;

	cur_mem_entry = (struct MP_MEM_ENTRY *)
					((unsigned char *)ptr - sizeof(struct MP_MEM_ENTRY));
	find.mem_size = cur_mem_entry->size;
	cur_tree_entry = RB_FIND(MP_TREE, &mp_tree, &find);
	if (cur_tree_entry == NULL) {
		printf("mp_free: FATAL ERROR! Could be memory overflow!, size: %ld \n", 
			cur_mem_entry->size);
		return;
	} else {
		if (cur_tree_entry->total_item > g_pre_free_num) {
			free(cur_mem_entry);
			while (!SLIST_EMPTY(&(cur_tree_entry->mem_head))) {
				mem_entry = SLIST_FIRST(&(cur_tree_entry->mem_head));
				SLIST_REMOVE_HEAD(&(cur_tree_entry->mem_head), mem_entries);
				cur_tree_entry->total_item--;
				free(mem_entry);
				if (cur_tree_entry->total_item == g_pre_free_num)
					break;
			}
			if (cur_tree_entry->total_item != g_pre_free_num)
				printf("mp_free: FATAL ERROR!\n");
		} else {
			SLIST_INSERT_HEAD(&(cur_tree_entry->mem_head),
				cur_mem_entry, mem_entries);
			cur_tree_entry->total_item++;
		}
	}
}

inline void *
mp_malloc(size_t size)
{
	return mp_alloc(size);
}

inline void *
mp_calloc(size_t number, size_t size)
{
	void *ptr;
	size_t num_size;

	num_size = number * size;
	ptr = mp_alloc(num_size);
	if (ptr != NULL)
		memset(ptr, 0, num_size);
	return ptr;
}

inline void
mp_free(void *ptr)
{
	mp_pfree(ptr);
}

/*
int
main(int argc, char* argv[])
{
	int i;
	char *ptr[1024];
	memory_pool_init(100, 200);
	for (i = 0; i < 1024; i++) {
			ptr[i] = mp_alloc(10240);

	}
	for (i = 0; i < 1024; i++) {
		mp_free(ptr[i]);
	}
	getch();
	return 0;
}
*/
