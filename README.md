# nobex

Declarative dependency graph extension for [nob.h](https://github.com/tsoding/nob.h).

Instead of writing imperative build logic in `main()`, you declare targets as data. nobex discovers them at startup, resolves dependencies and runs the minimum necessary work — with optional parallel execution and a file-watch loop.

## Requirements

- [nob.h](https://github.com/tsoding/nob.h) in the same directory
- A C99-capable compiler
- `pthreads` on Linux/macOS (optional)

---

## Modes

nobex has three personalities, selected by a `#define` before the include:

| Define | What it emits |
|---|---|
| _(none)_ | Types and macros only — safe to include in other headers |
| `NOBEX_IMPLEMENTATION` | A `main()` that discovers targets and drives the build |
| `NOBEX_CLI` | A standalone `nobex` binary that compiles and runs your `build.c` |

---

## Quick start — project mode

Create `build.c`:

```c
#define NOB_IMPLEMENTATION
#include "nob.h"

#define NOBEX_DEFAULT_GROUP "build"
#define NOBEX_IMPLEMENTATION
#include "nobex.h"

NOB_ARTIFACT(myapp,
    .sources    = SRCS("main.c"),
    .output     = "myapp",
    .type       = TARGET_EXECUTABLE,
    .is_default = true,
);
```

Bootstrap and run:

```sh
cc build.c -o build -lpthread
./build
```

following then NOB_GO_REBUILD_YOURSELF, nobex recompiles `build.c` automatically when the source changes.

---

## Macros

### `NOB_ARTIFACT`

Declares a target that produces a file. The build command is generated from `.type`.

```c
NOB_ARTIFACT(mylib,
    .sources = SRCS("src/a.c", "src/b.c"),
    .output  = "build/libmylib.a",
    .type    = TARGET_STATIC_LIB,
    .cflags  = FLAGS("-Isrc"),
);

NOB_ARTIFACT(myapp,
    .sources    = SRCS("main.c"),
    .output     = "myapp",
    .type       = TARGET_EXECUTABLE,
    .deps       = DEPS("mylib"),
    .cflags     = FLAGS("-Isrc", "-O2"),
    .lflags     = FLAGS("-lm"),
    .is_default = true,
    .groups     = GROUPS("build", "ci"),
);
```

`.type` defaults to `TARGET_EXECUTABLE` when omitted. Supported types:

| Type | Compile step | Link step |
|---|---|---|
| `TARGET_EXECUTABLE` | `cc cflags -c src -o src.o` per source | `cc src.o... -o output lflags` |
| `TARGET_STATIC_LIB` | `cc cflags -c src... -o output.o` | `ar rcs output output.o` |
| `TARGET_SHARED_LIB` | — | `cc -shared cflags src... -o output lflags` |


### `NOB_RULE`

Like `NOB_ARTIFACT` but you supply the build function. Still checks mtime via `.sources` and `.output`.

```c
bool compile_shaders(Target *t, NobexContext *ctx)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "glslc", t->sources[0], "-o", t->output);
    return nob_cmd_run(&cmd);
}

NOB_RULE(shaders,
    .sources = SRCS("shaders/main.glsl"),
    .output  = "build/shaders.h",
    .run     = compile_shaders,
);
```

### `NOB_PHONY`

Declares a target with no output — always runs when invoked. Requires a matching `bool name(NobexContext*)` function declared before the macro.

```c
bool clean(NobexContext *ctx)
{
    nob_delete_file("myapp");
    return true;
}

NOB_PHONY(clean);

bool run(NobexContext *ctx)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "./myapp", "--port", "8080");
    return nob_cmd_run(&cmd);
}

NOB_PHONY(run, .deps = DEPS("myapp"));
```

`.deps` listed on a phony are built before the function is called.

---

## Helper macros

```c
SRCS(...)    // NULL-terminated array of source paths
DEPS(...)    // NULL-terminated array of target names
FLAGS(...)   // NULL-terminated array of flags
PKGS(...)    // NULL-terminated array of pkg-config package names
GROUPS(...)  // NULL-terminated array of group names
```

---

## Groups

Targets belong to one or more groups. Pass a group name as a positional argument to build only that subset.

```c
#define NOBEX_DEFAULT_GROUP "build"

NOB_ARTIFACT(docs,
    .sources = SRCS("src/main.c"),
    .output  = "docs/index.html",
    .groups  = GROUPS("doc"),
    .run     = generate_docs,
);

NOB_ARTIFACT(myapp,
    .sources    = SRCS("main.c"),
    .output     = "myapp",
    .groups     = GROUPS("build", "ci"),
    .is_default = true,
);
```

```sh
./build          # runs the default group ("build")
./build doc      # runs only the "doc" group
./build ci       # runs only the "ci" group
./build build doc  # runs both groups
```

Targets without `.groups` automatically inherit the default group. If `NOBEX_DEFAULT_GROUP` is not defined, the fallback is `"build"`.

---

## Store and `$(key)` expansion

A `NOB_PHONY` can inject key-value pairs into the context store. Any `NOB_ARTIFACT` that lists the phony in `.deps` can consume those values via `$(key)` expansion in `.cflags` or `.lflags`.

```c
bool deps(NobexContext *ctx)
{
    nobex_set(ctx, "raylib.cflags", "-I/usr/local/include/raylib");
    nobex_set(ctx, "raylib.lflags", "-lraylib -lm");
    return true;
}

NOB_PHONY(deps);

NOB_ARTIFACT(game,
    .sources = SRCS("main.c"),
    .output  = "game",
    .deps    = DEPS("deps"),
    .cflags  = FLAGS("$(raylib.cflags)"),
    .lflags  = FLAGS("$(raylib.lflags)"),
);
```

`deps` runs first because it appears in `.deps`. The store is populated before the expansion happens.

Store API:

```c
void        nobex_set(NobexContext *ctx, const char *key, const char *value);
const char *nobex_get(NobexContext *ctx, const char *key);
```

---

## xflags — flags embedded in source files

Source files can declare their own compiler flags in a contiguous block of `//> key : value` comments at the very top of the file. The parser stops at the first line that is not a `//> ...` comment.

```c
//> cflags : -O2 -Iimgui
//> lflags : -lm -lpthread

#include "mylib.h"
```

Enable it per-target with `.use_xflags = true`:

```c
NOB_ARTIFACT(imgui_core,
    .sources    = SRCS("imgui.cpp", "imgui_draw.cpp"),
    .output     = "build/libimgui.a",
    .type       = TARGET_STATIC_LIB,
    .use_xflags = true,
);
```

xflags accumulate on top of `.cflags` and `.lflags` — they never replace them.

Recognized keys:

| Key | Effect |
|---|---|
| `cflags` | Added to the compile step for that source |
| `lflags` | Added to the link step for the target |

The CLI also reads xflags from `build.c` to know what flags to use when compiling it.

Direct API:

```c
const char *nobex_xflags_get(const char *filepath, const char *key);

void nobex_xflags_each(const char *filepath,
                       void (*cb)(const char *key, const char *value, void *ud),
                       void *ud);
```

---

## Lifecycle hooks

Targets support three hooks. Each receives the `Target` and the `NobexContext`.

```c
void on_before(Target *t, NobexContext *ctx)
{
    // inject extra flags from the store, set up directories, etc.
}

void on_after(Target *t, NobexContext *ctx)
{
    // deploy, sign, copy, send a notification
}

void on_error(Target *t, NobexContext *ctx)
{
    // clean up partial output, notify
}

NOB_ARTIFACT(myapp,
    .sources         = SRCS("main.c"),
    .output          = "myapp",
    .on_before_build = on_before,
    .on_after_build  = on_after,
    .on_error        = on_error,
);
```

Hooks run regardless of whether `--watch` is active. To suppress them during watch, set `.watch.skip_hooks = true`.

---

## Watch mode

```sh
./build --watch        # watches the default group
./build --watch doc    # watches only the "doc" group
```

The watch loop polls mtime at `.watch.poll_ms` (default 50 ms) with a debounce of `.watch.debounce_ms` (default 100 ms). Set `.watch.skip = true` on a target to exclude it completely.

Per-target watch hooks:

```c
NOB_ARTIFACT(server,
    .sources = SRCS("main.c"),
    .output  = "server",
    .watch   = {
        .debounce_ms = 200,
        .on_built    = notify_reload,
        .on_error    = notify_failure,
    },
);
```

---

## Parallel builds

```sh
./build -j4       # up to 4 targets in parallel
./build -j 4      # same
```

Targets with no dependency relationship run concurrently. A failing target cancels pending siblings. Default is `-j1` (serial).

---

## CLI flags

| Flag | Description |
|---|---|
| `[group...]` | One or more groups to build; uses `NOBEX_DEFAULT_GROUP` if omitted |
| `-j<N>` | Run up to N targets in parallel |
| `--watch` | Watch mode on the specified group(s) |
| `-B` | Force rebuild of all targets, ignoring mtimes |
| `--dry-run` | Print commands without executing |
| `--verbose`, `-v` | Print each command before executing |
| `--list`, `-l` | Print all targets and their groups, then exit |
| `--help`, `-h` | Print groups and targets with descriptions, then exit |

---

## Configurable defines

All defines must be set before `#include "nobex.h"`.

| Define | Default | Description |
|---|---|---|
| `NOBEX_DEFAULT_GROUP` | `"build"` | Default group when no argument is passed |
| `NOBEX_SECTION_HASH` | `"a3f8c2d901"` | Linker section name; change if it collides |
| `NOBEX_CACHE_DIR` | `".nobex"` | Directory for compiled build scripts (CLI mode) |
| `NOBEX_AR` | `"ar"` / `"lib.exe"` | Archiver used for static libraries |
| `NOBEX_DEFAULT_DEBOUNCE_MS` | `100` | Watch debounce in milliseconds |
| `NOBEX_DEFAULT_POLL_MS` | `50` | Watch poll interval in milliseconds |

The C compiler is not overridden — nobex uses `nob_cc()` and `nob_cc_output()` from nob.h, so whatever compiler the user configures there is used automatically.

---

## Standalone CLI

### Bootstrap

```sh
cc -x c -DNOBEX_CLI nobex.h -o nobex -lpthread
```

The `-x c` flag is required because `.h` files are not treated as C source by default.

### What the CLI does

When you run `nobex` in a directory, it:

1. Looks for `nob.c` or `build.c`
2. Reads `cflags` and `lflags` xflags from that file to get compilation flags
3. Compiles it to `.nobex/<filename>` and manages Recompilation if necessary
4. Passes all remaining arguments to the build script

```sh
nobex                  # auto-discovers build.c, compiles if needed, runs it
nobex all              # passes "all" as a group argument
nobex run build.c      # explicit source file
nobex --watch doc
nobex -j4 ci
nobex --list
```

### Building the CLI with nobex itself

Once bootstrapped, `build.c` can declare nobex as a target so it rebuilds when `nobex.h` changes:

```c
NOB_ARTIFACT(nobex,
    .sources = SRCS("nobex.h"),
    .output  = "nobex",
    .type    = TARGET_EXECUTABLE,
    .cflags  = FLAGS("-x", "c", "-DNOBEX_CLI"),
    .lflags  = FLAGS("-lpthread"),
);
```

---

## Complete example

A project that builds a static library, links an executable against it, runs tests, and deploys on success.

```c
//> lflags : -lpthread

#define NOB_IMPLEMENTATION
#include "nob.h"

#define NOBEX_DEFAULT_GROUP "build"
#define NOBEX_IMPLEMENTATION
#include "nobex.h"

/* ── hooks ── */

void after_deploy(Target *t, NobexContext *ctx)
{
    (void)t; (void)ctx;
    nob_log(NOB_INFO, "deployed to /usr/local/bin");
}

/* ── rules ── */

bool gen_version(Target *t, NobexContext *ctx)
{
    (void)ctx;
    FILE *f = fopen(t->output, "w");
    if (!f) return false;
    fprintf(f, "#define VERSION \"1.0.0\"\n");
    fclose(f);
    return true;
}

/* ── phonies ── */

bool clean(NobexContext *ctx)
{
    (void)ctx;
    nob_delete_file("myapp");
    nob_delete_file("build/libcore.a");
    nob_delete_file("build/version.h");
    return true;
}

bool run_tests(NobexContext *ctx)
{
    if (!nobex_run(ctx, "tests")) return false;
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "./tests");
    return nob_cmd_run(&cmd);
}

bool install(NobexContext *ctx)
{
    return nob_copy_file(nobex_output(ctx, "myapp"), "/usr/local/bin/myapp");
}

/* ── targets ── */

NOB_RULE(version_h,
    .sources = SRCS("build.c"),
    .output  = "build/version.h",
    .run     = gen_version,
);

NOB_ARTIFACT(core,
    .sources = SRCS("src/core.c", "src/util.c"),
    .output  = "build/libcore.a",
    .type    = TARGET_STATIC_LIB,
    .cflags  = FLAGS("-Isrc"),
);

NOB_ARTIFACT(myapp,
    .sources        = SRCS("src/main.c"),
    .output         = "myapp",
    .type           = TARGET_EXECUTABLE,
    .deps           = DEPS("core", "version_h"),
    .cflags         = FLAGS("-Isrc", "-Ibuild"),
    .lflags         = FLAGS("-Lbuild", "-lcore"),
    .groups         = GROUPS("build", "ci"),
    .is_default     = true,
    .on_after_build = after_deploy,
);

NOB_ARTIFACT(tests,
    .sources = SRCS("tests/test_core.c"),
    .output  = "tests/test_core",
    .type    = TARGET_EXECUTABLE,
    .deps    = DEPS("core"),
    .cflags  = FLAGS("-Isrc"),
    .lflags  = FLAGS("-Lbuild", "-lcore"),
    .groups  = GROUPS("test"),
);

NOB_PHONY(clean);
NOB_PHONY(install, .deps = DEPS("myapp"));
NOB_PHONY(run_tests, .groups = GROUPS("test"));
```

```sh
cc build.c -o build -lpthread

./build              # compiles core + myapp, calls after_deploy
./build test         # compiles tests, runs test binary
./build clean
./build install
./build --watch
./build -j2 ci
./build --dry-run ci
./build --list
```
