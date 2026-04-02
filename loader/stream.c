#include "loader/stream.h"

#include <string.h>

void jmevm_stream_init(jmevm_stream *s, const uint8_t *data, size_t len) {
  s->data = data;
  s->len = len;
  s->pos = 0;
}

int jmevm_stream_has(const jmevm_stream *s, size_t n) {
  return (s->pos + n) <= s->len;
}

size_t jmevm_stream_remaining(const jmevm_stream *s) { return s->len - s->pos; }

const uint8_t *jmevm_stream_peek(const jmevm_stream *s) {
  return s->data + s->pos;
}

int jmevm_stream_skip(jmevm_stream *s, size_t n) {
  if (!jmevm_stream_has(s, n))
    return -1;
  s->pos += n;
  return 0;
}

int jmevm_stream_read_u8(jmevm_stream *s, uint8_t *out) {
  if (!jmevm_stream_has(s, 1))
    return -1;
  *out = s->data[s->pos++];
  return 0;
}

int jmevm_stream_read_u16le(jmevm_stream *s, uint16_t *out) {
  if (!jmevm_stream_has(s, 2))
    return -1;
  *out = (uint16_t)(s->data[s->pos]) | ((uint16_t)(s->data[s->pos + 1]) << 8);
  s->pos += 2;
  return 0;
}

int jmevm_stream_read_u32le(jmevm_stream *s, uint32_t *out) {
  if (!jmevm_stream_has(s, 4))
    return -1;
  *out = (uint32_t)(s->data[s->pos]) | ((uint32_t)(s->data[s->pos + 1]) << 8) |
         ((uint32_t)(s->data[s->pos + 2]) << 16) |
         ((uint32_t)(s->data[s->pos + 3]) << 24);
  s->pos += 4;
  return 0;
}

int jmevm_stream_read_u16be(jmevm_stream *s, uint16_t *out) {
  if (!jmevm_stream_has(s, 2))
    return -1;
  *out = ((uint16_t)(s->data[s->pos]) << 8) | (uint16_t)(s->data[s->pos + 1]);
  s->pos += 2;
  return 0;
}

int jmevm_stream_read_u32be(jmevm_stream *s, uint32_t *out) {
  if (!jmevm_stream_has(s, 4))
    return -1;
  *out = ((uint32_t)(s->data[s->pos]) << 24) |
         ((uint32_t)(s->data[s->pos + 1]) << 16) |
         ((uint32_t)(s->data[s->pos + 2]) << 8) |
         (uint32_t)(s->data[s->pos + 3]);
  s->pos += 4;
  return 0;
}

int jmevm_stream_sub(jmevm_stream *s, size_t len, jmevm_stream *out) {
  if (!jmevm_stream_has(s, len))
    return -1;
  jmevm_stream_init(out, s->data + s->pos, len);
  s->pos += len;
  return 0;
}
