/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* UUID / IP address / email / URL / domain draws: every value is well
** formed.  Run PASSES (exit 0).
**
** Three layers:
**   1. Functions under test — uuid_format / uuid_parse (canonical
**      8-4-4-4-12 text and back), and the shape checkers domain_ok,
**      email_ok, url_ok (RFC 1035 labels, an RFC 5322 atext local part,
**      an http(s) URL with a percent-encoded path)
**   2. Hegel test           — a UUID with a forced version carries that
**      version and the RFC 4122 variant, an unversioned one is never nil,
**      both round-trip through text; IPv4 / IPv6 addresses round-trip
**      through inet_ntop / inet_pton; emails, URLs and domains have the
**      shape their RFC says, no NUL, and respect max_length
**   3. Makefile runner      — TESTS_PASS; each property runs in fork
**      mode AND in nofork mode, must exit 0 */

#include "hegel_c.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Layer 1: functions under test ---- */

static void uuid_format (const hegel_uuid * u, char out[37])
{
  static const char hex[] = "0123456789abcdef";
  int o = 0;
  for (int i = 0; i < 16; i++) {
    if (i == 4 || i == 6 || i == 8 || i == 10) out[o++] = '-';
    out[o++] = hex[u->bytes[i] >> 4];
    out[o++] = hex[u->bytes[i] & 15];
  }
  out[o] = '\0';
}

static int hexval (int c)
{
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
       : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* Returns 0 on success, -1 if `s` is not a canonical UUID. */
static int uuid_parse (const char * s, hegel_uuid * u)
{
  if (strlen (s) != 36) return -1;
  int b = 0;
  for (int i = 0; i < 36; ) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i++] != '-') return -1;
      continue;
    }
    int hi = hexval (s[i]), lo = hexval (s[i + 1]);
    if (hi < 0 || lo < 0) return -1;
    u->bytes[b++] = (uint8_t) (hi << 4 | lo);
    i += 2;
  }
  return 0;
}

/* One RFC 1035 label: 1..63 letters, digits, hyphens; no hyphen at an end. */
static int label_ok (const char * s, size_t n)
{
  if (n < 1 || n > 63 || s[0] == '-' || s[n - 1] == '-') return 0;
  for (size_t i = 0; i < n; i++)
    if (!isalnum ((unsigned char) s[i]) && s[i] != '-') return 0;
  return 1;
}

/* Dot-separated labels, the last (TLD) starting with a letter. */
static int domain_ok (const char * s, size_t n)
{
  size_t start = 0, last = 0;
  for (size_t i = 0; i <= n; i++)
    if (i == n || s[i] == '.') {
      if (!label_ok (s + start, i - start)) return 0;
      last = start;
      start = i + 1;
    }
  return isalpha ((unsigned char) s[last]);
}

static int atext (int c)
{
  return isalnum (c) || (c && strchr ("!#$%&'*+-/=?^_`{|}~", c));
}

static int email_ok (const char * s)
{
  const char * at = strchr (s, '@');
  if (!at || strchr (at + 1, '@')) return 0;
  size_t local = (size_t) (at - s);
  if (local < 1 || local > 64 || strlen (s) > 254) return 0;
  for (size_t i = 0; i < local; i++)
    if (!atext ((unsigned char) s[i])) return 0;
  return domain_ok (at + 1, strlen (at + 1));
}

static int url_char_ok (const char * s, size_t i)
{
  int c = (unsigned char) s[i];
  if (c == '%') return isxdigit ((unsigned char) s[i + 1]) && isxdigit ((unsigned char) s[i + 2]);
  return c > 0x20 && c < 0x7f && c != '#' && c != '%';
}

static int url_ok (const char * s)
{
  const char * p;
  if (strncmp (s, "http://", 7) == 0) p = s + 7;
  else if (strncmp (s, "https://", 8) == 0) p = s + 8;
  else return 0;
  size_t host = strcspn (p, ":/");
  if (!domain_ok (p, host)) return 0;
  p += host;
  if (*p == ':') {
    char * end;
    long port = strtol (p + 1, &end, 10);
    if (end == p + 1 || port < 1 || port > 65535) return 0;
    p = end;
  }
  if (*p != '/') return 0;
  for (; *p && *p != '#'; p++)
    if (!url_char_ok (p, 0)) return 0;
  if (*p == '#')
    for (p++; *p; p++)
      if (!url_char_ok (p, 0) && *p != '#') return 0;
  return 1;
}

/* ---- Layer 2: properties ---- */

static void prop_uuids (hegel_testcase * tc)
{
  char text[37];
  hegel_uuid back;

  int version = hegel_draw_int (tc, 1, 15);
  hegel_uuid u = hegel_draw_uuid (tc, version);
  HEGEL_ASSERT (u.bytes[6] >> 4 == version, "version nibble %d, asked %d", u.bytes[6] >> 4,
                version);
  HEGEL_ASSERT ((u.bytes[8] & 0xc0) == 0x80, "variant bits 0x%02x, not RFC 4122",
                u.bytes[8] & 0xc0);
  uuid_format (&u, text);
  HEGEL_ASSERT (uuid_parse (text, &back) == 0 && memcmp (&back, &u, sizeof u) == 0,
                "uuid %s did not round-trip", text);

  static const hegel_uuid nil;
  hegel_uuid any = hegel_draw_uuid (tc, 0);
  HEGEL_ASSERT (memcmp (&any, &nil, sizeof nil) != 0, "unversioned draw gave the nil uuid");
  uuid_format (&any, text);
  HEGEL_ASSERT (uuid_parse (text, &back) == 0 && memcmp (&back, &any, sizeof any) == 0,
                "uuid %s did not round-trip", text);
}

static void prop_ips (hegel_testcase * tc)
{
  char text[INET6_ADDRSTRLEN];
  uint8_t back[16];

  hegel_ipv4 a = hegel_draw_ipv4 (tc);
  HEGEL_ASSERT (inet_ntop (AF_INET, a.bytes, text, sizeof text) != NULL, "inet_ntop v4");
  HEGEL_ASSERT (inet_pton (AF_INET, text, back) == 1 && memcmp (back, a.bytes, 4) == 0,
                "ipv4 %s did not round-trip", text);

  hegel_ipv6 b = hegel_draw_ipv6 (tc);
  HEGEL_ASSERT (inet_ntop (AF_INET6, b.bytes, text, sizeof text) != NULL, "inet_ntop v6");
  HEGEL_ASSERT (inet_pton (AF_INET6, text, back) == 1 && memcmp (back, b.bytes, 16) == 0,
                "ipv6 %s did not round-trip", text);
}

static void prop_strings (hegel_testcase * tc)
{
  char buf[4096];

  int n = hegel_draw_email (tc, buf, sizeof buf);
  HEGEL_ASSERT ((size_t) n == strlen (buf), "email has an embedded NUL");
  HEGEL_ASSERT (email_ok (buf), "malformed email \"%s\"", buf);

  n = hegel_draw_url (tc, buf, sizeof buf);
  HEGEL_ASSERT ((size_t) n == strlen (buf) && n < (int) sizeof buf - 1,
                "url has an embedded NUL or was truncated (%d bytes)", n);
  HEGEL_ASSERT (url_ok (buf), "malformed url \"%s\"", buf);

  n = hegel_draw_domain (tc, 0, buf, sizeof buf);
  HEGEL_ASSERT (n <= 255 && domain_ok (buf, (size_t) n), "malformed domain \"%s\"", buf);

  int max_length = hegel_draw_int (tc, 4, 255);
  n = hegel_draw_domain (tc, max_length, buf, sizeof buf);
  HEGEL_ASSERT (n <= max_length, "domain of %d chars over max_length %d", n, max_length);
  HEGEL_ASSERT (domain_ok (buf, (size_t) n), "malformed domain \"%s\"", buf);

  /* Truncation follows hegel_draw_text: capacity - 1 bytes and a NUL. */
  char small[4];
  n = hegel_draw_email (tc, small, sizeof small);
  HEGEL_ASSERT (n <= 3 && small[n] == '\0', "capped email draw wrote %d bytes", n);
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

int main (void)
{
  hegel_run_options o = { .test_cases = 300 };
  int failed = 0;
  failed += hegel_run_test_with (prop_uuids, &o);
  failed += hegel_run_test_nofork_with (prop_uuids, &o);
  failed += hegel_run_test_with (prop_ips, &o);
  failed += hegel_run_test_nofork_with (prop_ips, &o);
  failed += hegel_run_test_with (prop_strings, &o);
  failed += hegel_run_test_nofork_with (prop_strings, &o);
  printf ("%d of 6 runs failed\n", failed);
  return failed != 0;
}
