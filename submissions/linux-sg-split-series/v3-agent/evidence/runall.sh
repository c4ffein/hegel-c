#!/bin/bash
# usage: work/v3x/runall.sh SRC TAG [plain_cases] [asan_cases]   (run from sgreplay/)
SRC=$1; TAG=$2; NP=${3:-2000}; NA=${4:-500}
NEEDF="-O1 -g -w -D_LGPL_SOURCE -DCONFIG_NEED_SG_DMA_LENGTH"
mkdir -p work/v3x/bin work/v3x/logs
for t in props_x x_new diff_x; do
  for cfg in plain need san need_san; do
    o=work/v3x/bin/${t}_${TAG}_$cfg
    case $cfg in
      plain) make -s bin T=work/v3x/test_$t.c SRC=$SRC OUT=$o ;;
      need) make -s bin T=work/v3x/test_$t.c SRC=$SRC OUT=$o CFLAGS="$NEEDF" ;;
      san) make -s bin T=work/v3x/test_$t.c SRC=$SRC OUT=$o SAN=1 ;;
      need_san) make -s bin T=work/v3x/test_$t.c SRC=$SRC OUT=$o CFLAGS="$NEEDF -fsanitize=address,undefined -fno-omit-frame-pointer" ;;
    esac >/dev/null 2>&1 || { echo "BUILD FAIL $t $cfg"; continue; }
    n=$NP; case $cfg in *san) n=$NA;; esac
    [ $t = diff_x ] && n=$((n*3))
    log=work/v3x/logs/${t}_${TAG}_$cfg.log
    if [[ $cfg == *san ]]; then (ulimit -c 0; timeout 900 ./$o $n > $log 2>&1); else (ulimit -c 0; ulimit -v 4000000; timeout 900 ./$o $n > $log 2>&1); fi
    rc=$?
    echo "$t $cfg rc=$rc :: $(grep -E '^=== [a-z_]+: (pass|FAIL)|differential.*: (PASS|FAIL|pass)|ERROR: AddressSanitizer|runtime error' $log | sed 's/^=== //' | tr '\n' ' ' | cut -c1-300)"
  done
done
