#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
"""Which libhegel functions does hegel-c bind, and is every other one accounted for?

Reads the functions a libhegel header declares, the ones core/hegel_engine.c
binds (its BIND lines), and tools/libhegel-coverage.toml (every unbound
function, grouped, with a status: deliberate or gap).  Exit 1 if:

  - the header declares a function that is neither bound nor listed
    (a new upstream feature: classify it in the toml, or bind it);
  - a bound function is gone from the header (the runtime would fail to load);
  - a listed function is gone from the header (drop it from the toml);
  - a function is both bound and listed, or listed twice.

  tools/libhegel-coverage.py VERSION [--header PATH] [--list]

The header is PATH if given, else deps/hegel/libhegel/include/hegel.h when the
submodule is checked out at libhegel/VERSION, else
build/hegel-VERSION.h, downloaded once from the release tag.  --list prints
every group with its functions.  Python 3.11+, no deps.
"""
import pathlib
import re
import subprocess
import sys
import tomllib
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
ENGINE_C = ROOT / "core" / "hegel_engine.c"
LISTED = ROOT / "tools" / "libhegel-coverage.toml"
SUBMODULE = ROOT / "deps" / "hegel"
RAW = "https://raw.githubusercontent.com/hegeldev/hegel/libhegel/{v}/libhegel/include/hegel.h"

DECL = re.compile(r"^[A-Za-z_][\w \*]*?\b(hegel_\w+)\s*\(", re.M)
BIND = re.compile(r'BIND\s*\(\s*\w+\s*,\s*"(hegel_\w+)"\s*\)')


def header_for(version, given):
  if given:
    return pathlib.Path(given), given
  sub = SUBMODULE / "libhegel" / "include" / "hegel.h"
  if sub.exists():
    tags = subprocess.run(["git", "-C", str(SUBMODULE), "tag", "--points-at", "HEAD"],
                          capture_output=True, text=True).stdout.split()
    if f"libhegel/{version}" in tags:
      return sub, f"deps/hegel (libhegel/{version})"
  cached = ROOT / "build" / f"hegel-{version}.h"
  if not cached.exists():
    url = RAW.format(v=version)
    try:
      with urllib.request.urlopen(url, timeout=60) as r:
        data = r.read()
    except Exception as e:  # network, 404 (no such tag)
      sys.exit(f"COULD NOT CHECK: no header for libhegel {version}: {url}: {e}")
    cached.parent.mkdir(exist_ok=True)
    cached.write_bytes(data)
  return cached, f"{cached.relative_to(ROOT)} (from libhegel/{version})"


def main(argv):
  args = [a for a in argv if not a.startswith("--")]
  given = None
  if "--header" in argv:
    i = argv.index("--header")
    given = argv[i + 1]
    args.remove(given)
  if len(args) != 1:
    sys.exit(__doc__)
  version = args[0]

  path, where = header_for(version, given)
  text = re.sub(r"/\*.*?\*/", "", path.read_text(), flags=re.S)
  text = re.sub(r"//[^\n]*", "", text)
  declared = set(DECL.findall(text))
  bound = set(BIND.findall(ENGINE_C.read_text()))
  groups = tomllib.loads(LISTED.read_text())["group"]

  errors = []
  listed = {}
  for g in groups:
    if g.get("status") not in ("deliberate", "gap"):
      errors.append(f"{LISTED.name}: group {g.get('name')!r}: status must be deliberate or gap")
    for f in g["functions"]:
      if f in listed:
        errors.append(f"{f}: listed twice ({listed[f]['name']!r}, {g['name']!r})")
      listed[f] = g

  for f in sorted(declared - bound - set(listed)):
    errors.append(f"{f}: NEW in libhegel {version}, neither bound nor listed — "
                  f"classify it in tools/{LISTED.name} (deliberate or gap), or bind it")
  for f in sorted(bound - declared):
    errors.append(f"{f}: bound in core/hegel_engine.c but not declared by libhegel {version} "
                  "— the runtime would fail to load")
  for f in sorted(set(listed) - declared):
    errors.append(f"{f}: listed in tools/{LISTED.name} but gone from libhegel {version} — drop it")
  for f in sorted(bound & set(listed)):
    errors.append(f"{f}: bound and listed — drop it from tools/{LISTED.name}")

  gap = sum(1 for f in declared if listed.get(f, {}).get("status") == "gap")
  deliberate = sum(1 for f in declared if listed.get(f, {}).get("status") == "deliberate")
  print(f"libhegel {version}: {len(declared)} functions declared ({where})")
  print(f"  bound by hegel-c: {len(bound & declared)}")
  print(f"  not bound, deliberate: {deliberate}")
  print(f"  not bound, gap: {gap}  (TODO.md, \"libhegel coverage\")")
  if "--list" in argv:
    for g in groups:
      fs = [f for f in g["functions"] if f in declared]
      print(f"\n[{g['status']}] {g['name']} ({len(fs)}): {g['why']}")
      for f in fs:
        print(f"    {f}")
  if errors:
    print(f"\n{len(errors)} problem(s):")
    for e in errors:
      print(f"  {e}")
    return 1
  print("  every declared function is bound or classified")
  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv[1:]))
