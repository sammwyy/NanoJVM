#ifndef JVM_LOADER_STREAM_H
#define JVM_LOADER_STREAM_H

#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * jvm_stream — a read-only cursor over a flat byte buffer.
 * Zero-copy: the buffer is never owned by the stream.
 * ------------------------------------------------------------------------- */

typedef struct jvm_stream {
  const uint8_t *data; /* base pointer                    */
  size_t len;          /* total length                    */
  size_t pos;          /* current read position           */
} jvm_stream;

/* Initialise a stream over an existing buffer. */
void jvm_stream_init(jvm_stream *s, const uint8_t *data, size_t len);

/* Return 1 if at least `n` bytes remain, 0 otherwise. */
int jvm_stream_has(const jvm_stream *s, size_t n);

/* Read a single byte.  Returns -1 on underflow. */
int jvm_stream_read_u8(jvm_stream *s, uint8_t *out);

/* Read 2-byte little-endian uint16. */
int jvm_stream_read_u16le(jvm_stream *s, uint16_t *out);

/* Read 4-byte little-endian uint32. */
int jvm_stream_read_u32le(jvm_stream *s, uint32_t *out);

/* Read 2-byte big-endian uint16 (class-file byte order). */
int jvm_stream_read_u16be(jvm_stream *s, uint16_t *out);

/* Read 4-byte big-endian uint32 (class-file byte order). */
int jvm_stream_read_u32be(jvm_stream *s, uint32_t *out);

/* Skip `n` bytes.  Returns 0 on success, -1 on underflow. */
int jvm_stream_skip(jvm_stream *s, size_t n);

/* Return a pointer to the current position without advancing. */
const uint8_t *jvm_stream_peek(const jvm_stream *s);

/* Remaining bytes in the stream. */
size_t jvm_stream_remaining(const jvm_stream *s);

/* Create a sub-stream of exactly `len` bytes starting at current pos.
 * Advances `s` by `len`.  Returns 0 on success. */
int jvm_stream_sub(jvm_stream *s, size_t len, jvm_stream *out);

#endif /* JVM_LOADER_STREAM_H */
