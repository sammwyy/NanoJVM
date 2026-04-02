#ifndef JMEVM_LOADER_ZIP_H
#define JMEVM_LOADER_ZIP_H

#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Minimal PKZIP / JAR reader — stored (method 0) and deflated (method 8)
 * entries.  No external zlib dependency: deflate is decoded with a small
 * built-in implementation sufficient for class files.
 *
 * Usage:
 *   1. Map or read the whole .jar/.zip file into a flat byte buffer.
 *   2. Call jmevm_zip_open() to parse the central directory.
 *   3. Iterate entries with jmevm_zip_entry_count() / jmevm_zip_entry_get().
 *   4. Extract a specific entry with jmevm_zip_entry_read().
 *   5. Call jmevm_zip_close() to release the index (not the source buffer).
 * ------------------------------------------------------------------------- */

/* Opaque archive handle. */
typedef struct jmevm_zip jmevm_zip;

/* Metadata for a single entry (name is NOT null-terminated here). */
typedef struct jmevm_zip_entry {
  const char *name; /* pointer into zip buffer, NOT NUL-terminated */
  uint16_t name_len;
  uint32_t compressed_size;
  uint32_t uncompressed_size;
  uint16_t method; /* 0 = stored, 8 = deflated                    */
  uint32_t offset; /* offset of the LOCAL file header              */
} jmevm_zip_entry;

/* Open a zip from a fully-loaded buffer.
 * Returns NULL on parse error.
 * The buffer must remain valid for the lifetime of the returned zip. */
jmevm_zip *jmevm_zip_open(const uint8_t *data, size_t len);

/* Free the zip index.  Does NOT free the source buffer. */
void jmevm_zip_close(jmevm_zip *z);

/* Number of entries in the archive. */
size_t jmevm_zip_entry_count(const jmevm_zip *z);

/* Get metadata for entry at index `i`.  Returns 0 on success. */
int jmevm_zip_entry_get(const jmevm_zip *z, size_t i, jmevm_zip_entry *out);

/* Find an entry by name (exact match, case-sensitive).
 * name must be NUL-terminated.  Returns entry index or -1 if not found. */
int jmevm_zip_entry_find(const jmevm_zip *z, const char *name);

/* Extract entry at index `i` into a newly-allocated buffer.
 * *out_len is set to the uncompressed size.
 * Caller must free() the returned buffer.
 * Returns NULL on error. */
uint8_t *jmevm_zip_entry_read(const jmevm_zip *z, size_t i, size_t *out_len);

#endif /* JMEVM_LOADER_ZIP_H */
