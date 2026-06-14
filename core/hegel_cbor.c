/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

#include "hegel_cbor.h"

#include <string.h>

/* ---------- Encoding ---------- */

void hegel_cbor_writer_init (hegel_cbor_writer * w, uint8_t * buf, size_t cap)
{
  w->buf = buf;
  w->cap = cap;
  w->pos = 0;
  w->err = HEGEL_CBOR_OK;
}

int hegel_cbor_writer_finalize (const hegel_cbor_writer * w)
{
  if (w->err != HEGEL_CBOR_OK) return (int)w->err;
  return (int)w->pos;
}

static void writer_put1 (hegel_cbor_writer * w, uint8_t b)
{
  if (w->err != HEGEL_CBOR_OK) return;
  if (w->pos + 1 > w->cap) { w->err = HEGEL_CBOR_ERR_NOMEM; return; }
  w->buf[w->pos++] = b;
}

static void writer_put_n (hegel_cbor_writer * w, const uint8_t * s, size_t n)
{
  if (w->err != HEGEL_CBOR_OK) return;
  if (w->pos + n > w->cap) { w->err = HEGEL_CBOR_ERR_NOMEM; return; }
  if (n) memcpy (w->buf + w->pos, s, n);
  w->pos += n;
}

static void writer_put_be (hegel_cbor_writer * w, uint64_t v, int width)
{
  uint8_t tmp[8];
  for (int i = 0; i < width; ++i)
    tmp[width - 1 - i] = (uint8_t)(v >> (8 * i));
  writer_put_n (w, tmp, (size_t)width);
}

/* Write a major-type prefix (3 bits) + count, using the smallest encoding. */
static void writer_emit_head (hegel_cbor_writer * w, uint8_t major, uint64_t count)
{
  uint8_t mt = (uint8_t)(major << 5);
  if (count < 24) {
    writer_put1 (w, (uint8_t)(mt | (uint8_t)count));
  } else if (count <= 0xFF) {
    writer_put1 (w, (uint8_t)(mt | 24));
    writer_put1 (w, (uint8_t)count);
  } else if (count <= 0xFFFF) {
    writer_put1 (w, (uint8_t)(mt | 25));
    writer_put_be (w, count, 2);
  } else if (count <= 0xFFFFFFFFu) {
    writer_put1 (w, (uint8_t)(mt | 26));
    writer_put_be (w, count, 4);
  } else {
    writer_put1 (w, (uint8_t)(mt | 27));
    writer_put_be (w, count, 8);
  }
}

void hegel_cbor_write_uint (hegel_cbor_writer * w, uint64_t v)
{
  writer_emit_head (w, 0, v);
}

void hegel_cbor_write_int (hegel_cbor_writer * w, int64_t v)
{
  if (v >= 0) {
    writer_emit_head (w, 0, (uint64_t)v);
  } else {
    /* CBOR negative int: encoded value = -1 - v. */
    uint64_t u = (uint64_t)(-(v + 1));  /* avoids overflow for INT64_MIN */
    writer_emit_head (w, 1, u);
  }
}

void hegel_cbor_write_bool (hegel_cbor_writer * w, bool v)
{
  writer_put1 (w, v ? 0xF5 : 0xF4);
}

void hegel_cbor_write_null (hegel_cbor_writer * w)
{
  writer_put1 (w, 0xF6);
}

void hegel_cbor_write_double (hegel_cbor_writer * w, double v)
{
  /* Always emit float64 (0xFB).  The engine accepts any width; only
  ** the reader needs to handle ciborium's smallest-lossless encoding. */
  uint64_t bits;
  memcpy (&bits, &v, sizeof bits);
  writer_put1 (w, 0xFB);
  writer_put_be (w, bits, 8);
}

void hegel_cbor_write_text (hegel_cbor_writer * w, const char * s, size_t len)
{
  writer_emit_head (w, 3, (uint64_t)len);
  writer_put_n (w, (const uint8_t *)s, len);
}

void hegel_cbor_write_text_z (hegel_cbor_writer * w, const char * s)
{
  hegel_cbor_write_text (w, s, strlen (s));
}

void hegel_cbor_write_bytes (hegel_cbor_writer * w, const uint8_t * b, size_t len)
{
  writer_emit_head (w, 2, (uint64_t)len);
  writer_put_n (w, b, len);
}

void hegel_cbor_write_array_header (hegel_cbor_writer * w, size_t n)
{
  writer_emit_head (w, 4, (uint64_t)n);
}

void hegel_cbor_write_map_header (hegel_cbor_writer * w, size_t n_pairs)
{
  writer_emit_head (w, 5, (uint64_t)n_pairs);
}

/* ---------- Decoding ---------- */

void hegel_cbor_reader_init (hegel_cbor_reader * r, const uint8_t * buf, size_t cap)
{
  r->buf = buf;
  r->cap = cap;
  r->pos = 0;
}

static hegel_cbor_result reader_peek_byte (const hegel_cbor_reader * r, uint8_t * out)
{
  if (r->pos >= r->cap) return HEGEL_CBOR_ERR_TRUNCATED;
  *out = r->buf[r->pos];
  return HEGEL_CBOR_OK;
}

static hegel_cbor_result reader_get_byte (hegel_cbor_reader * r, uint8_t * out)
{
  if (r->pos >= r->cap) return HEGEL_CBOR_ERR_TRUNCATED;
  *out = r->buf[r->pos++];
  return HEGEL_CBOR_OK;
}

static hegel_cbor_result reader_get_be (hegel_cbor_reader * r, int width, uint64_t * out)
{
  if (r->pos + (size_t)width > r->cap) return HEGEL_CBOR_ERR_TRUNCATED;
  uint64_t v = 0;
  for (int i = 0; i < width; ++i)
    v = (v << 8) | r->buf[r->pos + (size_t)i];
  r->pos += (size_t)width;
  *out = v;
  return HEGEL_CBOR_OK;
}

/* Decode the next head: returns major type and the count value. */
static hegel_cbor_result reader_read_head (hegel_cbor_reader * r,
                                           uint8_t * out_major,
                                           uint64_t * out_count,
                                           uint8_t * out_info)
{
  uint8_t b;
  hegel_cbor_result rc = reader_get_byte (r, &b);
  if (rc != HEGEL_CBOR_OK) return rc;

  uint8_t major = (uint8_t)(b >> 5);
  uint8_t info  = (uint8_t)(b & 0x1F);
  *out_major = major;
  *out_info  = info;

  if (info < 24) {
    *out_count = info;
    return HEGEL_CBOR_OK;
  }
  if (info == 24) {
    uint8_t v;
    rc = reader_get_byte (r, &v);
    if (rc != HEGEL_CBOR_OK) return rc;
    *out_count = v;
    return HEGEL_CBOR_OK;
  }
  if (info == 25) return reader_get_be (r, 2, out_count);
  if (info == 26) return reader_get_be (r, 4, out_count);
  if (info == 27) return reader_get_be (r, 8, out_count);
  /* info 28..30 reserved; 31 indefinite — unsupported. */
  return HEGEL_CBOR_ERR_UNSUPPORTED;
}

hegel_cbor_result hegel_cbor_peek_type (const hegel_cbor_reader * r,
                                        hegel_cbor_type * out_type)
{
  uint8_t b;
  hegel_cbor_result rc = reader_peek_byte (r, &b);
  if (rc != HEGEL_CBOR_OK) return rc;

  uint8_t major = (uint8_t)(b >> 5);
  uint8_t info  = (uint8_t)(b & 0x1F);

  if (major < 6) {
    *out_type = (hegel_cbor_type)major;
    return HEGEL_CBOR_OK;
  }
  if (major == 6) { *out_type = HEGEL_CBOR_TYPE_TAG; return HEGEL_CBOR_OK; }
  if (major == 7) {
    if (info == 20 || info == 21) { *out_type = HEGEL_CBOR_TYPE_BOOL;  return HEGEL_CBOR_OK; }
    if (info == 22)               { *out_type = HEGEL_CBOR_TYPE_NULL;  return HEGEL_CBOR_OK; }
    if (info >= 25 && info <= 27) { *out_type = HEGEL_CBOR_TYPE_FLOAT; return HEGEL_CBOR_OK; }
  }
  return HEGEL_CBOR_ERR_UNSUPPORTED;
}

hegel_cbor_result hegel_cbor_read_uint (hegel_cbor_reader * r, uint64_t * out)
{
  size_t saved = r->pos;
  uint8_t major, info; uint64_t count;
  hegel_cbor_result rc = reader_read_head (r, &major, &count, &info);
  if (rc != HEGEL_CBOR_OK) { r->pos = saved; return rc; }
  if (major != 0) { r->pos = saved; return HEGEL_CBOR_ERR_TYPE; }
  *out = count;
  return HEGEL_CBOR_OK;
}

hegel_cbor_result hegel_cbor_read_int (hegel_cbor_reader * r, int64_t * out)
{
  size_t saved = r->pos;
  uint8_t major, info; uint64_t count;
  hegel_cbor_result rc = reader_read_head (r, &major, &count, &info);
  if (rc != HEGEL_CBOR_OK) { r->pos = saved; return rc; }

  if (major == 0) {
    if (count > (uint64_t)INT64_MAX) { r->pos = saved; return HEGEL_CBOR_ERR_OVERFLOW; }
    *out = (int64_t)count;
    return HEGEL_CBOR_OK;
  }
  if (major == 1) {
    /* CBOR negative: encoded count = -1 - v, so v = -1 - count. */
    if (count > (uint64_t)INT64_MAX) { r->pos = saved; return HEGEL_CBOR_ERR_OVERFLOW; }
    *out = -1 - (int64_t)count;
    return HEGEL_CBOR_OK;
  }
  r->pos = saved;
  return HEGEL_CBOR_ERR_TYPE;
}

hegel_cbor_result hegel_cbor_read_bool (hegel_cbor_reader * r, bool * out)
{
  size_t saved = r->pos;
  uint8_t b;
  hegel_cbor_result rc = reader_get_byte (r, &b);
  if (rc != HEGEL_CBOR_OK) { r->pos = saved; return rc; }
  if (b == 0xF4) { *out = false; return HEGEL_CBOR_OK; }
  if (b == 0xF5) { *out = true;  return HEGEL_CBOR_OK; }
  r->pos = saved;
  return HEGEL_CBOR_ERR_TYPE;
}

hegel_cbor_result hegel_cbor_read_null (hegel_cbor_reader * r)
{
  size_t saved = r->pos;
  uint8_t b;
  hegel_cbor_result rc = reader_get_byte (r, &b);
  if (rc != HEGEL_CBOR_OK) { r->pos = saved; return rc; }
  if (b == 0xF6) return HEGEL_CBOR_OK;
  r->pos = saved;
  return HEGEL_CBOR_ERR_TYPE;
}

/* Widen an IEEE 754 binary16 to double.  Needed because ciborium
** serializes floats at the smallest lossless width — 0.5 arrives as
** half-precision even though the engine computed it as an f64. */
static double half_to_double (uint16_t h)
{
  uint64_t sign = (uint64_t)(h >> 15) & 1;
  uint32_t exp  = (h >> 10) & 0x1F;
  uint32_t frac = h & 0x3FF;
  uint64_t dbits;

  if (exp == 0) {
    if (frac == 0) {
      dbits = sign << 63;                          /* ±0 */
    } else {
      /* subnormal half: value = frac * 2^-24; renormalize for double */
      int e = -15;
      while ((frac & 0x400) == 0) { frac <<= 1; e--; }
      frac &= 0x3FF;
      dbits = (sign << 63)
            | ((uint64_t)(e + 1023) << 52)
            | ((uint64_t)frac << 42);
    }
  } else if (exp == 0x1F) {
    dbits = (sign << 63) | (0x7FFULL << 52) | ((uint64_t)frac << 42);  /* inf/nan */
  } else {
    dbits = (sign << 63)
          | ((uint64_t)(exp - 15 + 1023) << 52)
          | ((uint64_t)frac << 42);
  }

  double d;
  memcpy (&d, &dbits, sizeof d);
  return d;
}

hegel_cbor_result hegel_cbor_read_double (hegel_cbor_reader * r, double * out)
{
  size_t saved = r->pos;
  uint8_t b;
  hegel_cbor_result rc = reader_get_byte (r, &b);
  if (rc != HEGEL_CBOR_OK) { r->pos = saved; return rc; }

  uint64_t bits;
  switch (b) {
    case 0xF9:  /* float16 */
      rc = reader_get_be (r, 2, &bits);
      if (rc != HEGEL_CBOR_OK) { r->pos = saved; return rc; }
      *out = half_to_double ((uint16_t)bits);
      return HEGEL_CBOR_OK;
    case 0xFA: { /* float32 */
      rc = reader_get_be (r, 4, &bits);
      if (rc != HEGEL_CBOR_OK) { r->pos = saved; return rc; }
      uint32_t b32 = (uint32_t)bits;
      float f;
      memcpy (&f, &b32, sizeof f);
      *out = (double)f;
      return HEGEL_CBOR_OK;
    }
    case 0xFB: { /* float64 */
      rc = reader_get_be (r, 8, &bits);
      if (rc != HEGEL_CBOR_OK) { r->pos = saved; return rc; }
      memcpy (out, &bits, sizeof *out);
      return HEGEL_CBOR_OK;
    }
    default:
      r->pos = saved;
      return HEGEL_CBOR_ERR_TYPE;
  }
}

hegel_cbor_result hegel_cbor_read_tag (hegel_cbor_reader * r, uint64_t * out_tag)
{
  size_t saved = r->pos;
  uint8_t major, info; uint64_t count;
  hegel_cbor_result rc = reader_read_head (r, &major, &count, &info);
  if (rc != HEGEL_CBOR_OK) { r->pos = saved; return rc; }
  if (major != 6) { r->pos = saved; return HEGEL_CBOR_ERR_TYPE; }
  *out_tag = count;
  return HEGEL_CBOR_OK;
}

static hegel_cbor_result reader_read_string (hegel_cbor_reader * r, uint8_t expect_major,
                                              const uint8_t ** out_ptr, size_t * out_len)
{
  size_t saved = r->pos;
  uint8_t major, info; uint64_t count;
  hegel_cbor_result rc = reader_read_head (r, &major, &count, &info);
  if (rc != HEGEL_CBOR_OK) { r->pos = saved; return rc; }
  if (major != expect_major) { r->pos = saved; return HEGEL_CBOR_ERR_TYPE; }
  if (count > r->cap - r->pos) { r->pos = saved; return HEGEL_CBOR_ERR_TRUNCATED; }
  *out_ptr = r->buf + r->pos;
  *out_len = (size_t)count;
  r->pos += (size_t)count;
  return HEGEL_CBOR_OK;
}

hegel_cbor_result hegel_cbor_read_text (hegel_cbor_reader * r,
                                        const char ** out_ptr, size_t * out_len)
{
  const uint8_t * p; size_t n;
  hegel_cbor_result rc = reader_read_string (r, 3, &p, &n);
  if (rc != HEGEL_CBOR_OK) return rc;
  *out_ptr = (const char *)p;
  *out_len = n;
  return HEGEL_CBOR_OK;
}

hegel_cbor_result hegel_cbor_read_bytes (hegel_cbor_reader * r,
                                         const uint8_t ** out_ptr, size_t * out_len)
{
  return reader_read_string (r, 2, out_ptr, out_len);
}

static hegel_cbor_result reader_read_container (hegel_cbor_reader * r,
                                                 uint8_t expect_major, size_t * out_n)
{
  size_t saved = r->pos;
  uint8_t major, info; uint64_t count;
  hegel_cbor_result rc = reader_read_head (r, &major, &count, &info);
  if (rc != HEGEL_CBOR_OK) { r->pos = saved; return rc; }
  if (major != expect_major) { r->pos = saved; return HEGEL_CBOR_ERR_TYPE; }
  *out_n = (size_t)count;
  return HEGEL_CBOR_OK;
}

hegel_cbor_result hegel_cbor_read_array_header (hegel_cbor_reader * r, size_t * out_n)
{
  return reader_read_container (r, 4, out_n);
}

hegel_cbor_result hegel_cbor_read_map_header (hegel_cbor_reader * r, size_t * out_n_pairs)
{
  return reader_read_container (r, 5, out_n_pairs);
}

hegel_cbor_result hegel_cbor_skip (hegel_cbor_reader * r)
{
  size_t saved = r->pos;
  uint8_t major, info; uint64_t count;
  hegel_cbor_result rc = reader_read_head (r, &major, &count, &info);
  if (rc != HEGEL_CBOR_OK) { r->pos = saved; return rc; }

  switch (major) {
    case 0: case 1:
      return HEGEL_CBOR_OK;
    case 2: case 3:
      if (count > r->cap - r->pos) { r->pos = saved; return HEGEL_CBOR_ERR_TRUNCATED; }
      r->pos += (size_t)count;
      return HEGEL_CBOR_OK;
    case 4:
      for (uint64_t i = 0; i < count; ++i) {
        rc = hegel_cbor_skip (r);
        if (rc != HEGEL_CBOR_OK) return rc;
      }
      return HEGEL_CBOR_OK;
    case 5:
      for (uint64_t i = 0; i < count; ++i) {
        rc = hegel_cbor_skip (r);  /* key */
        if (rc != HEGEL_CBOR_OK) return rc;
        rc = hegel_cbor_skip (r);  /* value */
        if (rc != HEGEL_CBOR_OK) return rc;
      }
      return HEGEL_CBOR_OK;
    case 6:
      /* tag: head already consumed; skip the tagged item */
      return hegel_cbor_skip (r);
    case 7:
      if (info == 20 || info == 21 || info == 22) return HEGEL_CBOR_OK;
      /* float16/32/64: reader_read_head already consumed the payload
      ** bytes as the head's count value — nothing left to skip */
      if (info >= 25 && info <= 27) return HEGEL_CBOR_OK;
      r->pos = saved;
      return HEGEL_CBOR_ERR_UNSUPPORTED;
    default:
      r->pos = saved;
      return HEGEL_CBOR_ERR_UNSUPPORTED;
  }
}

hegel_cbor_result hegel_cbor_map_find (hegel_cbor_reader * r, const char * key)
{
  size_t n;
  hegel_cbor_result rc = hegel_cbor_read_map_header (r, &n);
  if (rc != HEGEL_CBOR_OK) return rc;

  size_t key_len = strlen (key);
  for (size_t i = 0; i < n; ++i) {
    const char * k; size_t klen;
    rc = hegel_cbor_read_text (r, &k, &klen);
    if (rc != HEGEL_CBOR_OK) return rc;

    if (klen == key_len && memcmp (k, key, klen) == 0) return HEGEL_CBOR_OK;

    rc = hegel_cbor_skip (r);  /* skip the value */
    if (rc != HEGEL_CBOR_OK) return rc;
  }
  return HEGEL_CBOR_ERR_NOT_FOUND;
}
