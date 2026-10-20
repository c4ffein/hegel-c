/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Adapter for testing real kernel lib/ts_kmp.c without compiling
** lib/textsearch.c (which pulls in rculist/rcupdate/spinlock — too
** much harness churn for one tiny target).
**
** Strategy: #include ts_kmp.c into this same TU, then expose extern
** wrappers that invoke kmp_ops.init / .find directly.  This bypasses
** textsearch_register / textsearch_prepare entirely while exercising
** the actual kmp_init / kmp_find / compute_prefix_tbl code paths. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Include the real kernel source (single-TU compilation). */
#include "ts_kmp.c"

/* Mirror of textsearch_find_continuous's get_linear_data — we need it
** exposed because the real one lives in lib/textsearch.c which we don't
** compile. */
struct ts_linear_state_userspace
{
	unsigned int	len;
	const void	*data;
};

static unsigned int adapter_get_next_block(unsigned int consumed, const u8 **dst,
                                           struct ts_config *conf,
                                           struct ts_state *state)
{
	(void)conf;
	/* state->cb is char[48] starting at offset 4 of struct ts_state, so
	** it has only 4-byte alignment.  Use memcpy to dodge UBSAN's strict
	** alignment check (the kernel's textsearch_find_continuous casts
	** directly — same latent issue, masked by no -fsanitize=undefined). */
	struct ts_linear_state_userspace st;
	memcpy(&st, state->cb, sizeof(st));

	if (consumed < st.len) {
		*dst = (const u8 *)st.data + consumed;
		return st.len - consumed;
	}
	return 0;
}

/* Public entry for the test: pattern + len + data + len -> offset or
** UINT_MAX.  Mirrors textsearch_find_continuous. */
unsigned int ts_kmp_search(const void *pattern, unsigned int plen,
                           const void *data,    unsigned int dlen)
{
	struct ts_config *conf;
	struct ts_state state;
	unsigned int pos;

	conf = kmp_ops.init(pattern, plen, GFP_KERNEL, 0 /* flags */);
	if (!conf || IS_ERR(conf))
		return UINT_MAX;

	conf->ops = &kmp_ops;
	conf->get_next_block = adapter_get_next_block;
	{
		struct ts_linear_state_userspace tmp = { .len = dlen, .data = data };
		memcpy(state.cb, &tmp, sizeof(tmp));
	}

	state.offset = 0;
	pos = kmp_ops.find(conf, &state);

	/* Free.  The kernel uses kfree(conf); we have one too. */
	kfree(conf);
	return pos;
}
