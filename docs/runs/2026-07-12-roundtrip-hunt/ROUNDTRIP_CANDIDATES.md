<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Round-trip candidate targets — consolidated (2026-07-12)

Sweep of inspiration/targets/linux/ by 6 haiku agents. Ranked on oracle strength
(independence of directions) + harness cost (cheap = self-contained).

## Slice 1: lib/ codecs & compression  [DONE]
TOP PICKS:
1. **bin2hex / hex2bin** (lib/hexdump.c) — decode(encode(x))==x. STRONG (separate paths, std hex). EASY (only <linux/types.h>). ← cheapest clean win.
2. **base64_encode / base64_decode** (lib/base64.c) — 3 variants (STD/URLSAFE/IMAP). WEAK-ish (same file, diff paths). EASY. Existing kunit tests.
3. **LZ4_compress_default / LZ4_decompress_safe** (lib/lz4/*) — decompress(compress(x))==x. STRONG (separate files + external liblz4). MEDIUM.
Others: sw842 compress/decompress (STRONG, MED), string_escape/unescape (WEAK, EASY), asn1 enc/dec (STRONG, HARD), zlib deflate/inflate (STRONG, HARD), unlzma/unzstd/bunzip2 decompressors (STRONG w/ external lib, HARD), string_helpers escape/unescape (EASY).
NOTE: bunzip2 already hunted this session (found real OOB); decompress family broadly viable.

## Slice 2: lib/ encode/pack (asn1, packing)  [DONE]
TOP PICKS:
1. **pack / unpack** (lib/packing.c) — unpack(pack(x,quirks))==x across quirk modes. STRONG (separate __pack/__unpack). EASY (libc + bitops). Has packing_test.c to model. ← STRONGEST cheap target of whole sweep.
2. **base64_encode / base64_decode** (lib/base64.c) — 3 variants. STRONG. EASY. (also flagged by slice 1)
3. **bitmap_parse / bitmap_print_bitmask_to_buf** + parselist/print_list (lib/bitmap-str.c) — parse(print(bm))==bm hex & list. STRONG (separate parser/formatter). EASY.
Also: asn1_encode_* / asn1_ber_decoder (STRONG, MED — real independent pair); string_escape/unescape (STRONG-ish, EASY); sw842 (STRONG, MED); bch encode/decode (STRONG but HARD, needs bch_init GF tables — already hunted); mpi read/write (WEAK, HARD).
## Slice 3: crypto/  [DONE]
TOP PICKS:
1. **crypto_ecdh_encode_key / crypto_ecdh_decode_key** (crypto/ecdh_helper.c) — decode(encode(x))==x. STRONG (separate paths). EASY (pure C memcpy, no crypto API). ← sweet spot.
2. **crypto_dh_encode_key / crypto_dh_decode_key** (crypto/dh_helper.c) — same shape + defensive bounds checks. STRONG. EASY.
3. (honorable) vli_from_be64/le64 (crypto/ecc.c) — endianness involution, EASY but WEAK (symmetric).
NOTE: deflate/lz4/lzo scompress/sdecompress are STRONG but HARD (crypto scomp API + external lib) — skip unless linking the lib. ecdsa/ecrdsa parsers WEAK/HARD.
## Slice 4: net/ serialization  [STOPPED by user — not run]
## Slice 6: whole-tree wildcard  [STOPPED by user — not run]

---

# CONSOLIDATED SHORTLIST (cross-slice, best cheap oracles first)

Tier A — EASY harness + STRONG independent oracle (do these first):
1. pack / unpack (lib/packing.c) — has packing_test.c reference. TOP.
2. utf8_to_utf32 / utf32_to_utf8 (fs/nls/nls_base.c) + 30 codepage uni2char/char2uni pairs — huge parametric surface.
3. bin2hex / hex2bin (lib/hexdump.c) — trivial warm-up.
4. base64 enc/dec (lib/base64.c) — 3 variants.
5. crypto_ecdh/dh encode/decode (crypto/ecdh_helper.c, dh_helper.c) — pure C, no crypto API.
6. bitmap_parse / print (lib/bitmap-str.c).

Tier B — STRONG oracle but MED/HARD harness (higher payoff, more scaffolding):
- asn1 encode/decode (lib/) ; sw842 compress/decompress (lib/842/) ; LZ4 (lib/lz4/, external liblz4 oracle).

Skip / weak: mpi read/write (shared repr), string escape/unescape (same file), involutions (bitrev/vli_from_*), one-way (ucs2_as_utf8, uuid_parse).
## Slice 5: fs/ charset/unicode  [DONE]
TOP PICKS:
1. **utf8_to_utf32 / utf32_to_utf8** (fs/nls/nls_base.c) — decode(encode(x))==x. STRONG (independent bit-manip). EASY (libc types only). ← classic clean win.
2. **uni2char / char2uni across 30+ NLS codepages** (fs/nls/nls_cp437.c, iso8859-*, cp850, euc-jp, ...) — char2uni(uni2char(x))==x injective mapping. STRONG (separate lookup tables). EASY–MED (need table includes). Great parametric fuzz across codepages.
3. **utf8s_to_utf16s / utf16s_to_utf8s** (fs/nls/nls_base.c) — surrogate-pair round-trip. STRONG. EASY.
Also: nls_utf8.c uni2char/char2uni wrappers (EASY); fs/unicode utf8_normalize idempotence (MEDIUM oracle, HARD — trie tables/module). ucs2_as_utf8 one-way only (skip).
## Slice 6: whole-tree wildcard  [pending]
