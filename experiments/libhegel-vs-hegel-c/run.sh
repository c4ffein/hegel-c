# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.
#
# run.sh — run every program of the comparison once per seed and print
# the summary table.  Called by `make run` with LIBHEGEL_SO, OUT, SEEDS.
# Raw outputs land in $OUT/out/<program>[+env].<seed>.txt.
#
# Columns: exit codes over the seeds; executions of the property body
# (count_exec, fork children included; median and range); wall time in
# ms (median); failures reported (seed 1); the counterexample as printed
# (seed 1; the line the program prints for the input).

set -u
: "${LIBHEGEL_SO:?}" "${OUT:?}" "${SEEDS:=1 2 3 4 5}"
export HEGEL_LIBHEGEL_PATH="$LIBHEGEL_SO"
unset HEGEL_DATABASE HEGEL_TEST_CASES HEGEL_C_REPRODUCE HEGEL_C_MERGE_ORIGINS HEGEL_C_FORK \
      HEGEL_REPORT_MULTIPLE_FAILURES HEGEL_VERBOSITY HEGEL_DERANDOMIZE 2>/dev/null
cd "$(dirname "$0")" || exit 1
mkdir -p "$OUT/out"
ulimit -c 0

# pair | program | side | extra env | grep for the counterexample line(s)
ROWS='
1 minimal|lite_minimal|libhegel lite||^FAILED:\|^run error:
1 minimal|raw_minimal|libhegel||^n =
1 minimal|hc_minimal|hegel-c fork||^Draw 1:
1 minimal|hcn_minimal|hegel-c nofork||^Draw 1:
2 crash|lite_crash|libhegel lite (own fork)||^FAILED:\|^run error:
2 crash|raw_crash|libhegel||^id =
2 crash|raw_crash_fork|libhegel + own fork||^id =
2 crash|hc_crash|hegel-c fork||^Draw 1:
2 crash|hcn_crash|hegel-c nofork||^Draw 1:
2 crash|hc_crash_iso|hegel-c nofork+ISOLATED||^Draw 1:
3 graph|lite_graph|libhegel lite||^FAILED:\|^run error:
3 graph|raw_graph|libhegel||^graph:
3 graph|hc_graph|hegel-c fork||^graph:
3 graph|hcn_graph|hegel-c nofork||^graph:
4 tree|lite_tree|libhegel lite||^FAILED:\|^run error:
4 tree|raw_tree|libhegel||^expr =
4 tree|hc_tree|hegel-c fork||^expr =
4 tree|hcn_tree|hegel-c nofork||^expr =
5 twobugs|lite_twobugs|libhegel lite||^FAILED:\|^run error:
5 twobugs|raw_twobugs|libhegel||^lo =
5 twobugs|hc_twobugs|hegel-c fork||^message:
5 twobugs|lite_twobugs|libhegel lite|HEGEL_REPORT_MULTIPLE_FAILURES=1|^FAILED:\|^run error:
5 twobugs|raw_twobugs|libhegel|HEGEL_REPORT_MULTIPLE_FAILURES=1|^lo =
5 twobugs|hc_twobugs|hegel-c fork|HEGEL_REPORT_MULTIPLE_FAILURES=1|^message:
6 misuse|lite_misuse|libhegel lite||run error:
6 misuse|raw_misuse|libhegel||run error:
6 misuse|hc_misuse|hegel-c fork||Health check failure:
'

median () { sort -n | awk '{ a[NR] = $1 } END { if (NR) print a[int ((NR + 1) / 2)]; else print "-" }'; }

printf '%-10s %-26s %-6s %-11s %-12s %-6s %-4s %s\n' \
  pair side env exits "execs med" "[range]" ms "rep  counterexample (seed 1)"
printf '%s\n' "------------------------------------------------------------------------------------------------------------------------"

echo "$ROWS" | while IFS='|' read -r pair prog side env pat; do
  [ -n "$pair" ] || continue
  tag=$prog${env:++multi}
  exits=""; : > "$OUT/out/$tag.execs"; : > "$OUT/out/$tag.ms"
  for seed in $SEEDS; do
    o="$OUT/out/$tag.$seed.txt"; cnt="$OUT/out/$tag.$seed.count"
    rm -f "$cnt"
    t0=$(date +%s%N)
    env HEGEL_SEED="$seed" LVH_COUNT="$cnt" $env "$OUT/$prog" > "$o" 2>&1
    rc=$?
    t1=$(date +%s%N)
    exits="$exits${exits:+,}$rc"
    if [ -f "$cnt" ]; then wc -c < "$cnt" | tr -d ' ' >> "$OUT/out/$tag.execs"; else echo 0 >> "$OUT/out/$tag.execs"; fi
    echo $(( (t1 - t0) / 1000000 )) >> "$OUT/out/$tag.ms"
  done
  exits=$(echo "$exits" | tr ',' '\n' | sort | uniq -c | awk '{ printf "%s%sx%s", (NR > 1 ? " " : ""), $1, $2 }')
  em=$(median < "$OUT/out/$tag.execs")
  er=$(sort -n "$OUT/out/$tag.execs" | awk 'NR == 1 { lo = $1 } { hi = $1 } END { print "[" lo "-" hi "]" }')
  ms=$(median < "$OUT/out/$tag.ms")
  first=$(echo $SEEDS | awk '{ print $1 }')
  o1="$OUT/out/$tag.$first.txt"
  reps=$(grep -c -e '^FAILED:' -e '^origin:' "$o1")
  cex=$(grep -e "$pat" "$o1" | sed 's/^message: //' | tr '\n' ';' | sed 's/;$//; s/;/ ; /g')
  [ -n "$cex" ] || cex="(none: $(tail -n 1 "$o1" | cut -c1-60))"
  [ -s "$o1" ] || cex="(no output)"
  printf '%-10s %-26s %-6s %-11s %-12s %-6s %-4s %s\n' \
    "$pair" "$side" "${env:+multi}" "$exits" "$em" "$er" "$ms" "$reps    $(echo "$cex" | cut -c1-70)"
done

# Replay: a printed blob through each side's replay path, then a blob
# that no longer fails (n = 4) to check "did not reproduce".
echo
echo "7 replay   (n = 5 blob, then a passing n = 4 blob)"
for b in AAEAAAAACgEAAAAF AAEAAAAACgEAAAAE; do
  r=$(REPRO=$b "$OUT/lite_replay" 2>&1); rc=$?
  printf '  lite      REPRO=%s  exit %s: %s\n' "$b" "$rc" "$(echo "$r" | tr '\n' ' ' | cut -c1-80)"
  r=$(REPRO=$b "$OUT/raw_replay" 2>&1); rc=$?
  printf '  libhegel  REPRO=%s  exit %s: %s\n' "$b" "$rc" "$(echo "$r" | tr '\n' ' ' | cut -c1-80)"
  h=$(HEGEL_C_REPRODUCE="1:$b" "$OUT/hc_minimal" 2>&1); rc=$?
  printf '  hegel-c   HEGEL_C_REPRODUCE=1:%s  exit %s: %s\n' "$b" "$rc" \
    "$(echo "$h" | grep -e '^Draw' -e 'did not' | tr '\n' ' ' | cut -c1-80)"
done

echo
echo "Lines of test code (comments and blank lines excluded; shared sut.h / common.h not counted)"
printf '  %-18s %6s %12s %10s %7s\n' file lines boilerplate "property" chars
sh ./loc.sh lite_minimal.c raw_minimal.c hc_minimal.c lite_crash.c raw_crash.c raw_crash_fork.c \
   hc_crash.c hc_crash_iso.c lite_graph.c raw_graph.c hc_graph.c lite_tree.c raw_tree.c hc_tree.c \
   lite_twobugs.c raw_twobugs.c hc_twobugs.c lite_misuse.c raw_misuse.c hc_misuse.c \
   lite_replay.c raw_replay.c raw_check.h |
  awk '{ printf "  %-18s %6d %12d %10d %7d\n", $1, $2, $3, $2 - $3, $4 }'
