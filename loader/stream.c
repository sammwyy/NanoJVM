#include "loader/stream.h"

#include <string.h>

void jvm_stream_init(jvm_stream *s, const uint8_t *data, size_t len) {
  s->data = data;
  s->len = len;
  s->pos = 0;
}

int jvm_stream_has(const jvm_stream *s, size_t n) {
  return (s->pos + n) <= s->len;
}

size_t jvm_stream_remaining(const jvm_stream *s) { return s->len - s->pos; }

const uint8_t *jvm_stream_peek(const jvm_stream *s) { return s->data + s->pos; }

int jvm_stream_skip(jvm_stream *s, size_t n) {
  if (!jvm_stream_has(s, n))
    return -1;
  s->pos += n;
  return 0;
}

int jvm_stream_read_u8(jvm_stream *s, uint8_t *out) {
  if (!jvm_stream_has(s, 1))
    return -1;
  *out = s->data[s->pos++];
  return 0;
}

int jvm_stream_read_u16le(jvm_stream *s, uint16_t *out) {
  if (!jvm_stream_has(s, 2))
    return -1;
  *out = (uint16_t)(s->data[s->pos]) | ((uint16_t)(s->data[s->pos + 1]) << 8);
  s->pos += 2;
  return 0;
}

int jvm_stream_read_u32le(jvm_stream *s, uint32_t *out) {
  if (!jvm_stream_has(s, 4))
    return -1;
  *out = (uint32_t)(s->data[s->pos]) | ((uint32_t)(s->data[s->pos + 1]) << 8) |
         ((uint32_t)(s->data[s->pos + 2]) << 16) |
         ((uint32_t)(s->data[s->pos + 3]) << 24);
  s->pos += 4;
  return 0;
}

int jvm_stream_read_u16be(jvm_stream *s, uint16_t *out) {
  if (!jvm_stream_has(s, 2))
    return -1;
  *out = ((uint16_t)(s->data[s->pos]) << 8) | (uint16_t)(s->data[s->pos + 1]);
  s->pos += 2;
  return 0;
}

int jvm_stream_read_u32be(jvm_stream *s, uint32_t *out) {
  if (!jvm_stream_has(s, 4))
    return -1;
  *out = ((uint32_t)(s->data[s->pos]) << 24) |
         ((uint32_t)(s->data[s->pos + 1]) << 16) |
         ((uint32_t)(s->data[s->pos + 2]) << 8) |
         (uint32_t)(s->data[s->pos + 3]);
  s->pos += 4;
  return 0;
}

int jvm_stream_sub(jvm_stream *s, size_t len, jvm_stream *out) {
  if (!jvm_stream_has(s, len))
    return -1;
  jvm_stream_init(out, s->data + s->pos, len);
  s->pos += len;
  return 0;
}
