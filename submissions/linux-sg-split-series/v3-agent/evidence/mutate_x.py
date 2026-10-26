#!/usr/bin/env python3
"""Mutation testing for one C file against one or more test programs.

  python3 tools/mutate.py TEST.c [TEST2.c ...] [--src kernel/lib/sg_split.c] [--san] [--func NAME ...]

For each mutant of --src (relational operator swaps, && <-> ||, --/++ swaps,
dropped "+ 1" / "- 1"), builds every TEST against the mutant (via `make bin`) and
runs it from this directory.  A mutant is KILLED if any test exits non-zero
(or times out), SURVIVED if all pass.  The unmutated file must pass first.
--func limits mutation to the bodies of the named functions.
Writes work/mutants/report.txt.  A survivor means: no test can tell this
change from the original.  Either it is equivalent (say why), or a property
is missing, or the test setup cannot see the difference (e.g. no ASan).
"""
import argparse, os, re, shutil, subprocess, sys
SB = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SWAP = [(r"(?<=[\w)\]]) > (?=[\w(])", " >= "), (r" >= ", " > "), (r"(?<=[\w)\]]) < (?=[\w(])", " <= "),
        (r" <= ", " < "), (r" == ", " != "), (r" != ", " == "), (r" && ", " || "), (r" \|\| ", " && "),
        (r"--(?=\w)", "++"), (r"(?<=\w)--", "++"), (r"(?<=[\w)]) - 1\b", ""), (r"(?<=[\w)]) \+ 1\b", "")]

def func_ranges(lines, names):
    rs = []
    for n in names:
        for i, l in enumerate(lines):
            if re.match(rf"^\w.*\b{re.escape(n)}\s*\(", l) and not l.rstrip().endswith(";"):
                j = i
                while j < len(lines) and lines[j] != "}": j += 1
                rs.append((i, j))
    return rs

def run_all(src, tests, san, tag):
    outdir = f"{SB}/{OUT}"
    for t in tests:
        exe = f"{outdir}/{tag}_{os.path.basename(t)[:-2]}"
        cf = ["CFLAGS=-O1 -g -w -D_LGPL_SOURCE -DCONFIG_NEED_SG_DMA_LENGTH" + (" -fsanitize=address,undefined -fno-omit-frame-pointer" if san else "")] if NEED else []
        b = subprocess.run(["make", "-s", "bin", f"T={t}", f"SRC={src}", f"OUT={exe}"] + (["SAN=1"] if san else []) + cf,
                           cwd=SB, capture_output=True, text=True)
        if b.returncode: return "no-compile"
        try:
            r = subprocess.run([exe], cwd=SB, capture_output=True, timeout=300,
                               env={**os.environ, "HEGEL_LIBHEGEL_PATH": f"{SB}/hegel-c/build/libhegel.so"})
        except subprocess.TimeoutExpired: return "killed (timeout)"
        finally:
            if os.path.exists(exe): os.remove(exe)
        if r.returncode: return f"killed by {os.path.basename(t)}"
    return "SURVIVED"

ap = argparse.ArgumentParser(); ap.add_argument("tests", nargs="+"); ap.add_argument("--src", default="kernel/lib/sg_split.c")
ap.add_argument("--san", action="store_true"); ap.add_argument("--need", action="store_true"); ap.add_argument("--out", default="work/mutants"); ap.add_argument("--func", nargs="*", default=[])
a = ap.parse_args()
NEED = a.need
OUT = a.out
os.makedirs(f"{SB}/{OUT}", exist_ok=True)
lines = open(f"{SB}/{a.src}").read().split("\n")
base = run_all(a.src, a.tests, a.san, "orig")
if base != "SURVIVED": sys.exit(f"the unmutated {a.src} does not pass: {base}")
ranges = func_ranges(lines, a.func) if a.func else [(0, len(lines))]
muts = []
for lo, hi in ranges:
    for i in range(lo, hi):
        s = lines[i].strip()
        if s.startswith(("*", "/*", "//", "#")): continue
        for pat, rep in SWAP:
            for m in re.finditer(pat, lines[i]):
                muts.append((i, lines[i][:m.start()] + rep + lines[i][m.end():]))
report = [f"{len(muts)} mutants of {a.src} vs {' '.join(a.tests)}{' (ASan/UBSan)' if a.san else ''}{' NEED' if NEED else ''}"]
print(report[0], flush=True)
surv = 0
for n, (i, new) in enumerate(muts):
    ml = lines[:]; ml[i] = new
    p = f"{OUT}/m{n}.c"; open(f"{SB}/{p}", "w").write("\n".join(ml))
    v = run_all(p, a.tests, a.san, f"m{n}")
    surv += v == "SURVIVED"
    row = f"{v:28} L{i+1}: {lines[i].strip()}   ->   {new.strip()}"
    report.append(row); print(row, flush=True)
report.append(f"\n{surv} of {len(muts)} survived")
open(f"{SB}/{OUT}/report.txt", "w").write("\n".join(report) + "\n")
print(report[-1])
