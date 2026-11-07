# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.
#
# loc.sh FILE... — per file: code lines (non-blank once comments are
# removed), of which boilerplate (lines between the /* B{ */ and /* }B */
# markers), and code characters (non-whitespace, comments removed).
# Prints "file lines boilerplate chars", one line per file.

for f in "$@"; do
  awk -v file="$f" '
    {
      raw = $0; line = $0; code = ""
      while (length (line)) {
        if (inc) {
          i = index (line, "*/")
          if (!i) { line = ""; break }
          line = substr (line, i + 2); inc = 0
        } else {
          i = index (line, "/*"); j = index (line, "//")
          if (j && (!i || j < i)) { code = code substr (line, 1, j - 1); break }
          if (!i) { code = code line; break }
          code = code substr (line, 1, i - 1); line = substr (line, i + 2); inc = 1
        }
      }
      if (raw ~ /\/\* B\{ \*\//) inb = 1
      if (raw ~ /\/\* \}B \*\//) inb = 0
      gsub (/[ \t\r]/, "", code)
      if (code != "") { total++; chars += length (code); if (inb) boil++ }
    }
    END { printf "%s %d %d %d\n", file, total, boil, chars }
  ' "$f"
done
