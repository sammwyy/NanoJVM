#include "loader/jar.h"
#include "loader/zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Read an entire file into a heap buffer.
 * ---------------------------------------------------------------------- */
static uint8_t *read_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long sz = ftell(f);
  if (sz <= 0) {
    fclose(f);
    return NULL;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }
  uint8_t *buf = (uint8_t *)malloc((size_t)sz);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *out_len = (size_t)sz;
  return buf;
}

/* -------------------------------------------------------------------------
 * Parse MANIFEST.MF to find the "Main-Class:" attribute.
 * ---------------------------------------------------------------------- */
static char *parse_main_class(const uint8_t *manifest, size_t len) {
  const char *src = (const char *)manifest;
  const char *end = src + len;
  const char *key = "Main-Class:";
  size_t klen = strlen(key);

  while (src < end) {
    /* Find line end */
    const char *nl = (const char *)memchr(src, '\n', (size_t)(end - src));
    size_t line_len = nl ? (size_t)(nl - src) : (size_t)(end - src);

    if (line_len >= klen && memcmp(src, key, klen) == 0) {
      const char *val = src + klen;
      size_t val_len = line_len - klen;
      /* strip leading whitespace */
      while (val_len > 0 && (*val == ' ' || *val == '\t')) {
        val++;
        val_len--;
      }
      /* strip trailing \r */
      while (val_len > 0 &&
             (val[val_len - 1] == '\r' || val[val_len - 1] == ' '))
        val_len--;
      if (val_len == 0)
        break;
      /* convert dots to slashes (java.lang.Foo → java/lang/Foo) */
      char *cls = (char *)malloc(val_len + 1);
      if (!cls)
        return NULL;
      memcpy(cls, val, val_len);
      cls[val_len] = '\0';
      for (size_t i = 0; i < val_len; i++)
        if (cls[i] == '.')
          cls[i] = '/';
      return cls;
    }
    src = nl ? nl + 1 : end;
  }
  return NULL;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
jvm_jar *jvm_jar_open(const char *path) {
  if (!path)
    return NULL;
  size_t len = 0;
  uint8_t *buf = read_file(path, &len);
  if (!buf)
    return NULL;

  jvm_zip *zip = jvm_zip_open(buf, len);
  if (!zip) {
    free(buf);
    return NULL;
  }

  jvm_jar *jar = (jvm_jar *)calloc(1, sizeof(*jar));
  if (!jar) {
    jvm_zip_close(zip);
    free(buf);
    return NULL;
  }
  jar->buf = buf;
  jar->len = len;
  jar->zip = zip;

  /* Try to read Main-Class from manifest */
  int mf_idx = jvm_zip_entry_find(zip, "META-INF/MANIFEST.MF");
  if (mf_idx >= 0) {
    size_t mf_len = 0;
    uint8_t *mf = jvm_zip_entry_read(zip, (size_t)mf_idx, &mf_len);
    if (mf) {
      jar->main_class = parse_main_class(mf, mf_len);
      free(mf);
    }
  }

  return jar;
}

void jvm_jar_close(jvm_jar *jar) {
  if (!jar)
    return;
  jvm_zip_close(jar->zip);
  free(jar->buf);
  free(jar->main_class);
  free(jar);
}

size_t jvm_jar_entry_count(const jvm_jar *jar) {
  return jar ? jvm_zip_entry_count(jar->zip) : 0;
}

int jvm_jar_find_class(const jvm_jar *jar, const char *class_name) {
  if (!jar || !class_name)
    return -1;
  size_t nlen = strlen(class_name);
  /* Build "com/example/Foo.class" */
  char *key = (char *)malloc(nlen + 7);
  if (!key)
    return -1;
  memcpy(key, class_name, nlen);
  memcpy(key + nlen, ".class", 7);
  int idx = jvm_zip_entry_find(jar->zip, key);
  free(key);
  return idx;
}

uint8_t *jvm_jar_read_entry(const jvm_jar *jar, size_t idx, size_t *out_len) {
  if (!jar)
    return NULL;
  return jvm_zip_entry_read(jar->zip, idx, out_len);
}

uint8_t *jvm_jar_read_class(const jvm_jar *jar, const char *class_name,
                            size_t *out_len) {
  int idx = jvm_jar_find_class(jar, class_name);
  if (idx < 0)
    return NULL;
  return jvm_zip_entry_read(jar->zip, (size_t)idx, out_len);
}
