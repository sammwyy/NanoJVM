#include "loader/zip.h"
#include "loader/stream.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * ZIP format constants
 * ---------------------------------------------------------------------- */
#define ZIP_SIG_LOCAL 0x04034b50UL
#define ZIP_SIG_CENTRAL 0x02014b50UL
#define ZIP_SIG_EOCD 0x06054b50UL
#define ZIP_SIG_EOCD64 0x06064b50UL

#define ZIP_METHOD_STORED 0
#define ZIP_METHOD_DEFLATED 8

/* -------------------------------------------------------------------------
 * Internal entry index
 * ---------------------------------------------------------------------- */
typedef struct zip_entry_idx {
  uint32_t offset; /* offset of local file header              */
  uint32_t compressed_size;
  uint32_t uncompressed_size;
  uint16_t method;
  uint16_t name_len;
  const char *name; /* points into the source buffer            */
} zip_entry_idx;

struct jvm_zip {
  const uint8_t *data;
  size_t len;
  zip_entry_idx *entries;
  size_t count;
};

/* -------------------------------------------------------------------------
 * Locate EOCD (End Of Central Directory) record
 * ---------------------------------------------------------------------- */
static int find_eocd(const uint8_t *data, size_t len, size_t *eocd_off) {
  if (len < 22)
    return -1;
  /* Scan backwards — comment may follow EOCD, max 65535 bytes */
  size_t limit = (len > 65535 + 22) ? len - 65535 - 22 : 0;
  size_t i = len - 22;
  while (1) {
    uint32_t sig = (uint32_t)data[i] | ((uint32_t)data[i + 1] << 8) |
                   ((uint32_t)data[i + 2] << 16) |
                   ((uint32_t)data[i + 3] << 24);
    if (sig == ZIP_SIG_EOCD) {
      *eocd_off = i;
      return 0;
    }
    if (i == limit)
      break;
    i--;
  }
  return -1;
}

/* -------------------------------------------------------------------------
 * Open / close
 * ---------------------------------------------------------------------- */
jvm_zip *jvm_zip_open(const uint8_t *data, size_t len) {
  if (!data || len < 22)
    return NULL;

  size_t eocd_off = 0;
  if (find_eocd(data, len, &eocd_off) != 0)
    return NULL;

  jvm_stream eocd;
  jvm_stream_init(&eocd, data + eocd_off, len - eocd_off);

  uint32_t sig = 0;
  uint16_t disk_num, disk_start, local_entries, total_entries;
  uint32_t cd_size, cd_offset;
  uint16_t comment_len;

  if (jvm_stream_read_u32le(&eocd, &sig) != 0)
    return NULL;
  if (jvm_stream_read_u16le(&eocd, &disk_num) != 0)
    return NULL;
  if (jvm_stream_read_u16le(&eocd, &disk_start) != 0)
    return NULL;
  if (jvm_stream_read_u16le(&eocd, &local_entries) != 0)
    return NULL;
  if (jvm_stream_read_u16le(&eocd, &total_entries) != 0)
    return NULL;
  if (jvm_stream_read_u32le(&eocd, &cd_size) != 0)
    return NULL;
  if (jvm_stream_read_u32le(&eocd, &cd_offset) != 0)
    return NULL;
  if (jvm_stream_read_u16le(&eocd, &comment_len) != 0)
    return NULL;
  (void)disk_num;
  (void)disk_start;
  (void)comment_len;
  (void)cd_size;

  if ((size_t)cd_offset >= len)
    return NULL;

  jvm_zip *z = (jvm_zip *)calloc(1, sizeof(*z));
  if (!z)
    return NULL;
  z->data = data;
  z->len = len;

  if (total_entries == 0)
    return z;

  z->entries = (zip_entry_idx *)calloc(total_entries, sizeof(zip_entry_idx));
  if (!z->entries) {
    free(z);
    return NULL;
  }

  jvm_stream cd;
  jvm_stream_init(&cd, data + cd_offset, len - cd_offset);

  for (uint16_t i = 0; i < total_entries; i++) {
    uint32_t csig = 0;
    if (jvm_stream_read_u32le(&cd, &csig) != 0)
      break;
    if (csig != ZIP_SIG_CENTRAL)
      break;

    uint16_t ver_made, ver_need, flags, method;
    uint32_t mod_time, crc32_val, comp_sz, uncomp_sz;
    uint16_t fname_len, extra_len, comment_len2;
    uint16_t disk_start2, int_attr;
    uint32_t ext_attr, local_off;

    if (jvm_stream_read_u16le(&cd, &ver_made) != 0)
      break;
    if (jvm_stream_read_u16le(&cd, &ver_need) != 0)
      break;
    if (jvm_stream_read_u16le(&cd, &flags) != 0)
      break;
    if (jvm_stream_read_u16le(&cd, &method) != 0)
      break;
    if (jvm_stream_read_u32le(&cd, &mod_time) != 0)
      break;
    if (jvm_stream_read_u32le(&cd, &crc32_val) != 0)
      break;
    if (jvm_stream_read_u32le(&cd, &comp_sz) != 0)
      break;
    if (jvm_stream_read_u32le(&cd, &uncomp_sz) != 0)
      break;
    if (jvm_stream_read_u16le(&cd, &fname_len) != 0)
      break;
    if (jvm_stream_read_u16le(&cd, &extra_len) != 0)
      break;
    if (jvm_stream_read_u16le(&cd, &comment_len2) != 0)
      break;
    if (jvm_stream_read_u16le(&cd, &disk_start2) != 0)
      break;
    if (jvm_stream_read_u16le(&cd, &int_attr) != 0)
      break;
    if (jvm_stream_read_u32le(&cd, &ext_attr) != 0)
      break;
    if (jvm_stream_read_u32le(&cd, &local_off) != 0)
      break;
    (void)ver_made;
    (void)ver_need;
    (void)flags;
    (void)mod_time;
    (void)crc32_val;
    (void)disk_start2;
    (void)int_attr;
    (void)ext_attr;

    if (!jvm_stream_has(&cd, fname_len))
      break;
    zip_entry_idx *e = &z->entries[z->count++];
    e->name = (const char *)(cd.data + cd.pos);
    e->name_len = fname_len;
    e->method = method;
    e->compressed_size = comp_sz;
    e->uncompressed_size = uncomp_sz;
    e->offset = local_off;

    if (jvm_stream_skip(&cd, fname_len) != 0)
      break;
    if (jvm_stream_skip(&cd, extra_len) != 0)
      break;
    if (jvm_stream_skip(&cd, comment_len2) != 0)
      break;
  }

  return z;
}

void jvm_zip_close(jvm_zip *z) {
  if (!z)
    return;
  free(z->entries);
  free(z);
}

size_t jvm_zip_entry_count(const jvm_zip *z) { return z ? z->count : 0; }

int jvm_zip_entry_get(const jvm_zip *z, size_t i, jvm_zip_entry *out) {
  if (!z || !out || i >= z->count)
    return -1;
  const zip_entry_idx *e = &z->entries[i];
  out->name = e->name;
  out->name_len = e->name_len;
  out->compressed_size = e->compressed_size;
  out->uncompressed_size = e->uncompressed_size;
  out->method = e->method;
  out->offset = e->offset;
  return 0;
}

int jvm_zip_entry_find(const jvm_zip *z, const char *name) {
  if (!z || !name)
    return -1;
  size_t nlen = strlen(name);
  for (size_t i = 0; i < z->count; i++) {
    const zip_entry_idx *e = &z->entries[i];
    if (e->name_len == (uint16_t)nlen && memcmp(e->name, name, nlen) == 0) {
      return (int)i;
    }
  }
  return -1;
}

/* -------------------------------------------------------------------------
 * Inflate — minimal DEFLATE decoder (RFC 1951)
 * Sufficient for class files; handles fixed and dynamic Huffman blocks.
 * ---------------------------------------------------------------------- */

/* Bit reader */
typedef struct {
  const uint8_t *src;
  size_t src_len;
  size_t src_pos;
  uint32_t bits;
  int nbits;
} bit_reader;

static void br_init(bit_reader *b, const uint8_t *src, size_t len) {
  b->src = src;
  b->src_len = len;
  b->src_pos = 0;
  b->bits = 0;
  b->nbits = 0;
}

static uint32_t br_peek(bit_reader *b, int n) {
  while (b->nbits < n) {
    if (b->src_pos >= b->src_len)
      break;
    b->bits |= ((uint32_t)b->src[b->src_pos++]) << b->nbits;
    b->nbits += 8;
  }
  return b->bits & ((1u << n) - 1u);
}

static void br_consume(bit_reader *b, int n) {
  b->bits >>= n;
  b->nbits -= n;
}

static uint32_t br_read(bit_reader *b, int n) {
  uint32_t v = br_peek(b, n);
  br_consume(b, n);
  return v;
}

/* Huffman table (canonical codes, limited depth) */
#define HUFF_MAX_BITS 15
#define HUFF_MAX_SYM 288

typedef struct {
  uint16_t counts[HUFF_MAX_BITS + 1]; /* count of codes of each length */
  uint16_t symbols[HUFF_MAX_SYM];     /* symbols ordered by code       */
  int max_bits;
} huff_table;

static int huff_build(huff_table *t, const uint8_t *lens, int nsyms) {
  int bl_count[HUFF_MAX_BITS + 1] = {0};
  int next_code[HUFF_MAX_BITS + 2] = {0};
  memset(t->counts, 0, sizeof(t->counts));
  t->max_bits = 0;

  for (int i = 0; i < nsyms; i++) {
    if (lens[i] > HUFF_MAX_BITS)
      return -1;
    bl_count[(int)lens[i]]++;
    if (lens[i] > t->max_bits)
      t->max_bits = lens[i];
  }
  bl_count[0] = 0;

  int code = 0;
  for (int bits = 1; bits <= HUFF_MAX_BITS; bits++) {
    code = (code + bl_count[bits - 1]) << 1;
    next_code[bits] = code;
  }

  /* Fill symbol table: sorted by (len, symbol) */
  memset(t->symbols, 0, sizeof(t->symbols));
  uint16_t offsets[HUFF_MAX_BITS + 1] = {0};
  offsets[1] = 0;
  for (int i = 1; i < HUFF_MAX_BITS; i++)
    offsets[i + 1] = (uint16_t)(offsets[i] + bl_count[i]);
  t->counts[0] = 0;
  for (int i = 1; i <= HUFF_MAX_BITS; i++)
    t->counts[i] = (uint16_t)bl_count[i];
  for (int i = 0; i < nsyms; i++) {
    if (lens[i])
      t->symbols[offsets[(int)lens[i]]++] = (uint16_t)i;
  }
  (void)next_code;
  return 0;
}

static int huff_decode(bit_reader *b, const huff_table *t) {
  int code = 0, first = 0, index = 0;
  for (int j = 1; j <= t->max_bits; j++) {
    int bit = (int)br_read(b, 1);
    code = (code << 1) | bit;
    int count = t->counts[j];
    if (code - count < first) {
      return t->symbols[index + (code - first)];
    }
    index += count;
    first = (first + count) << 1;
  }
  return -1;
}

/* Fixed Huffman tables (RFC 1951 §3.2.6) */
static void make_fixed_litlen(huff_table *t) {
  uint8_t lens[288];
  int i;
  for (i = 0; i < 144; i++)
    lens[i] = 8;
  for (; i < 256; i++)
    lens[i] = 9;
  for (; i < 280; i++)
    lens[i] = 7;
  for (; i < 288; i++)
    lens[i] = 8;
  huff_build(t, lens, 288);
}
static void make_fixed_dist(huff_table *t) {
  uint8_t lens[32];
  for (int i = 0; i < 32; i++)
    lens[i] = 5;
  huff_build(t, lens, 32);
}

/* Length / distance extra bits (RFC 1951 tables) */
static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                                      1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                      4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t len_base[29] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t dist_extra[30] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                       4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                       9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const uint16_t dist_base[30] = {
    1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

static int inflate_block(bit_reader *br, const huff_table *ll,
                         const huff_table *dist, uint8_t *out, size_t out_cap,
                         size_t *out_pos) {
  for (;;) {
    int sym = huff_decode(br, ll);
    if (sym < 0)
      return -1;
    if (sym < 256) {
      if (*out_pos >= out_cap)
        return -1;
      out[(*out_pos)++] = (uint8_t)sym;
    } else if (sym == 256) {
      break; /* end of block */
    } else {
      /* length / distance back-reference */
      int lsym = sym - 257;
      if (lsym >= 29)
        return -1;
      uint32_t length = len_base[lsym] + br_read(br, len_extra[lsym]);

      int dsym = huff_decode(br, dist);
      if (dsym < 0 || dsym >= 30)
        return -1;
      uint32_t distance = dist_base[dsym] + br_read(br, dist_extra[dsym]);

      if (distance > *out_pos)
        return -1;
      if (*out_pos + length > out_cap)
        return -1;
      for (uint32_t k = 0; k < length; k++) {
        out[*out_pos] = out[*out_pos - distance];
        (*out_pos)++;
      }
    }
  }
  return 0;
}

static uint8_t *do_inflate(const uint8_t *src, size_t src_len, size_t out_cap,
                           size_t *out_len) {
  uint8_t *out = (uint8_t *)malloc(out_cap + 1);
  if (!out)
    return NULL;
  size_t out_pos = 0;

  bit_reader br;
  br_init(&br, src, src_len);

  int bfinal = 0;
  while (!bfinal) {
    bfinal = (int)br_read(&br, 1);
    int btype = (int)br_read(&br, 2);

    if (btype == 0) {
      /* Stored block */
      br_consume(&br, br.nbits & 7); /* byte-align */
      /* consume from src directly */
      size_t pos = br.src_pos - (size_t)(br.nbits / 8);
      br.nbits = 0;
      br.bits = 0;
      if (pos + 4 > src_len) {
        free(out);
        return NULL;
      }
      uint16_t blen = (uint16_t)src[pos] | ((uint16_t)src[pos + 1] << 8);
      uint16_t bnlen = (uint16_t)src[pos + 2] | ((uint16_t)src[pos + 3] << 8);
      (void)bnlen;
      pos += 4;
      if (pos + blen > src_len) {
        free(out);
        return NULL;
      }
      if (out_pos + blen > out_cap) {
        free(out);
        return NULL;
      }
      memcpy(out + out_pos, src + pos, blen);
      out_pos += blen;
      br.src_pos = pos + blen;
    } else if (btype == 1) {
      /* Fixed Huffman */
      huff_table ll, dist;
      make_fixed_litlen(&ll);
      make_fixed_dist(&dist);
      if (inflate_block(&br, &ll, &dist, out, out_cap, &out_pos) != 0) {
        free(out);
        return NULL;
      }
    } else if (btype == 2) {
      /* Dynamic Huffman */
      uint32_t hlit = br_read(&br, 5) + 257;
      uint32_t hdist = br_read(&br, 5) + 1;
      uint32_t hclen = br_read(&br, 4) + 4;

      static const uint8_t clen_order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                             11, 4,  12, 3, 13, 2, 14, 1, 15};
      uint8_t clen_lens[19] = {0};
      for (uint32_t i = 0; i < hclen; i++)
        clen_lens[clen_order[i]] = (uint8_t)br_read(&br, 3);

      huff_table clen_t;
      if (huff_build(&clen_t, clen_lens, 19) != 0) {
        free(out);
        return NULL;
      }

      uint8_t all_lens[320] = {0};
      uint32_t total = hlit + hdist;
      for (uint32_t i = 0; i < total;) {
        int sym = huff_decode(&br, &clen_t);
        if (sym < 0) {
          free(out);
          return NULL;
        }
        if (sym < 16) {
          all_lens[i++] = (uint8_t)sym;
        } else if (sym == 16) {
          uint32_t rep = 3 + br_read(&br, 2);
          if (i == 0) {
            free(out);
            return NULL;
          }
          for (uint32_t k = 0; k < rep && i < total; k++) {
            uint8_t last = all_lens[i > 0 ? i - 1 : 0];
            all_lens[i++] = last;
          }
        } else if (sym == 17) {
          uint32_t rep = 3 + br_read(&br, 3);
          for (uint32_t k = 0; k < rep && i < total; k++)
            all_lens[i++] = 0;
        } else {
          uint32_t rep = 11 + br_read(&br, 7);
          for (uint32_t k = 0; k < rep && i < total; k++)
            all_lens[i++] = 0;
        }
      }
      huff_table ll, dist_t;
      if (huff_build(&ll, all_lens, (int)hlit) != 0) {
        free(out);
        return NULL;
      }
      if (huff_build(&dist_t, all_lens + hlit, (int)hdist) != 0) {
        free(out);
        return NULL;
      }
      if (inflate_block(&br, &ll, &dist_t, out, out_cap, &out_pos) != 0) {
        free(out);
        return NULL;
      }
    } else {
      free(out);
      return NULL;
    }
  }
  *out_len = out_pos;
  return out;
}

/* -------------------------------------------------------------------------
 * Entry extraction
 * ---------------------------------------------------------------------- */
uint8_t *jvm_zip_entry_read(const jvm_zip *z, size_t i, size_t *out_len) {
  if (!z || i >= z->count || !out_len)
    return NULL;
  const zip_entry_idx *e = &z->entries[i];

  /* Parse local file header to find data offset */
  if ((size_t)e->offset + 30 > z->len)
    return NULL;
  jvm_stream lhdr;
  jvm_stream_init(&lhdr, z->data + e->offset, z->len - e->offset);

  uint32_t lsig = 0;
  uint16_t lver, lflags, lmethod, ltime, ldate;
  uint32_t lcrc, lcomp, luncomp;
  uint16_t lfname, lextra;

  if (jvm_stream_read_u32le(&lhdr, &lsig) != 0)
    return NULL;
  if (lsig != ZIP_SIG_LOCAL)
    return NULL;
  if (jvm_stream_read_u16le(&lhdr, &lver) != 0)
    return NULL;
  if (jvm_stream_read_u16le(&lhdr, &lflags) != 0)
    return NULL;
  if (jvm_stream_read_u16le(&lhdr, &lmethod) != 0)
    return NULL;
  if (jvm_stream_read_u16le(&lhdr, &ltime) != 0)
    return NULL;
  if (jvm_stream_read_u16le(&lhdr, &ldate) != 0)
    return NULL;
  if (jvm_stream_read_u32le(&lhdr, &lcrc) != 0)
    return NULL;
  if (jvm_stream_read_u32le(&lhdr, &lcomp) != 0)
    return NULL;
  if (jvm_stream_read_u32le(&lhdr, &luncomp) != 0)
    return NULL;
  if (jvm_stream_read_u16le(&lhdr, &lfname) != 0)
    return NULL;
  if (jvm_stream_read_u16le(&lhdr, &lextra) != 0)
    return NULL;
  (void)lver;
  (void)lflags;
  (void)ltime;
  (void)ldate;
  (void)lcrc;
  (void)luncomp;

  if (jvm_stream_skip(&lhdr, lfname) != 0)
    return NULL;
  if (jvm_stream_skip(&lhdr, lextra) != 0)
    return NULL;

  /* Use sizes from central directory (more reliable with data descriptors) */
  uint32_t comp_sz = e->compressed_size;
  uint32_t uncomp_sz = e->uncompressed_size;
  if (!jvm_stream_has(&lhdr, comp_sz))
    return NULL;

  const uint8_t *comp_data = jvm_stream_peek(&lhdr);

  if (lmethod == ZIP_METHOD_STORED) {
    uint8_t *buf = (uint8_t *)malloc(comp_sz + 1);
    if (!buf)
      return NULL;
    memcpy(buf, comp_data, comp_sz);
    *out_len = comp_sz;
    return buf;
  }

  if (lmethod == ZIP_METHOD_DEFLATED) {
    size_t actual = 0;
    uint8_t *buf = do_inflate(comp_data, comp_sz, uncomp_sz, &actual);
    if (!buf)
      return NULL;
    *out_len = actual;
    return buf;
  }

  return NULL; /* unsupported method */
}
