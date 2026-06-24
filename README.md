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
);
```

Bootstrap and run:

```sh
cc build.c -o build -lpthread
./build
```

nobex recompiles `build.c` automatically when the source changes.

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
    .sources      = SRCS("main.c"),
    .inputs       = INPUTS("include/mylib.h", "include/config.h"),
    .output       = "myapp",
    .type         = TARGET_EXECUTABLE,
    .deps         = DEPS("mylib"),
    .cflags       = FLAGS("-Isrc", "-O2"),
    .lflags       = FLAGS("-lm"),
    .groups       = GROUPS("build", "ci"),
    .description  = "main application binary",
);
```

`.type` defaults to `TARGET_EXECUTABLE` when omitted. Supported types:

| Type | Compile step | Link step |
|---|---|---|
| `TARGET_EXECUTABLE` | `cc cflags -c src -o src.o` per source | `cc src.o... -o output lflags` |
| `TARGET_STATIC_LIB` | `cc cflags -c src -o src.o` per source | `ar rcs output src.o...` |
| `TARGET_SHARED_LIB` | — | `cc -shared cflags src... -o output lflags` |

`.inputs` lists files that participate in the mtime check but are not compiled individually — headers, generated files, scripts, or any other dependency. If any input is newer than the output, the target rebuilds.

```c
NOB_ARTIFACT(myapp,
    .sources = SRCS("main.c"),
    .inputs  = INPUTS("include/api.h", "include/config.h"),
    .output  = "myapp",
);
```

### `NOB_RULE`

Like `NOB_ARTIFACT` but you supply the build function. Still checks mtime via `.sources`, `.inputs` and `.output`.

```c
bool compile_shaders(NobexTarget *t, NobexContext *ctx)
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
    return nobex_cleanup("build/*.o", "build/myapp");
}

NOB_PHONY(clean, .description = "remove build artifacts");

bool run(NobexContext *ctx)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "./myapp", "--port", "8080");
    return nob_cmd_run(&cmd);
}

NOB_PHONY(run, .deps = DEPS("myapp"), .description = "start the server on port 8080");
```

`.deps` listed on a phony are built before the function is called.

---

## Helper macros

```c
LIST(...)    // NULL-terminated array — base macro for all the aliases below
SRCS(...)    // source paths
DEPS(...)    // target names
FLAGS(...)   // compiler/linker flags
PKGS(...)    // pkg-config package names
GROUPS(...)  // group names
INPUTS(...)  // file dependencies (headers, generated files) checked for mtime but not compiled
VARS(...)    // variable names consumed by this target, shown in --help
```

---

## Groups

Targets belong to one or more groups. Pass `@group` to build only that subset, or a bare name to build a specific target directly.

```c
#define NOBEX_DEFAULT_GROUP "build"

NOB_ARTIFACT(docs,
    .sources = SRCS("src/main.c"),
    .output  = "docs/index.html",
    .groups  = GROUPS("doc"),
    .run     = generate_docs,
);

NOB_ARTIFACT(myapp,
    .sources = SRCS("main.c"),
    .output  = "myapp",
    .groups  = GROUPS("build", "ci"),
);
```

```sh
./build                # runs the default group
./build @doc           # runs only the "doc" group
./build @ci            # runs only the "ci" group
./build @build @doc    # runs both groups in order
./build myapp          # builds only the "myapp" target
./build clean @build   # runs "clean" target, then the "build" group
```

Targets without `.groups` automatically inherit the default group. If `NOBEX_DEFAULT_GROUP` is not defined, the fallback is `"build"`.

---

## Store and `$(key)` expansion

The store is a key-value map attached to the build context. Values can come from two sources: a `NOB_PHONY` that calls `nobex_set`, or variables passed directly on the CLI as `key=value`.

### Variables from CLI

Pass variables as `key=value` arguments anywhere in the command line:

```sh
./build install_dir=~/workspace/tools install
```

nobex expands `~` to the home directory automatically. The variable is injected into the store before any target runs, so phonies can read it with `nobex_get` and artifacts can reference it via `$(key)` in `.cflags` or `.lflags`.

Targets declare which variables they consume with `.vars`. nobex warns about variables passed on the CLI that no scheduled target declares:

```c
bool install(NobexContext *ctx)
{
    const char *dest = nobex_get(ctx, "install_dir");
    if (!dest) dest = "/usr/local/bin";
    return nob_copy_file("build/myapp", nob_temp_sprintf("%s/myapp", dest));
}

NOB_PHONY(install,
    .deps        = DEPS("myapp"),
    .description = "install myapp",
    .vars        = VARS("install_dir"),
);
```

```sh
./build install_dir=~/workspace/tools install   # installs to ~/workspace/tools
./build install                                  # installs to /usr/local/bin (default)
./build foo=bar install                          # WARNING: 'foo' is not used by any scheduled target
```

Variables appear in `--help` under their declaring target:

```
Targets:
    install              Phony    install myapp
      vars:  install_dir=<value>
```

### Variables from phonies

A phony can also populate the store programmatically. Any artifact that lists the phony in `.deps` can consume those values via `$(key)` expansion in `.cflags` or `.lflags`:

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

## Pipeline — custom compile and link steps

By default nobex uses `nob_cc()` for both compile and link. A `NobexPipeline` lets you replace either step per-target — useful for mixed-language projects or non-standard toolchains.

```c
typedef struct {
    void (*compile)(NobexCompileCtx *pctx);
    void (*link)(NobexLinkCtx *pctx);
} NobexPipeline;
```

The handler only populates `pctx->cmd` — nobex runs, prints, or discards the command depending on flags like `--dry-run`.

```c
void my_compile(NobexCompileCtx *pctx)
{
    nob_cmd_append(pctx->cmd, "g++");
    if (pctx->cflags)
        for (size_t i = 0; pctx->cflags[i]; i++)
            nob_cmd_append(pctx->cmd, pctx->cflags[i]);
    nob_cmd_append(pctx->cmd, "-c", pctx->src, "-o", pctx->obj);
}

NOB_ARTIFACT(engine,
    .sources  = SRCS("src/engine.cpp"),
    .output   = "build/engine",
    .pipeline = { .compile = my_compile },  /* link step stays default */
);
```

Built-in pipelines ready to use:

| Pipeline | Compiler |
|---|---|
| `nobex_default_pipeline` | `nob_cc()` (follows nob.h compiler setting) |
| `nobex_pipeline_gcc` | `gcc` |
| `nobex_pipeline_clang` | `clang` |
| `nobex_pipeline_gpp` | `g++` |

```c
NOB_ARTIFACT(imgui,
    .sources  = SRCS("vendor/imgui/imgui.cpp", "vendor/imgui/imgui_draw.cpp"),
    .output   = "build/imgui.a",
    .type     = TARGET_STATIC_LIB,
    .pipeline = nobex_pipeline_gpp,
);
```

---

## `nobex_cleanup` — glob-based file removal

Deletes files matching one or more glob patterns. Useful in `clean` phonies.

```c
bool clean(NobexContext *ctx)
{
    return nobex_cleanup("build/*.o", "build/*.a", "build/myapp");
}

NOB_PHONY(clean, .groups = GROUPS("clean"));
```

The macro automatically appends the required `NULL` sentinel — no need to add it manually.

On POSIX, uses `glob(3)`. On Windows, uses `FindFirstFile`/`FindNextFile`.

---

## Lifecycle hooks

Targets support three hooks. Each receives the `NobexTarget` and the `NobexContext`.

```c
void on_before(NobexTarget *t, NobexContext *ctx)
{
    // inject extra flags from the store, set up directories, etc.
}

void on_after(NobexTarget *t, NobexContext *ctx)
{
    // deploy, sign, copy, send a notification
}

void on_error(NobexTarget *t, NobexContext *ctx)
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

---

## Watch mode

```sh
./build --watch         # watches the default group
./build --watch @doc    # watches only the "doc" group
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
| `target` | Build a named target directly, in the order given |
| `@group` | Build all targets in a group, in graph order |
| `key=value` | Set a variable consumed by a target; `~` is expanded to home directory |
| `-j<N>` | Run up to N targets in parallel |
| `--watch` | Watch mode on the specified group(s) |
| `-B` | Force rebuild of all targets, ignoring mtimes |
| `--dry-run` | Print commands without executing |
| `--verbose`, `-v` | Print each command before executing |
| `--list`, `-l` | Print all targets, their groups, and descriptions, then exit |
| `--help`, `-h` | Print help; if targets/groups are also given, shows detail only for those |
| `--version`, `-V` | Print when the nobex CLI binary was compiled, then exit |

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
3. Compiles it to `.nobex/<filename>` and manages recompilation if necessary
4. Passes all remaining arguments to the build script

```sh
nobex                   # auto-discovers build.c, compiles if needed, runs it
nobex @all              # runs the "all" group
nobex run build.c       # explicit source file
nobex --watch @doc
nobex -j4 @ci
nobex --list
nobex --version         # shows when the nobex CLI binary itself was compiled
nobex install --help    # shows help and details only for the "install" target
nobex @ci --help        # shows help and details only for targets in the "ci" group
```

### Building the CLI with nobex itself

Once bootstrapped, `build.c` can declare nobex as a target so it rebuilds when `nobex.h` changes:

```c
NOB_ARTIFACT(nobex,
    .sources = SRCS("nobex.h"),
    .inputs  = INPUTS("nob.h"),
    .output  = "build/nobex",
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

#define BUILD_DIR "build"

/* ── hooks ── */

void after_deploy(NobexTarget *t, NobexContext *ctx)
{
    (void)t; (void)ctx;
    nob_log(NOB_INFO, "deployed to /usr/local/bin");
}

/* ── rules ── */

bool gen_version(NobexTarget *t, NobexContext *ctx)
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
    return nobex_cleanup(BUILD_DIR "/*.o", BUILD_DIR "/*.a", BUILD_DIR "/myapp");
}

bool run_tests(NobexContext *ctx)
{
    if (!nobex_run(ctx, "tests")) return false;
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, BUILD_DIR "/tests");
    return nob_cmd_run(&cmd);
}

bool install(NobexContext *ctx)
{
    return nob_copy_file(nobex_output(ctx, "myapp"), "/usr/local/bin/myapp");
}

/* ── targets ── */

NOB_RULE(version_h,
    .sources = SRCS("build.c"),
    .output  = BUILD_DIR "/version.h",
    .run     = gen_version,
);

NOB_ARTIFACT(core,
    .sources     = SRCS("src/core.c", "src/util.c"),
    .inputs      = INPUTS("src/core.h"),
    .output      = BUILD_DIR "/libcore.a",
    .type        = TARGET_STATIC_LIB,
    .cflags      = FLAGS("-Isrc"),
    .description = "core static library",
);

NOB_ARTIFACT(myapp,
    .sources        = SRCS("src/main.c"),
    .inputs         = INPUTS("src/core.h", BUILD_DIR "/version.h"),
    .output         = BUILD_DIR "/myapp",
    .type           = TARGET_EXECUTABLE,
    .deps           = DEPS("core", "version_h"),
    .cflags         = FLAGS("-Isrc", "-I" BUILD_DIR),
    .lflags         = FLAGS("-L" BUILD_DIR, "-lcore"),
    .groups         = GROUPS("build", "ci"),
    .description    = "main application binary",
    .on_after_build = after_deploy,
);

NOB_ARTIFACT(tests,
    .sources     = SRCS("tests/test_core.c"),
    .output      = BUILD_DIR "/tests",
    .type        = TARGET_EXECUTABLE,
    .deps        = DEPS("core"),
    .cflags      = FLAGS("-Isrc"),
    .lflags      = FLAGS("-L" BUILD_DIR, "-lcore"),
    .groups      = GROUPS("test"),
    .description = "core unit tests",
);

NOB_PHONY(clean,     .groups = GROUPS("clean"),       .description = "remove build artifacts");
NOB_PHONY(install,   .deps = DEPS("myapp"),           .description = "install myapp to /usr/local/bin");
NOB_PHONY(run_tests, .groups = GROUPS("test"),        .description = "build and run tests");
```

```sh
cc build.c -o build -lpthread

./build               # compiles core + myapp, calls after_deploy
./build @test         # compiles tests, runs test binary
./build clean         # removes build artifacts
./build install       # installs myapp
./build --watch
./build -j2 @ci
./build --dry-run @ci
./build --list
./build --version
```
