/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Minimal rhashtable shim for compiling lib/objagg.c against the
** radix-tree harness.  objagg uses only 5 rhashtable functions:
**   rhashtable_init, rhashtable_destroy, rhashtable_lookup_fast,
**   rhashtable_insert_fast, rhashtable_remove_fast.
** We implement them as a flat singly-linked list (correct, not fast).
** This is correctness-shim territory — the property test never sees
** more than ~32 objects so O(N) lookup is fine. */

#ifndef HEGEL_OBJAGG_RHASHTABLE_SHIM_H
#define HEGEL_OBJAGG_RHASHTABLE_SHIM_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/list.h>
#include <string.h>

struct rhash_head {
	struct rhash_head *next;
};

struct rhashtable_params {
	u16 nelem_hint;
	u16 key_len;
	u16 key_offset;
	u16 head_offset;
	unsigned int max_size;
	u16 min_size;
	bool insecure_elasticity;
	bool automatic_shrinking;
	void *hashfn;
	void *obj_hashfn;
	void *obj_cmpfn;
};

struct rhashtable {
	struct rhash_head *head;
	struct rhashtable_params p;
};

static inline int rhashtable_init(struct rhashtable *ht,
				  const struct rhashtable_params *params)
{
	ht->head = NULL;
	ht->p = *params;
	return 0;
}

static inline void rhashtable_destroy(struct rhashtable *ht)
{
	(void)ht;
	/* objagg removes all entries before destroy; nothing to free here. */
}

static inline void *_rht_obj(const struct rhashtable *ht,
			     const struct rhash_head *he)
{
	return (char *)he - ht->p.head_offset;
}

static inline const void *_rht_key(const struct rhashtable *ht,
				   const struct rhash_head *he)
{
	return (const char *)_rht_obj(ht, he) + ht->p.key_offset;
}

static inline void *rhashtable_lookup_fast(struct rhashtable *ht,
					   const void *key,
					   const struct rhashtable_params params)
{
	struct rhash_head *cur;
	(void)params;
	for (cur = ht->head; cur; cur = cur->next) {
		if (memcmp(_rht_key(ht, cur), key, ht->p.key_len) == 0)
			return _rht_obj(ht, cur);
	}
	return NULL;
}

static inline int rhashtable_insert_fast(struct rhashtable *ht,
					 struct rhash_head *obj,
					 const struct rhashtable_params params)
{
	struct rhash_head *cur;
	const void *key = (const char *)_rht_obj(ht, obj) + ht->p.key_offset;
	(void)params;
	/* duplicate-key check */
	for (cur = ht->head; cur; cur = cur->next) {
		if (memcmp(_rht_key(ht, cur), key, ht->p.key_len) == 0)
			return -EEXIST;
	}
	obj->next = ht->head;
	ht->head = obj;
	return 0;
}

static inline int rhashtable_remove_fast(struct rhashtable *ht,
					 struct rhash_head *obj,
					 const struct rhashtable_params params)
{
	struct rhash_head **slot;
	(void)params;
	for (slot = &ht->head; *slot; slot = &(*slot)->next) {
		if (*slot == obj) {
			*slot = obj->next;
			obj->next = NULL;
			return 0;
		}
	}
	return -ENOENT;
}

#endif
