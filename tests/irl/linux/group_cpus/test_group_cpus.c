/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* lib/group_cpus.c, non-SMP build: group_cpus_evenly(n) splits the possible
** CPUs into n groups. Contract (include/linux/group_cpus.h and its callers):
** returns an array of n masks the caller kfree()s, or NULL on failure; the
** groups together cover every possible CPU. */
#include <stdio.h>
#include "hegel_c.h"
#include "group_cpus_compat.h"

extern struct cpumask *group_cpus_evenly(unsigned int numgrps);

static void prop_groups_cover_possible(hegel_testcase *tc) {
  unsigned int n = (unsigned int)hegel_draw_int(tc, 0, 4 * NR_CPUS);
  struct cpumask *masks = NULL;
  struct cpumask all = { { 0 } };
  char msg[64];

  snprintf(msg, sizeof msg, "numgrps = %u", n);
  hegel_note(tc, msg);
  HEGEL_ISOLATED(tc) {
    masks = group_cpus_evenly(n);
    if (masks && n > 0) {
      for (unsigned int g = 0; g < n; g++)
        for (size_t w = 0; w < sizeof(all.bits) / sizeof(all.bits[0]); w++)
          all.bits[w] |= masks[g].bits[w];
      HEGEL_ASSERT(memcmp(&all, cpu_possible_mask, sizeof all) == 0,
                   "the %u groups don't cover every possible CPU", n);
    }
    kfree(masks);
  }
}

int main(void) { return hegel_run_test_result(prop_groups_cover_possible); }
