/* Watchdog for tools/mutate.py drivers: a mutant that hangs (or makes the run crawl
** through fork-mode shrinking) is stopped after MUT_SECONDS: the driver puts itself in
** its own process group and SIGKILLs the whole group (fork children included), which
** mutate.py sees as a non-zero exit, i.e. "killed". The unmutated runs take ~2 s. */
#include <signal.h>
#include <unistd.h>
#ifndef MUT_SECONDS
#define MUT_SECONDS 90
#endif
static void mut_alarm (int s) { (void) s; kill (0, SIGKILL); }
static void mut_guard (void) { setpgid (0, 0); signal (SIGALRM, mut_alarm); alarm (MUT_SECONDS); }
