#ifndef JVM_LOADER_ZIP_H
#define JVM_LOADER_ZIP_H

#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Minimal PKZIP / JAR reader — stored (method 0) and deflated (method 8)
 * entries.  No external zlib dependency: deflate is decoded with a small
 * built-in implementation sufficient for class files.
 *
 * Usage:
 *   1. Map or read the whole .jar/.zip file into a flat byte buffer.
 *   2. Call jvm_zip_open() to parse the central directory.
 *   3. Iterate entries with jvm_zip_entry_count() / jvm_zip_entry_get().
 *   4. Extract a specific entry with jvm_zip_entry_read().
 *   5. Call jvm_zip_close() to release the index (not the source buffer).
 * ------------------------------------------------------------------------- */

/* Opaque archive handle. */
typedef struct jvm_zip jvm_zip;

/* Metadata for a single entry (name is NOT null-terminated here). */
typedef struct jvm_zip_entry {
  const char *name; /* pointer into zip buffer, NOT NUL-terminated */
  uint16_t name_len;
  uint32_t compressed_size;
  uint32_t uncompressed_size;
  uint16_t method; /* 0 = stored, 8 = deflated                    */
  uint32_t offset; /* offset of the LOCAL file header              */
} jvm_zip_entry;

/* Open a zip from a fully-loaded buffer.
 * Returns NULL on parse error.
 * The buffer must remain valid for the lifetime of the returned zip. */
jvm_zip *jvm_zip_open(const uint8_t *data, size_t len);

/* Free the zip index.  Does NOT free the source buffer. */
void jvm_zip_close(jvm_zip *z);

/* Number of entries in the archive. */
size_t jvm_zip_entry_count(const jvm_zip *z);

/* Get metadata for entry at index `i`.  Returns 0 on success. */
int jvm_zip_entry_get(const jvm_zip *z, size_t i, jvm_zip_entry *out);

/* Find an entry by name (exact match, case-sensitive).
 * name must be NUL-terminated.  Returns entry index or -1 if not found. */
int jvm_zip_entry_find(const jvm_zip *z, const char *name);

/* Extract entry at index `i` into a newly-allocated buffer.
 * *out_len is set to the uncompressed size.
 * Caller must free() the returned buffer.
 * Returns NULL on error. */
uint8_t *jvm_zip_entry_read(const jvm_zip *z, size_t i, size_t *out_len);

#endif /* JVM_LOADER_ZIP_H */
