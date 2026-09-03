#pragma once
// diagnostics/bone_dump_json.h — allocation-free JSON writer used by the
// bone scanner. Kept independent from Unity/IL2CPP so escaping and comma
// handling can be unit-tested directly.
#include <cstdio>

static void BoneScannerWriteJsonString(FILE *f, const char *text) {
  if (!f) return;
  fputc('"', f);
  const unsigned char *p =
      reinterpret_cast<const unsigned char *>(text ? text : "");
  for (; *p; ++p) {
    switch (*p) {
      case '"': fputs("\\\"", f); break;
      case '\\': fputs("\\\\", f); break;
      case '\b': fputs("\\b", f); break;
      case '\f': fputs("\\f", f); break;
      case '\n': fputs("\\n", f); break;
      case '\r': fputs("\\r", f); break;
      case '\t': fputs("\\t", f); break;
      default:
        if (*p < 0x20)
          fprintf(f, "\\u%04x", (unsigned int)*p);
        else
          fputc((int)*p, f);
        break;
    }
  }
  fputc('"', f);
}

static void BoneScannerWriteEntry(FILE *f, const char *name, int depth,
                                  bool &firstEntry) {
  if (!f) return;
  if (!firstEntry) fputs(",\n", f);
  firstEntry = false;
  fputs("    {\"name\": ", f);
  BoneScannerWriteJsonString(f, name);
  fprintf(f, ", \"depth\": %d}", depth);
}
