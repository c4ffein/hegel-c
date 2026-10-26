#!/bin/bash
# usage: work/v3x/build32.sh SRC OUT [extra CFLAGS]   (from sgreplay/) -> static i386 exec32
# needs the i386 libc sysroot in /tmp/claude-1000/-home-dev-workspace-hegel-c/98f78428-236e-4f69-850f-f21c4bd893ab/scratchpad/sys32/root (Debian libc6-i386, libc6-dev-i386, lib32gcc-14-dev)
R=/tmp/claude-1000/-home-dev-workspace-hegel-c/98f78428-236e-4f69-850f-f21c4bd893ab/scratchpad/sys32/root
SRC=$1; OUT=$2; shift 2
gcc -m32 -static -isystem $R/usr/include/x86_64-linux-gnu -isystem /usr/include/x86_64-linux-gnu \
  -B$R/usr/lib32 -B$R/usr/lib/gcc/x86_64-linux-gnu/14/32 -L$R/usr/lib32 -L$R/usr/lib/gcc/x86_64-linux-gnu/14/32 \
  -O1 -g -w -D_LGPL_SOURCE "$@" -Iharness/include -Ikernel/tools/testing/shared -Ikernel/tools/include \
  -Ikernel/tools/arch/x86/include -o $OUT work/v3x/exec32.c $SRC harness/scatterlist_shim.c
