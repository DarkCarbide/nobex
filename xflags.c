#define NOB_IMPLEMENTATION
#include "nob.h"

#include <stdio.h>

int main(int argc, char **argv) {
  const char *program = nob_shift_args(&argc, &argv);
  if (argc < 1) {
    nob_log(NOB_ERROR, "Usage: %s <source.c>", program);
    return 1;
  }

  const char *wanted_key = NULL;
  const char *path = NULL;

  while (argc > 0) {
    const char *arg = nob_shift_args(&argc, &argv);

    if (strcmp(arg, "-m") == 0) {
      if (argc == 0) {
        nob_log(NOB_ERROR, "-m expects a key");
        return 1;
      }
      wanted_key = nob_shift_args(&argc, &argv);
    } else {
      if (path) {
        nob_log(NOB_ERROR, "Too many arguments provided\n");
        return 1;
      }
      path = arg;
    }
  }

  if (!path) {
    nob_log(NOB_ERROR, "No input file provided");
    return 1;
  }

  Nob_String_Builder sb = {0};
  if (!nob_read_entire_file(path, &sb)) {
    nob_log(NOB_ERROR, "Could not read file: %s", path);
    return 1;
  }

  Nob_String_View source = nob_sb_to_sv(sb);
  int processed = 0;

  while (source.count > 0) {
    Nob_String_View line = nob_sv_chop_by_delim(&source, '\n');
    line = nob_sv_trim(line);

    /* Metadata must be contiguous and start immediately */
    if (!nob_sv_starts_with(line, nob_sv_from_cstr("//>"))) {
      break;
    }

    /* Strip "//>" */
    line.data += 3;
    line.count -= 3;
    line = nob_sv_trim(line);

    /* key : value */
    Nob_String_View key = nob_sv_chop_by_delim(&line, ':');
    Nob_String_View value = line;

    key = nob_sv_trim(key);
    value = nob_sv_trim(value);

    /* Validate key/value */
    if (key.count == 0 || value.count == 0) {
      break;
    }

    if (wanted_key) {
      if (strncmp(key.data, wanted_key, key.count) == 0) {
        fprintf(stdout, "%.*s", (int)value.count, value.data);
        break;
      }
    } else {
      printf("%.*s = %.*s\n", (int)key.count, key.data, (int)value.count,
             value.data);
    }

    processed++;
  }

  if (!wanted_key) {
    fprintf(stderr, "processed %d metadata line(s)\n", processed);
  }

  nob_sb_free(sb);
  return 0;
}
