/* Runs the v3 KUnit suite (work/v3/lib/tests/sg_split_kunit.c) in userspace through
** work/v3/kunit_shim, against any sg_split.c:
**   make bin T=work/v3/test_kunit_v3.c SRC=work/v3/lib/sg_split.c OUT=work/v3/kunit_v3 \
**        CFLAGS="-O1 -g -w -D_LGPL_SOURCE -I$PWD/work/v3/kunit_shim"   (+ -DCONFIG_NEED_SG_DMA_LENGTH)
**   ./work/v3/kunit_v3        -- one line per case; each case runs in a forked child. */
#include <unistd.h>
#include <sys/wait.h>
#include "lib/tests/sg_split_kunit.c"

extern struct kunit_suite *kunit_shim_suite;

int main(void)
{
	int fails = 0, n = 0;
	for (struct kunit_case *c = kunit_shim_suite->test_cases; c->run_case; c++, n++) {
		fflush(stdout);
		pid_t p = fork();
		if (!p) {
			struct kunit t = { .name = c->name };
			if (!setjmp(t.jb))
				c->run_case(&t);
			for (int i = t.nact - 1; i >= 0; i--)
				t.act[i](t.actx[i]);
			for (int i = 0; i < t.nmem; i++)
				free(t.mem[i]);
			_exit(t.skipped ? 2 : t.failed ? 1 : 0);
		}
		int st;
		waitpid(p, &st, 0);
		const char *v = !WIFEXITED(st) ? "CRASH" : WEXITSTATUS(st) == 0 ? "ok"
			: WEXITSTATUS(st) == 2 ? "skip" : "FAIL";
		fails += strcmp(v, "ok") && strcmp(v, "skip");
		printf("%-5s %s\n", v, c->name);
	}
	printf("kunit-replay: %d cases, %d failed/crashed\n", n, fails);
	return !!fails;
}
