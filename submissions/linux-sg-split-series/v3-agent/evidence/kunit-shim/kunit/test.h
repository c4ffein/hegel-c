/* Minimal userspace KUnit shim: just enough to compile and run
** work/v3/lib/tests/sg_split_kunit.c against a userspace sg_split.c build.
** EXPECT failures mark the case failed; ASSERT failures also abort it (longjmp),
** as kunit_try_catch_throw() does.  Deferred actions run at case end. */
#ifndef KUNIT_SHIM_TEST_H
#define KUNIT_SHIM_TEST_H
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>

typedef void (kunit_action_t)(void *);
struct kunit {
	const char *name;
	int failed, skipped;
	jmp_buf jb;
	kunit_action_t *act[64];
	void *actx[64];
	int nact;
	void *mem[256];
	int nmem;
};
struct kunit_case { void (*run_case)(struct kunit *); const char *name; };
struct kunit_suite { const char *name; struct kunit_case *test_cases; };
#define KUNIT_CASE(f) { .run_case = f, .name = #f }
#define kunit_test_suite(s) struct kunit_suite *kunit_shim_suite = &(s)
#define MODULE_DESCRIPTION(x)
#define MODULE_LICENSE(x)

#define KU_CHECK(t, cond, abort_, fmt, ...) do {				\
	if (!(cond)) {							\
		(t)->failed = 1;					\
		fprintf(stderr, "    %s:%d: %s: " fmt "\n", __FILE__,	\
			__LINE__, (t)->name, ##__VA_ARGS__);		\
		if (abort_)						\
			longjmp((t)->jb, 1);				\
	}								\
} while (0)
#define KU_V(x) ((long long)(x))
#define KU_BIN(t, a, op, b, ab) KU_CHECK(t, (a) op (b), ab, "%s %s %s (%lld vs %lld)", #a, #op, #b, KU_V(a), KU_V(b))
#define KUNIT_EXPECT_EQ(t, a, b)			KU_BIN(t, a, ==, b, 0)
#define KUNIT_ASSERT_EQ(t, a, b)			KU_BIN(t, a, ==, b, 1)
#define KUNIT_ASSERT_LE(t, a, b)			KU_BIN(t, a, <=, b, 1)
#define KUNIT_EXPECT_EQ_MSG(t, a, b, fmt, ...)	KU_CHECK(t, (a) == (b), 0, fmt, ##__VA_ARGS__)
#define KUNIT_ASSERT_EQ_MSG(t, a, b, fmt, ...)	KU_CHECK(t, (a) == (b), 1, fmt, ##__VA_ARGS__)
#define KUNIT_EXPECT_NE_MSG(t, a, b, fmt, ...)	KU_CHECK(t, (a) != (b), 0, fmt, ##__VA_ARGS__)
#define KUNIT_EXPECT_LE_MSG(t, a, b, fmt, ...)	KU_CHECK(t, (a) <= (b), 0, fmt, ##__VA_ARGS__)
#define KUNIT_ASSERT_GE_MSG(t, a, b, fmt, ...)	KU_CHECK(t, (a) >= (b), 1, fmt, ##__VA_ARGS__)
#define KUNIT_EXPECT_PTR_EQ(t, a, b)		KU_CHECK(t, (void *)(a) == (void *)(b), 0, "%s == %s", #a, #b)
#define KUNIT_ASSERT_NOT_NULL(t, p)		KU_CHECK(t, (p) != NULL, 1, "%s != NULL", #p)
#define KUNIT_ASSERT_NOT_NULL_MSG(t, p, fmt, ...)	KU_CHECK(t, (p) != NULL, 1, fmt, ##__VA_ARGS__)
#define kunit_skip(t, fmt, ...) do { (t)->skipped = 1; longjmp((t)->jb, 1); } while (0)

static inline void *kunit_kzalloc(struct kunit *t, size_t n, gfp_t gfp)
{
	size_t sz = (n + 4095) & ~(size_t)4095;
	void *p = aligned_alloc(4096, sz ? sz : 4096);
	(void)gfp;
	if (p) memset(p, 0, sz ? sz : 4096);
	if (p && t->nmem < 256) t->mem[t->nmem++] = p;
	return p;
}
static inline int kunit_add_action_or_reset(struct kunit *t, kunit_action_t *a, void *ctx)
{
	if (t->nact >= 64) { a(ctx); return -12; }
	t->act[t->nact] = a;
	t->actx[t->nact++] = ctx;
	return 0;
}
#define KUNIT_DEFINE_ACTION_WRAPPER(wrapper, orig, arg_type) \
	static void wrapper(void *in) { orig((arg_type)in); }
#endif
