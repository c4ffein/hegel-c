/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Minimal CBOR codec for libhegel's `hegel_generate` schema/value
** exchange.  Descended from purec/hegel_cbor.h (the wire-protocol
** v0.8 codec); the transport that codec served is dead upstream, but
** the byte format lives on as libhegel's schema dialect.
**
** Subset implemented (RFC 8949 major types):
**   0  unsigned int   (1..8 byte)
**   1  negative int   (1..8 byte)
**   2  byte string    (definite length)
**   3  text string    (definite length, UTF-8)
**   4  array          (definite length)
**   5  map            (definite length, text-string keys for our needs)
**   6  tag            (read side only — the engine wraps strings in
**                      tag 91, its WTF-8 marker)
**   7  false / true / null / float16 / float32 / float64
**     (float16 is read-only: ciborium emits the smallest lossless
**      width, so values like 0.5 arrive as half-precision)
**
** Not implemented (deliberately):
**   - indefinite-length encodings (additional info 31)
**   - bignums, decimals, rationals
**
** Encoding model: streaming writer to a caller-provided buffer.
** Decoding model: token cursor over a caller-held buffer (zero-copy
** for strings/bytes — they point into the input).
*/

#ifndef HEGEL_CBOR_H
#define HEGEL_CBOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
  HEGEL_CBOR_OK              = 0,
  HEGEL_CBOR_ERR_NOMEM       = -1,  /* writer ran out of buffer space */
  HEGEL_CBOR_ERR_TRUNCATED   = -2,  /* decoder hit end of buffer mid-item */
  HEGEL_CBOR_ERR_UNSUPPORTED = -3,  /* major type / additional info we don't decode */
  HEGEL_CBOR_ERR_OVERFLOW    = -4,  /* int doesn't fit requested width */
  HEGEL_CBOR_ERR_TYPE        = -5,  /* expected one type, got another */
  HEGEL_CBOR_ERR_NOT_FOUND   = -6   /* hegel_cbor_map_find: key not present */
} hegel_cbor_result;

typedef enum {
  HEGEL_CBOR_TYPE_UINT  = 0,
  HEGEL_CBOR_TYPE_NINT  = 1,
  HEGEL_CBOR_TYPE_BYTES = 2,
  HEGEL_CBOR_TYPE_TEXT  = 3,
  HEGEL_CBOR_TYPE_ARRAY = 4,
  HEGEL_CBOR_TYPE_MAP   = 5,
  HEGEL_CBOR_TYPE_TAG   = 6,
  HEGEL_CBOR_TYPE_BOOL  = 7,  /* major 7, info 20|21 */
  HEGEL_CBOR_TYPE_NULL  = 8,  /* major 7, info 22 — synthetic id */
  HEGEL_CBOR_TYPE_FLOAT = 9   /* major 7, info 25|26|27 — synthetic id */
} hegel_cbor_type;

/* ---------- Writer ---------- */

typedef struct {
  uint8_t * buf;
  size_t    cap;
  size_t    pos;
  hegel_cbor_result err;
} hegel_cbor_writer;

void hegel_cbor_writer_init (hegel_cbor_writer * w, uint8_t * buf, size_t cap);

/* Returns bytes written so far if no error, else negative result code. */
int  hegel_cbor_writer_finalize (const hegel_cbor_writer * w);

void hegel_cbor_write_uint   (hegel_cbor_writer * w, uint64_t v);
void hegel_cbor_write_int    (hegel_cbor_writer * w, int64_t v);
void hegel_cbor_write_bool   (hegel_cbor_writer * w, bool v);
void hegel_cbor_write_null   (hegel_cbor_writer * w);
void hegel_cbor_write_double (hegel_cbor_writer * w, double v);  /* always float64 */
void hegel_cbor_write_text   (hegel_cbor_writer * w, const char * s, size_t len);
void hegel_cbor_write_text_z (hegel_cbor_writer * w, const char * s);
void hegel_cbor_write_bytes  (hegel_cbor_writer * w, const uint8_t * b, size_t len);

/* Open a definite-length array/map header. Caller writes n items / n*2
** child items afterward. */
void hegel_cbor_write_array_header (hegel_cbor_writer * w, size_t n);
void hegel_cbor_write_map_header   (hegel_cbor_writer * w, size_t n_pairs);

/* ---------- Reader ---------- */

typedef struct {
  const uint8_t * buf;
  size_t          cap;
  size_t          pos;
} hegel_cbor_reader;

void hegel_cbor_reader_init (hegel_cbor_reader * r, const uint8_t * buf, size_t cap);

/* Peek the type of the next item without consuming it. */
hegel_cbor_result hegel_cbor_peek_type (const hegel_cbor_reader * r,
                                        hegel_cbor_type * out_type);

/* Read scalar items. Each consumes the item on success. */
hegel_cbor_result hegel_cbor_read_uint   (hegel_cbor_reader * r, uint64_t * out);
hegel_cbor_result hegel_cbor_read_int    (hegel_cbor_reader * r, int64_t * out);
hegel_cbor_result hegel_cbor_read_bool   (hegel_cbor_reader * r, bool * out);
hegel_cbor_result hegel_cbor_read_null   (hegel_cbor_reader * r);

/* Read a float of any encoded width (16/32/64-bit), widened to double. */
hegel_cbor_result hegel_cbor_read_double (hegel_cbor_reader * r, double * out);

/* Read a tag header (major 6), returning the tag number.  The tagged
** item follows and is read with the ordinary read functions. */
hegel_cbor_result hegel_cbor_read_tag    (hegel_cbor_reader * r, uint64_t * out_tag);

/* String/bytes — out_ptr aliases into r->buf, no copy. */
hegel_cbor_result hegel_cbor_read_text   (hegel_cbor_reader * r,
                                          const char ** out_ptr, size_t * out_len);
hegel_cbor_result hegel_cbor_read_bytes  (hegel_cbor_reader * r,
                                          const uint8_t ** out_ptr, size_t * out_len);

/* Container headers — return the count, advance past the header only. */
hegel_cbor_result hegel_cbor_read_array_header (hegel_cbor_reader * r, size_t * out_n);
hegel_cbor_result hegel_cbor_read_map_header   (hegel_cbor_reader * r, size_t * out_n_pairs);

/* Skip exactly one CBOR item (recurses through containers and tags). */
hegel_cbor_result hegel_cbor_skip (hegel_cbor_reader * r);

/*
** Find a top-level text-string key inside a map and position the reader at
** its value. Map header must be the next item; after success the reader is
** positioned to read the value of `key`. Other keys are skipped.
**
** Returns HEGEL_CBOR_ERR_NOT_FOUND if the key isn't present (reader state
** undefined in that case — typically you'd reset it).
*/
hegel_cbor_result hegel_cbor_map_find (hegel_cbor_reader * r, const char * key);

#endif /* HEGEL_CBOR_H */
