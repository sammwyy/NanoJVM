#ifndef JMEVM_LOADER_STREAM_H
#define JMEVM_LOADER_STREAM_H

#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * jmevm_stream — a read-only cursor over a flat byte buffer.
 * Zero-copy: the buffer is never owned by the stream.
 * ------------------------------------------------------------------------- */

typedef struct jmevm_stream {
  const uint8_t *data; /* base pointer                    */
  size_t len;          /* total length                    */
  size_t pos;          /* current read position           */
} jmevm_stream;

/* Initialise a stream over an existing buffer. */
void jmevm_stream_init(jmevm_stream *s, const uint8_t *data, size_t len);

/* Return 1 if at least `n` bytes remain, 0 otherwise. */
int jmevm_stream_has(const jmevm_stream *s, size_t n);

/* Read a single byte.  Returns -1 on underflow. */
int jmevm_stream_read_u8(jmevm_stream *s, uint8_t *out);

/* Read 2-byte little-endian uint16. */
int jmevm_stream_read_u16le(jmevm_stream *s, uint16_t *out);

/* Read 4-byte little-endian uint32. */
int jmevm_stream_read_u32le(jmevm_stream *s, uint32_t *out);

/* Read 2-byte big-endian uint16 (class-file byte order). */
int jmevm_stream_read_u16be(jmevm_stream *s, uint16_t *out);

/* Read 4-byte big-endian uint32 (class-file byte order). */
int jmevm_stream_read_u32be(jmevm_stream *s, uint32_t *out);

/* Skip `n` bytes.  Returns 0 on success, -1 on underflow. */
int jmevm_stream_skip(jmevm_stream *s, size_t n);

/* Return a pointer to the current position without advancing. */
const uint8_t *jmevm_stream_peek(const jmevm_stream *s);

/* Remaining bytes in the stream. */
size_t jmevm_stream_remaining(const jmevm_stream *s);

/* Create a sub-stream of exactly `len` bytes starting at current pos.
 * Advances `s` by `len`.  Returns 0 on success. */
int jmevm_stream_sub(jmevm_stream *s, size_t len, jmevm_stream *out);

#endif /* JMEVM_LOADER_STREAM_H */
