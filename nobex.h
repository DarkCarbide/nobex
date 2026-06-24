/* nobex.h — declarative dependency graph extension for nob.h
 *
 * Three modes controlled by defines (mutually exclusive):
 *   (none)               — types and macros only
 *   NOBEX_IMPLEMENTATION — emits project main()
 *   NOBEX_CLI            — emits standalone CLI main() (builds the `nobex` binary)
 *
 * Usage as project (nob.c):
 *   #define NOB_IMPLEMENTATION
 *   #include "nob.h"
 *   #define NOBEX_IMPLEMENTATION
 *   #include "nobex.h"
 *
 * Usage as standalone CLI:
 *   cc -DNOBEX_CLI nobex.h -o nobex
 *
 * On Linux/macOS link with: -lpthread
 */

#ifndef NOBEX_H_
#define NOBEX_H_

#if defined(NOBEX_IMPLEMENTATION) && defined(NOBEX_CLI)
#  error "NOBEX_IMPLEMENTATION and NOBEX_CLI are mutually exclusive"
#endif

/* In CLI mode the header is compiled directly without nob.h upfront,
 * so we include it here. In normal mode the user includes nob.h first;
 * the NOB_H_ guard prevents double inclusion. */
#ifdef NOBEX_CLI
#  define NOB_IMPLEMENTATION
#  include "nob.h"
#else
#  ifndef NOB_H_
#    error "nobex.h requires nob.h to be included first (or compile with -DNOBEX_CLI)"
#  endif
#endif

/* =========================================================
 * B — User-configurable defines
 * ========================================================= */

#ifndef NOBEX_SECTION_HASH
#  define NOBEX_SECTION_HASH "a3f8c2d901"
#endif

#ifndef NOBEX_DEFAULT_DEBOUNCE_MS
#  define NOBEX_DEFAULT_DEBOUNCE_MS 100
#endif

#ifndef NOBEX_DEFAULT_POLL_MS
#  define NOBEX_DEFAULT_POLL_MS 50
#endif

#ifndef NOBEX_CACHE_DIR
#  define NOBEX_CACHE_DIR ".nobex"
#endif

/* Archiver — used for TARGET_STATIC_LIB.
 * Override before including nobex.h if needed:
 *   #define NOBEX_AR "llvm-ar"
 * The C compiler is intentionally not redefined here; nobex uses the
 * nob_cc() / nob_cc_output() macros from nob.h so the user's compiler
 * choice (set by redefining nob_cc before including nob.h) is honoured
 * automatically.
 */
#ifndef NOBEX_AR
#  ifdef _MSC_VER
#    define NOBEX_AR "lib.exe"
#  else
#    define NOBEX_AR "ar"
#  endif
#endif

/* =========================================================
 * C — Additional includes
 * ========================================================= */

#ifndef _WIN32
#  include <pthread.h>
#  include <glob.h>
#  include <unistd.h>
#  include <pwd.h>
#endif

/* =========================================================
 * D — Linker sections (target auto-discovery)
 *
 * ELF  : prefix "nbx_" ensures the section name is a valid C
 *         identifier, required for __start_/__stop_ synthesis.
 *         We use __asm to name the boundary symbols.
 * Mach-O: __DATA,<hash> section with section$start/section$end.
 * PE   : nob_tg$a/$m/$z (8-char PE limit); sentinel variables
 *         are emitted once inside the implementation block.
 * ========================================================= */

#if defined(__GNUC__) || defined(__clang__)
#  define NOBEX__UNUSED __attribute__((__unused__))
#else
#  define NOBEX__UNUSED
#endif

typedef struct NobexTarget NobexTarget;

#if defined(__APPLE__)
#  define NOBEX__SECNAME "__DATA," NOBEX_SECTION_HASH
#  define NOBEX_SECTION  __attribute__((used, section(NOBEX__SECNAME)))
   extern NobexrTarget *__nobex_sec_start __asm("section$start$__DATA$" NOBEX_SECTION_HASH);
   extern NobexTarget *__nobex_sec_stop  __asm("section$end$__DATA$"   NOBEX_SECTION_HASH);
#  define NOBEX_TARGETS_BEGIN (&__nobex_sec_start)
#  define NOBEX_TARGETS_END   (&__nobex_sec_stop)

#elif defined(_WIN32)
#  define NOBEX_SECTION __declspec(allocate("nob_tg$m"))
   /* _nobex_win_begin and _nobex_win_end emitted in the implementation block */
   extern NobexTarget *_nobex_win_begin;
   extern NobexTarget *_nobex_win_end;
#  define NOBEX_TARGETS_BEGIN (&_nobex_win_begin + 1)
#  define NOBEX_TARGETS_END   (&_nobex_win_end)

#else /* ELF (Linux, FreeBSD, …) */
#  define NOBEX__ELF_SEC "nbx_" NOBEX_SECTION_HASH
#  define NOBEX_SECTION  __attribute__((used, section(NOBEX__ELF_SEC)))
   extern NobexTarget *__nobex_sec_start __asm("__start_nbx_" NOBEX_SECTION_HASH);
   extern NobexTarget *__nobex_sec_stop  __asm("__stop_nbx_"  NOBEX_SECTION_HASH);
#  define NOBEX_TARGETS_BEGIN (&__nobex_sec_start)
#  define NOBEX_TARGETS_END   (&__nobex_sec_stop)
#endif

/* =========================================================
 * E — Public types
 * ========================================================= */

/* Forward declarations for cross-references */
typedef struct NobexContext    NobexContext;
typedef struct NobexStore      NobexStore;
typedef struct NobexWatch      NobexWatch;
typedef struct NobexGraph      NobexGraph;
typedef struct NobexCompileCtx NobexCompileCtx;
typedef struct NobexLinkCtx    NobexLinkCtx;
typedef struct NobexPipeline   NobexPipeline;

struct NobexCompileCtx {
    NobexTarget  *target;
    NobexContext *ctx;
    const char   *src;
    const char   *obj;
    const char  **cflags;  /* already expanded */
    Nob_Cmd      *cmd;     /* handler populates; nobex runs/prints/frees */
};

struct NobexLinkCtx {
    NobexTarget  *target;
    NobexContext *ctx;
    const char  **objs;
    const char  **lflags;  /* already expanded */
    Nob_Cmd      *cmd;     /* handler populates; nobex runs/prints/frees */
};

struct NobexPipeline {
    void (*compile)(NobexCompileCtx *pctx);
    void (*link)(NobexLinkCtx *pctx);
};

typedef enum {
    TARGET_EXECUTABLE,
    TARGET_STATIC_LIB,
    TARGET_SHARED_LIB,
    TARGET_RULE,
    TARGET_PHONY,
} TargetType;

struct NobexStore {
    const char **keys;
    const char **values;
    size_t       count;
    size_t       capacity;
};

struct NobexWatch {
    bool     skip;
    bool     skip_hooks;
    uint32_t debounce_ms;
    uint32_t poll_ms;
    void   (*on_change)(NobexTarget*, NobexContext*);
    void   (*on_built)(NobexTarget*,  NobexContext*);
    void   (*on_error)(NobexTarget*,  NobexContext*);
};

/* Target — full definition (forward-declared in section D) */
struct NobexTarget {
    const char   *name;
    const char   *description;
    TargetType    type;
    const char  **sources;
    const char  **inputs;
    const char   *output;
    const char  **deps;
    const char  **cflags;
    const char  **lflags;
    const char  **packages;
    const char  **groups;
    bool          use_xflags;
    NobexPipeline pipeline;
    NobexWatch    watch;
    const char  **vars;   /* variable names consumed by this target, shown in --help */
    bool        (*run)(NobexTarget*, NobexContext*);
    bool        (*phony)(NobexContext*);
    void        (*on_before_build)(NobexTarget*, NobexContext*);
    void        (*on_after_build)(NobexTarget*,  NobexContext*);
    void        (*on_error)(NobexTarget*,        NobexContext*);
};

struct NobexGraph {
    NobexTarget **items;
    size_t   count;
    size_t   capacity;
};

typedef struct { NobexTarget **items; size_t count; size_t capacity; } _NobexDoneSet;

struct NobexContext {
    NobexGraph         *graph;
    NobexStore    *store;
    int            jobs;
    bool           dry_run;
    bool           verbose;
    bool           force;
    /* targets completed this run — prevents double-build when a phony
     * calls nobex_run() on a target already built by the main graph */
    _NobexDoneSet  done;
};

/* =========================================================
 * F — Target declaration macros
 * ========================================================= */

/* #define NOBEX_DEFAULT_GROUP "groupname" before including this header
 * to set the default group. If omitted, falls back to "build". */

/* NOB_PHONY requires the function bool FN(NobexContext*) to be declared
 * before the macro — the compiler needs to see the signature.
 * The parameter is named FN (not `name`) to avoid the macro argument
 * being substituted inside the `.name` struct field designator. */
#define NOB_ARTIFACT(FN, ...)                                                \
    static NobexTarget _nobex_target_##FN = { .name = #FN, __VA_ARGS__ };        \
    static NobexTarget *_nobex_ptr_##FN NOBEX_SECTION = &_nobex_target_##FN

#define NOB_RULE(FN, ...)                                                    \
    static NobexTarget _nobex_target_##FN = {                                     \
        .name = #FN, .type = TARGET_RULE, __VA_ARGS__                       \
    };                                                                       \
    static NobexTarget *_nobex_ptr_##FN NOBEX_SECTION = &_nobex_target_##FN

#define NOB_PHONY(FN, ...)                                                   \
    static NobexTarget _nobex_target_##FN = {                                     \
        .name = #FN, .type = TARGET_PHONY,                                  \
        .phony = FN, __VA_ARGS__                                             \
    };                                                                       \
    static NobexTarget *_nobex_ptr_##FN NOBEX_SECTION = &_nobex_target_##FN

/* =========================================================
 * G — Helper macros
 * ========================================================= */

#define LIST(...)   ((const char*[]){__VA_ARGS__, NULL})
#define SRCS(...)   LIST(__VA_ARGS__)
#define DEPS(...)   LIST(__VA_ARGS__)
#define FLAGS(...)  LIST(__VA_ARGS__)
#define PKGS(...)   LIST(__VA_ARGS__)
#define GROUPS(...) LIST(__VA_ARGS__)
#define INPUTS(...) LIST(__VA_ARGS__)
#define VARS(...)   LIST(__VA_ARGS__)

/* =========================================================
 * H — Public API declarations
 * ========================================================= */

/* Store */
void        nobex_set(NobexContext *ctx, const char *key, const char *value);
const char *nobex_get(NobexContext *ctx, const char *key);

/* Graph helpers (usable inside hooks and phonies) */
NobexTarget     *nobex_find(NobexContext *ctx, const char *name);
bool        nobex_run(NobexContext *ctx, const char *name);
const char *nobex_output(NobexContext *ctx, const char *name);
bool        nobex_cleanup_impl(const char *first, ...);
#define     nobex_cleanup(first, ...) nobex_cleanup_impl(first, ##__VA_ARGS__, NULL)

/* xflags API — reads //> key : value lines from the top of C source files */
const char *nobex_xflags_get(const char *filepath, const char *key);
void        nobex_xflags_each(const char *filepath,
                               void (*cb)(const char *key, const char *value, void *ud),
                               void *ud);

/* Default group — written by NOBEX_DEFAULT_GROUP constructor; falls back to "build" if NULL */
extern const char *_nobex_default_group;

#endif /* NOBEX_H_ */

/* =========================================================
 * IMPLEMENTATION
 * Guarded by NOBEX_IMPLEMENTATION or NOBEX_CLI
 * ========================================================= */

#if defined(NOBEX_IMPLEMENTATION) || defined(NOBEX_CLI)

/* ── Platform sentinels ── */

#ifdef _WIN32
__declspec(allocate("nob_tg$a")) NobexTarget *_nobex_win_begin = NULL;
__declspec(allocate("nob_tg$z")) NobexTarget *_nobex_win_end   = NULL;
#elif !defined(__APPLE__)
/* ELF: a dummy entry ensures the section always exists so the linker
 * synthesizes __start_nbx_<hash> and __stop_nbx_<hash> even when no
 * targets are declared (e.g. NOBEX_CLI mode). */
static NobexTarget * NOBEX__UNUSED _nobex_section_dummy NOBEX_SECTION = NULL;
#endif

#ifdef NOBEX_DEFAULT_GROUP
const char *_nobex_default_group = NOBEX_DEFAULT_GROUP;
#else
const char *_nobex_default_group = NULL;
#endif

/* ── Store ── */

void nobex_set(NobexContext *ctx, const char *key, const char *value)
{
    NobexStore *s = ctx->store;
    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->keys[i], key) == 0) { s->values[i] = value; return; }
    }
    if (s->count >= s->capacity) {
        size_t cap = s->capacity == 0 ? 8 : s->capacity * 2;
        s->keys   = (const char**)NOB_REALLOC(s->keys,   cap * sizeof(char*));
        s->values = (const char**)NOB_REALLOC(s->values, cap * sizeof(char*));
        NOB_ASSERT(s->keys && s->values);
        s->capacity = cap;
    }
    s->keys[s->count]   = key;
    s->values[s->count] = value;
    s->count++;
}

const char *nobex_get(NobexContext *ctx, const char *key)
{
    NobexStore *s = ctx->store;
    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->keys[i], key) == 0) return s->values[i];
    }
    return NULL;
}

/* ── Path expansion ── */

static NOBEX__UNUSED const char *_nobex_expand_path(const char *path)
{
    if (!path || path[0] != '~') return path;
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home) home = getenv("HOMEDRIVE");  /* best-effort fallback */
#else
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
#endif
    if (!home) return path;
    if (path[1] == '\0') return home;
    if (path[1] == '/' || path[1] == '\\')
        return nob_temp_sprintf("%s%s", home, path + 1);
    return path;
}

/* ── xflags parser ──
 *
 * Format: //> key : value
 * Only processes contiguous lines at the top of the file (stops at the
 * first line that does not start with //>). Identical to xflags.c logic.
 */

void nobex_xflags_each(const char *filepath,
                        void (*cb)(const char *key, const char *value, void *ud),
                        void *ud)
{
    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(filepath, &sb)) return;

    Nob_String_View source = nob_sb_to_sv(sb);

    while (source.count > 0) {
        Nob_String_View line = nob_sv_chop_by_delim(&source, '\n');
        line = nob_sv_trim(line);

        if (!nob_sv_starts_with(line, nob_sv_from_cstr("//>"))) break;

        line.data  += 3;
        line.count -= 3;
        line = nob_sv_trim(line);

        Nob_String_View key   = nob_sv_chop_by_delim(&line, ':');
        Nob_String_View value = line;
        key   = nob_sv_trim(key);
        value = nob_sv_trim(value);

        if (key.count == 0 || value.count == 0) break;

        char *k = nob_temp_strndup(key.data,   key.count);
        char *v = nob_temp_strndup(value.data, value.count);
        cb(k, v, ud);
    }

    NOB_FREE(sb.items);
}

typedef struct { const char *wanted; const char *result; } _NobexXflagsGet;

static NOBEX__UNUSED void _nobex_xflags_get_cb(const char *key, const char *value, void *ud)
{
    _NobexXflagsGet *g = (_NobexXflagsGet*)ud;
    if (g->result == NULL && strcmp(key, g->wanted) == 0) g->result = value;
}

const char *nobex_xflags_get(const char *filepath, const char *key)
{
    _NobexXflagsGet g = { .wanted = key, .result = NULL };
    nobex_xflags_each(filepath, _nobex_xflags_get_cb, &g);
    return g.result;
}

/* ── Graph: collect, find, validate, resolve_groups ── */

static NOBEX__UNUSED void _nobex_collect_targets(NobexGraph *g)
{
#if defined(__APPLE__) || !defined(_WIN32)
    /* On ELF/Mach-O the boundary symbols may not exist if no targets were
     * declared (empty section). Guard with a weak-reference trick: if
     * NOBEX_TARGETS_BEGIN == NOBEX_TARGETS_END the section is empty. */
    NobexTarget **begin = NOBEX_TARGETS_BEGIN;
    NobexTarget **end   = NOBEX_TARGETS_END;
    for (NobexTarget **p = begin; p < end; p++) {
        if (*p) nob_da_append(g, *p);
    }
#else
    for (Target **p = NOBEX_TARGETS_BEGIN; p < NOBEX_TARGETS_END; p++) {
        if (*p) nob_da_append(g, *p);
    }
#endif
}

static NOBEX__UNUSED NobexTarget *_nobex_graph_find(NobexGraph *g, const char *name)
{
    for (size_t i = 0; i < g->count; i++) {
        if (strcmp(g->items[i]->name, name) == 0) return g->items[i];
    }
    return NULL;
}

NobexTarget *nobex_find(NobexContext *ctx, const char *name)
{
    return _nobex_graph_find(ctx->graph, name);
}

const char *nobex_output(NobexContext *ctx, const char *name)
{
    NobexTarget *t = _nobex_graph_find(ctx->graph, name);
    return t ? t->output : NULL;
}

bool nobex_cleanup_impl(const char *first, ...)
{
    bool ok = true;
    va_list ap;
    va_start(ap, first);
    const char *pattern = first;
    while (pattern != NULL) {
#ifdef _WIN32
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        /* extract directory prefix from pattern */
        const char *slash = strrchr(pattern, '/');
        const char *bslash = strrchr(pattern, '\\');
        const char *sep = slash > bslash ? slash : bslash;
        char dir[512] = {0};
        if (sep) { size_t dlen = (size_t)(sep - pattern) + 1; if (dlen < sizeof(dir)) { memcpy(dir, pattern, dlen); dir[dlen] = '\0'; } }
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            char path[512];
            snprintf(path, sizeof(path), "%s%s", dir, fd.cFileName);
            if (!nob_delete_file(path)) ok = false;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
#else
        glob_t gs = {0};
        int r = glob(pattern, GLOB_NOSORT, NULL, &gs);
        if (r == GLOB_NOMATCH) { globfree(&gs); continue; }
        if (r != 0)            { globfree(&gs); ok = false; continue; }
        for (size_t i = 0; i < gs.gl_pathc; i++)
            if (!nob_delete_file(gs.gl_pathv[i])) ok = false;
        globfree(&gs);
#endif
        pattern = va_arg(ap, const char *);
    }
    va_end(ap);
    return ok;
}

typedef enum { NOBEX_UNVISITED = 0, NOBEX_IN_PROGRESS, NOBEX_DONE } _NobexVisit;

static NOBEX__UNUSED void _nobex_validate_dfs(NobexGraph *g, size_t idx, _NobexVisit *state, bool *ok)
{
    if (state[idx] == NOBEX_DONE)        return;
    if (state[idx] == NOBEX_IN_PROGRESS) {
        nob_log(NOB_ERROR, "nobex: cycle detected involving target '%s'", g->items[idx]->name);
        *ok = false;
        return;
    }

    state[idx] = NOBEX_IN_PROGRESS;
    NobexTarget *t = g->items[idx];

    if (t->deps) {
        for (size_t d = 0; t->deps[d]; d++) {
            NobexTarget *dep = _nobex_graph_find(g, t->deps[d]);
            if (!dep) {
                nob_log(NOB_ERROR, "nobex: target '%s' depends on '%s' which does not exist",
                        t->name, t->deps[d]);
                *ok = false;
                continue;
            }
            for (size_t j = 0; j < g->count; j++) {
                if (g->items[j] == dep) { _nobex_validate_dfs(g, j, state, ok); break; }
            }
        }
    }

    state[idx] = NOBEX_DONE;
}

static NOBEX__UNUSED bool _nobex_validate_graph(NobexGraph *g)
{
    _NobexVisit *state = (_NobexVisit*)NOB_REALLOC(NULL, g->count * sizeof(_NobexVisit));
    NOB_ASSERT(state);
    memset(state, 0, g->count * sizeof(_NobexVisit));

    bool ok = true;
    for (size_t i = 0; i < g->count; i++) {
        if (state[i] == NOBEX_UNVISITED) _nobex_validate_dfs(g, i, state, &ok);
    }

    NOB_FREE(state);
    return ok;
}

static NOBEX__UNUSED bool _nobex_target_in_group(NobexTarget *t, const char *group)
{
    if (t->groups == NULL) {
        const char *dg = _nobex_default_group ? _nobex_default_group : "build";
        return strcmp(group, dg) == 0;
    }
    for (size_t i = 0; t->groups[i]; i++) {
        if (strcmp(t->groups[i], group) == 0) return true;
    }
    return false;
}

static NOBEX__UNUSED bool _nobex_resolve_groups(NobexGraph *g, const char **groups, size_t ngroups, NobexGraph *out)
{
    for (size_t i = 0; i < g->count; i++) {
        for (size_t j = 0; j < ngroups; j++) {
            if (_nobex_target_in_group(g->items[i], groups[j])) {
                nob_da_append(out, g->items[i]);
                break;
            }
        }
    }
    if (out->count == 0) {
        nob_log(NOB_ERROR, "nobex: no targets found for the specified groups");
        return false;
    }
    return true;
}

/* ── $(key) store expansion ── */

static NOBEX__UNUSED const char *_nobex_expand(const char *s, NobexContext *ctx)
{
    if (!strchr(s, '$')) return s;

    Nob_String_Builder sb = {0};
    const char *p = s;

    while (*p) {
        if (p[0] == '$' && p[1] == '(') {
            const char *end = strchr(p + 2, ')');
            if (!end) { nob_da_append(&sb, *p++); continue; }

            size_t keylen = (size_t)(end - (p + 2));
            char *key = nob_temp_strndup(p + 2, keylen);
            const char *val = nobex_get(ctx, key);
            if (val) {
                nob_sb_append_cstr(&sb, val);
            } else {
                if (ctx->verbose)
                    nob_log(NOB_WARNING, "nobex: store key '$(%s)' not found", key);
                nob_da_append_many(&sb, p, (size_t)(end - p) + 1);
            }
            p = end + 1;
        } else {
            nob_da_append(&sb, *p++);
        }
    }
    nob_sb_append_null(&sb);
    return sb.items;
}

/* ── pkg-config integration ── */

static NOBEX__UNUSED bool _nobex_pkg_config(const char *pkg, bool want_cflags, Nob_String_Builder *out)
{
    
    Nob_Log_Level saved = nob_minimal_log_level;
    nob_minimal_log_level = NOB_WARNING;
    nob_mkdir_if_not_exists(NOBEX_CACHE_DIR);
    nob_minimal_log_level = saved;

    const char *tmp = nob_temp_sprintf("%s/pkg_%s.tmp", NOBEX_CACHE_DIR, pkg);
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "pkg-config", want_cflags ? "--cflags" : "--libs", pkg);

    if (!nob_cmd_run(&cmd, .stdout_path = tmp)) {
        nob_log(NOB_ERROR, "nobex: pkg-config failed for '%s'", pkg);
        return false;
    }

    Nob_String_Builder file = {0};
    if (!nob_read_entire_file(tmp, &file)) return false;
    while (file.count > 0 &&
           (file.items[file.count-1] == '\n' || file.items[file.count-1] == '\r'))
        file.count--;
    nob_sb_append_buf(out, file.items, file.count);
    NOB_FREE(file.items);
    nob_delete_file(tmp);
    return true;
}

/* ── Build engine ── */

static NOBEX__UNUSED bool _nobex_target_build(NobexTarget *t, NobexContext *ctx);  /* forward */

static NOBEX__UNUSED bool _nobex_needs_rebuild(NobexTarget *t, NobexContext *ctx)
{
    if (ctx->force) return true;
    if (!t->output) return true;
    if (!t->sources && !t->inputs) return true;

    /* collect sources + inputs into a single array for the mtime check */
    size_t nsrc = 0, ninp = 0;
    if (t->sources) while (t->sources[nsrc]) nsrc++;
    if (t->inputs)  while (t->inputs[ninp])  ninp++;

    const char **all = (const char **)NOB_REALLOC(NULL, (nsrc + ninp) * sizeof(char *));
    for (size_t i = 0; i < nsrc; i++) all[i]        = t->sources[i];
    for (size_t i = 0; i < ninp; i++) all[nsrc + i] = t->inputs[i];

    int r = nob_needs_rebuild(t->output, all, nsrc + ninp);
    NOB_FREE(all);
    return r < 0 ? true : (bool)r;
}

/* ── Built-in pipeline implementations ── */

static NOBEX__UNUSED void _nobex_default_compile(NobexCompileCtx *pctx)
{
    nob_cc(pctx->cmd);
    if (pctx->cflags)
        for (size_t i = 0; pctx->cflags[i]; i++) nob_cmd_append(pctx->cmd, pctx->cflags[i]);
    nob_cmd_append(pctx->cmd, "-c", pctx->src);
    nob_cc_output(pctx->cmd, pctx->obj);
}

static NOBEX__UNUSED void _nobex_default_link(NobexLinkCtx *pctx)
{
    nob_cc(pctx->cmd);
    if (pctx->objs)
        for (size_t i = 0; pctx->objs[i]; i++) nob_cmd_append(pctx->cmd, pctx->objs[i]);
    nob_cc_output(pctx->cmd, pctx->target->output ? pctx->target->output : "a.out");
    if (pctx->lflags)
        for (size_t i = 0; pctx->lflags[i]; i++) nob_cmd_append(pctx->cmd, pctx->lflags[i]);
}

#define _NOBEX_MAKE_PIPELINE(cc_bin)                                              \
static NOBEX__UNUSED void _nobex_compile_##cc_bin(NobexCompileCtx *pctx)         \
{                                                                                  \
    nob_cmd_append(pctx->cmd, #cc_bin);                                           \
    if (pctx->cflags)                                                             \
        for (size_t i = 0; pctx->cflags[i]; i++) nob_cmd_append(pctx->cmd, pctx->cflags[i]); \
    nob_cmd_append(pctx->cmd, "-c", pctx->src, "-o", pctx->obj);                 \
}                                                                                  \
static NOBEX__UNUSED void _nobex_link_##cc_bin(NobexLinkCtx *pctx)               \
{                                                                                  \
    nob_cmd_append(pctx->cmd, #cc_bin);                                           \
    if (pctx->objs)                                                               \
        for (size_t i = 0; pctx->objs[i]; i++) nob_cmd_append(pctx->cmd, pctx->objs[i]); \
    nob_cmd_append(pctx->cmd, "-o", pctx->target->output ? pctx->target->output : "a.out"); \
    if (pctx->lflags)                                                             \
        for (size_t i = 0; pctx->lflags[i]; i++) nob_cmd_append(pctx->cmd, pctx->lflags[i]); \
}

_NOBEX_MAKE_PIPELINE(gcc)
_NOBEX_MAKE_PIPELINE(clang)

/* g++ pipeline — macro can't stringify "g++" directly */
static NOBEX__UNUSED void _nobex_compile_gpp(NobexCompileCtx *pctx)
{
    nob_cmd_append(pctx->cmd, "g++");
    if (pctx->cflags)
        for (size_t i = 0; pctx->cflags[i]; i++) nob_cmd_append(pctx->cmd, pctx->cflags[i]);
    nob_cmd_append(pctx->cmd, "-c", pctx->src, "-o", pctx->obj);
}

static NOBEX__UNUSED void _nobex_link_gpp(NobexLinkCtx *pctx)
{
    nob_cmd_append(pctx->cmd, "g++");
    if (pctx->objs)
        for (size_t i = 0; pctx->objs[i]; i++) nob_cmd_append(pctx->cmd, pctx->objs[i]);
    nob_cmd_append(pctx->cmd, "-o", pctx->target->output ? pctx->target->output : "a.out");
    if (pctx->lflags)
        for (size_t i = 0; pctx->lflags[i]; i++) nob_cmd_append(pctx->cmd, pctx->lflags[i]);
}

#ifdef NOBEX_IMPLEMENTATION

const NobexPipeline nobex_default_pipeline = { _nobex_default_compile, _nobex_default_link };
const NobexPipeline nobex_pipeline_gcc     = { _nobex_compile_gcc,     _nobex_link_gcc     };
const NobexPipeline nobex_pipeline_clang   = { _nobex_compile_clang,   _nobex_link_clang   };
const NobexPipeline nobex_pipeline_gpp     = { _nobex_compile_gpp,     _nobex_link_gpp     };

#else

extern const NobexPipeline nobex_default_pipeline;
extern const NobexPipeline nobex_pipeline_gcc;
extern const NobexPipeline nobex_pipeline_clang;
extern const NobexPipeline nobex_pipeline_gpp;

#endif

/* ── Parallel compile helper ─────────────────────────────────────────────────
 * Compiles all sources that need rebuilding, up to ctx->jobs at a time,
 * using nob's nob_cmd_run_async + nob_procs_append_with_flush primitives.
 * objs[] must already be filled (nsrc entries + NULL sentinel).
 * exp_cflags[] must already be expanded.
 * --------------------------------------------------------------------------- */
static NOBEX__UNUSED bool _nobex_compile_sources(
        NobexTarget *t, NobexContext *ctx,
        size_t nsrc, const char **objs,
        size_t ncf,  const char **exp_cflags,
        void (*do_compile)(NobexCompileCtx*))
{
    size_t ninp = 0;
    if (t->inputs) while (t->inputs[ninp]) ninp++;

    int    jobs  = ctx->jobs > 1 ? ctx->jobs : 1;
    Nob_Procs procs = {0};
    bool ok = true;

    for (size_t i = 0; i < nsrc && ok; i++) {
        const char *src = t->sources[i];
        const char *obj = objs[i];

        const char **chk = (const char **)NOB_REALLOC(NULL, (1 + ninp) * sizeof(char *));
        chk[0] = src;
        for (size_t k = 0; k < ninp; k++) chk[1 + k] = t->inputs[k];
        int needs = ctx->force ? 1 : nob_needs_rebuild(obj, chk, 1 + ninp);
        NOB_FREE(chk);
        if (needs < 0) { ok = false; break; }
        if (!needs && nob_file_exists(obj)) continue;

        size_t extra = 0;
        const char *xf = t->use_xflags ? nobex_xflags_get(src, "cflags") : NULL;
        if (xf) extra = 1;
        const char **src_cflags = (const char **)NOB_REALLOC(NULL, (ncf + extra + 1) * sizeof(char *));
        for (size_t j = 0; j < ncf; j++) src_cflags[j] = exp_cflags[j];
        if (xf) src_cflags[ncf] = xf;
        src_cflags[ncf + extra] = NULL;

        Nob_Cmd cmd = {0};
        NobexCompileCtx pctx = { t, ctx, src, obj, src_cflags, &cmd };
        do_compile(&pctx);
        NOB_FREE(src_cflags);

        if (ctx->dry_run) {
            Nob_String_Builder sb = {0};
            nob_cmd_render(cmd, &sb); nob_sb_append_null(&sb);
            printf("[dry-run] %s\n", sb.items);
            NOB_FREE(sb.items); NOB_FREE(cmd.items);
        } else {
            Nob_Proc proc = nob_cmd_run_async_and_reset(&cmd);
            if (proc == NOB_INVALID_PROC) { ok = false; break; }
            ok = nob_procs_append_with_flush(&procs, proc, (size_t)jobs);
        }
    }

    /* wait for remaining in-flight compilations */
    if (ok) ok = nob_procs_flush(&procs);
    else         nob_procs_flush(&procs); /* drain to avoid zombies */

    NOB_FREE(procs.items);
    return ok;
}

static NOBEX__UNUSED bool _nobex_build_executable(NobexTarget *t, NobexContext *ctx)
{
    Nob_Log_Level saved = nob_minimal_log_level;
    nob_minimal_log_level = NOB_WARNING;
    nob_mkdir_if_not_exists("build");
    nob_minimal_log_level = saved;

    void (*do_compile)(NobexCompileCtx*) = t->pipeline.compile ? t->pipeline.compile : _nobex_default_compile;
    void (*do_link)(NobexLinkCtx*)       = t->pipeline.link    ? t->pipeline.link    : _nobex_default_link;

    /* build expanded cflags array (cflags + xflags + packages) */
    size_t ncf = 0;
    if (t->cflags) while (t->cflags[ncf]) ncf++;
    const char **exp_cflags = (const char **)NOB_REALLOC(NULL, (ncf + 2) * sizeof(char *));
    for (size_t j = 0; j < ncf; j++) exp_cflags[j] = _nobex_expand(t->cflags[j], ctx);
    exp_cflags[ncf] = NULL;

    /* build expanded lflags array */
    size_t nlf = 0;
    if (t->lflags) while (t->lflags[nlf]) nlf++;
    const char **exp_lflags = (const char **)NOB_REALLOC(NULL, (nlf + 2) * sizeof(char *));
    for (size_t j = 0; j < nlf; j++) exp_lflags[j] = _nobex_expand(t->lflags[j], ctx);
    exp_lflags[nlf] = NULL;

    /* collect obj paths */
    size_t nsrc = 0;
    if (t->sources) while (t->sources[nsrc]) nsrc++;
    const char **objs = (const char **)NOB_REALLOC(NULL, (nsrc + 1) * sizeof(char *));
    for (size_t i = 0; i < nsrc; i++) {
        const char *base = nob_path_name(t->sources[i]);
        objs[i] = nob_temp_sprintf("build/%s.o", base);
    }
    objs[nsrc] = NULL;

    /* compile step */
    if (t->sources) {
        if (!_nobex_compile_sources(t, ctx, nsrc, objs, ncf, exp_cflags, do_compile)) {
            NOB_FREE(exp_cflags); NOB_FREE(exp_lflags); NOB_FREE(objs);
            return false;
        }
    }

    /* append package lflags and xflags lflags */
    size_t total_lf = nlf;
    if (t->packages) {
        for (size_t j = 0; t->packages[j]; j++) {
            Nob_String_Builder pl = {0};
            if (!_nobex_pkg_config(t->packages[j], false, &pl)) {
                NOB_FREE(exp_cflags); NOB_FREE(exp_lflags); NOB_FREE(objs); return false;
            }
            nob_sb_append_null(&pl);
            exp_lflags = (const char **)NOB_REALLOC(exp_lflags, (total_lf + 2) * sizeof(char *));
            exp_lflags[total_lf++] = pl.items;
            exp_lflags[total_lf]   = NULL;
        }
    }
    if (t->use_xflags && t->sources) {
        for (size_t i = 0; t->sources[i]; i++) {
            const char *xl = nobex_xflags_get(t->sources[i], "libs");
            if (xl) {
                exp_lflags = (const char **)NOB_REALLOC(exp_lflags, (total_lf + 2) * sizeof(char *));
                exp_lflags[total_lf++] = xl;
                exp_lflags[total_lf]   = NULL;
            }
        }
    }

    /* link step */
    {
        Nob_Cmd cmd = {0};
        NobexLinkCtx lpctx = { t, ctx, objs, exp_lflags, &cmd };
        do_link(&lpctx);

        bool ok;
        if (ctx->dry_run) {
            Nob_String_Builder sb = {0};
            nob_cmd_render(cmd, &sb); nob_sb_append_null(&sb);
            printf("[dry-run] %s\n", sb.items);
            NOB_FREE(sb.items); ok = true;
        } else {
            ok = nob_cmd_run(&cmd);
        }
        NOB_FREE(cmd.items);
        NOB_FREE(exp_cflags); NOB_FREE(exp_lflags); NOB_FREE(objs);
        return ok;
    }
}

static NOBEX__UNUSED bool _nobex_build_static_lib(NobexTarget *t, NobexContext *ctx)
{
    nob_mkdir_if_not_exists("build");
    const char *output = t->output ? t->output : "build/libout.a";

    void (*do_compile)(NobexCompileCtx*) = t->pipeline.compile ? t->pipeline.compile : _nobex_default_compile;

    /* build expanded cflags */
    size_t ncf = 0;
    if (t->cflags) while (t->cflags[ncf]) ncf++;
    const char **exp_cflags = (const char **)NOB_REALLOC(NULL, (ncf + 1) * sizeof(char *));
    for (size_t j = 0; j < ncf; j++) exp_cflags[j] = _nobex_expand(t->cflags[j], ctx);
    exp_cflags[ncf] = NULL;

    /* collect obj paths */
    size_t nsrc = 0;
    if (t->sources) while (t->sources[nsrc]) nsrc++;
    const char **objs = (const char **)NOB_REALLOC(NULL, (nsrc + 1) * sizeof(char *));
    for (size_t i = 0; i < nsrc; i++) {
        const char *base = nob_path_name(t->sources[i]);
        objs[i] = nob_temp_sprintf("build/%s.o", base);
    }
    objs[nsrc] = NULL;

    /* compile each source individually, in parallel if ctx->jobs > 1 */
    if (!_nobex_compile_sources(t, ctx, nsrc, objs, ncf, exp_cflags, do_compile)) {
        NOB_FREE(exp_cflags); NOB_FREE(objs);
        return false;
    }

    NOB_FREE(exp_cflags);

    if (ctx->dry_run) {
        printf("[dry-run] %s rcs %s ...\n", NOBEX_AR, output);
        NOB_FREE(objs);
        return true;
    }

    /* archive all objects */
    Nob_Cmd ar = {0};
    nob_cmd_append(&ar, NOBEX_AR, "rcs", output);
    for (size_t i = 0; i < nsrc; i++) nob_cmd_append(&ar, objs[i]);
    bool ok = nob_cmd_run(&ar);
    NOB_FREE(ar.items); NOB_FREE(objs);
    return ok;
}

static NOBEX__UNUSED bool _nobex_build_shared_lib(NobexTarget *t, NobexContext *ctx)
{
    nob_mkdir_if_not_exists("build");
    const char *output = t->output ? t->output : "build/libout.so";

    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    nob_cmd_append(&cmd, "-shared");
    if (t->cflags) {
        for (size_t j = 0; t->cflags[j]; j++)
            nob_cmd_append(&cmd, _nobex_expand(t->cflags[j], ctx));
    }
    if (t->sources) {
        for (size_t i = 0; t->sources[i]; i++)
            nob_cmd_append(&cmd, t->sources[i]);
    }
    nob_cmd_append(&cmd, "-o", output);
    if (t->lflags) {
        for (size_t j = 0; t->lflags[j]; j++)
            nob_cmd_append(&cmd, _nobex_expand(t->lflags[j], ctx));
    }

    if (ctx->dry_run) {
        Nob_String_Builder r = {0}; nob_cmd_render(cmd, &r); nob_sb_append_null(&r);
        printf("[dry-run] %s\n", r.items); NOB_FREE(r.items); return true;
    }
    return nob_cmd_run(&cmd);
}

static bool _nobex_done_contains(NobexContext *ctx, NobexTarget *t)
{
    for (size_t i = 0; i < ctx->done.count; i++)
        if (ctx->done.items[i] == t) return true;
    return false;
}

static void _nobex_done_mark(NobexContext *ctx, NobexTarget *t)
{
    nob_da_append(&ctx->done, t);
}

static NOBEX__UNUSED bool _nobex_target_build(NobexTarget *t, NobexContext *ctx)
{
    /* non-phony targets are only built once per run */
    if (t->type != TARGET_PHONY && _nobex_done_contains(ctx, t)) return true;

    /* post-order: run deps first */
    if (t->deps) {
        for (size_t i = 0; t->deps[i]; i++) {
            NobexTarget *dep = _nobex_graph_find(ctx->graph, t->deps[i]);
            if (!dep) {
                nob_log(NOB_ERROR, "nobex: dep '%s' not found", t->deps[i]);
                return false;
            }
            if (!_nobex_target_build(dep, ctx)) return false;
        }
    }

    if (t->on_before_build) t->on_before_build(t, ctx);

    if (t->type == TARGET_PHONY) {
        if (!t->phony) {
            nob_log(NOB_ERROR, "nobex: phony '%s' has no function defined", t->name);
            return false;
        }
        bool ok = t->phony(ctx);
        if (!ok && t->on_error) t->on_error(t, ctx);
        if ( ok && t->on_after_build) t->on_after_build(t, ctx);
        return ok;
    }

    if (!_nobex_needs_rebuild(t, ctx)) {
        nob_log(NOB_INFO, "nobex: '%s' is up to date", t->name);
        if (t->on_after_build) t->on_after_build(t, ctx);
        _nobex_done_mark(ctx, t);
        return true;
    }

    bool ok = false;
    switch (t->type) {
        case TARGET_EXECUTABLE: ok = _nobex_build_executable(t, ctx); break;
        case TARGET_STATIC_LIB: ok = _nobex_build_static_lib(t, ctx); break;
        case TARGET_SHARED_LIB: ok = _nobex_build_shared_lib(t, ctx); break;
        case TARGET_RULE:
            if (!t->run) {
                nob_log(NOB_ERROR, "nobex: rule '%s' has no .run function", t->name);
                ok = false;
            } else {
                ok = t->run(t, ctx);
            }
            break;
        default:
            nob_log(NOB_ERROR, "nobex: unknown type for target '%s'", t->name);
            ok = false;
    }

    if (!ok && t->on_error) t->on_error(t, ctx);
    if ( ok && t->on_after_build) t->on_after_build(t, ctx);
    if (ok) _nobex_done_mark(ctx, t);
    return ok;
}

static NOBEX__UNUSED bool _nobex_graph_run_serial(NobexGraph *g, NobexContext *ctx)
{
    for (size_t i = 0; i < g->count; i++) {
        if (!_nobex_target_build(g->items[i], ctx)) return false;
    }
    return true;
}

/* Parallel build — simple batch model: run up to `jobs` targets at a time.
 * Full DAG-aware scheduling (in-degree queue) is left for a future version. */
#ifdef _WIN32

typedef struct { NobexTarget *target; NobexContext *ctx; bool result; } _NobexWinArg;

static DWORD WINAPI _nobex_thread_win(LPVOID arg)
{
    _NobexWinArg *a = (_NobexWinArg*)arg;
    a->result = _nobex_target_build(a->target, a->ctx);
    return 0;
}

static NOBEX__UNUSED bool _nobex_graph_run_parallel(NobexGraph *g, NobexContext *ctx, int jobs)
{
    size_t i = 0; bool ok = true;
    while (i < g->count && ok) {
        size_t batch = (size_t)jobs < (g->count - i) ? (size_t)jobs : (g->count - i);
        HANDLE *handles = (HANDLE*)NOB_REALLOC(NULL, batch * sizeof(HANDLE));
        _NobexWinArg *args = (_NobexWinArg*)NOB_REALLOC(NULL, batch * sizeof(_NobexWinArg));
        for (size_t j = 0; j < batch; j++) {
            args[j] = (_NobexWinArg){ g->items[i+j], ctx, false };
            handles[j] = CreateThread(NULL, 0, _nobex_thread_win, &args[j], 0, NULL);
        }
        WaitForMultipleObjects((DWORD)batch, handles, TRUE, INFINITE);
        for (size_t j = 0; j < batch; j++) {
            CloseHandle(handles[j]);
            if (!args[j].result) ok = false;
        }
        NOB_FREE(handles); NOB_FREE(args);
        i += batch;
    }
    return ok;
}

#else /* POSIX */

typedef struct { NobexTarget *target; NobexContext *ctx; bool result; } _NobexPosixArg;

static void *_nobex_thread_posix(void *arg)
{
    _NobexPosixArg *a = (_NobexPosixArg*)arg;
    a->result = _nobex_target_build(a->target, a->ctx);
    return NULL;
}

static NOBEX__UNUSED bool _nobex_graph_run_parallel(NobexGraph *g, NobexContext *ctx, int jobs)
{
    size_t i = 0; bool ok = true;
    while (i < g->count && ok) {
        size_t batch = (size_t)jobs < (g->count - i) ? (size_t)jobs : (g->count - i);
        pthread_t *threads = (pthread_t*)NOB_REALLOC(NULL, batch * sizeof(pthread_t));
        _NobexPosixArg *args = (_NobexPosixArg*)NOB_REALLOC(NULL, batch * sizeof(_NobexPosixArg));
        for (size_t j = 0; j < batch; j++) {
            args[j] = (_NobexPosixArg){ g->items[i+j], ctx, false };
            pthread_create(&threads[j], NULL, _nobex_thread_posix, &args[j]);
        }
        for (size_t j = 0; j < batch; j++) {
            pthread_join(threads[j], NULL);
            if (!args[j].result) ok = false;
        }
        NOB_FREE(threads); NOB_FREE(args);
        i += batch;
    }
    return ok;
}

#endif /* parallel */

static NOBEX__UNUSED bool _nobex_graph_run(NobexGraph *g, NobexContext *ctx)
{
    if (ctx->jobs <= 1) return _nobex_graph_run_serial(g, ctx);
    return _nobex_graph_run_parallel(g, ctx, ctx->jobs);
}

bool nobex_run(NobexContext *ctx, const char *name)
{
    NobexTarget *t = _nobex_graph_find(ctx->graph, name);
    if (!t) { nob_log(NOB_ERROR, "nobex: target '%s' not found", name); return false; }
    return _nobex_target_build(t, ctx);
}

/* ── --help and --list ── */

static NOBEX__UNUSED void _nobex_print_target_detail(NobexTarget *t, const char *default_group)
{
    const char *ts = "Executable";
    switch (t->type) {
        case TARGET_STATIC_LIB: ts = "Static lib"; break;
        case TARGET_SHARED_LIB: ts = "Shared lib"; break;
        case TARGET_RULE:       ts = "Rule";       break;
        case TARGET_PHONY:      ts = "Phony";      break;
        default: break;
    }
    printf("    %-20s %-12s", t->name, ts);
    if (t->description) printf("  %s", t->description);
    printf("\n");
    /* groups */
    const char *_dg_arr[2] = { default_group, NULL };
    const char **gs = t->groups ? t->groups : _dg_arr;
    printf("      groups:");
    for (size_t k = 0; gs[k]; k++) printf(" @%s", gs[k]);
    printf("\n");
    /* deps */
    if (t->deps) {
        printf("      deps:");
        for (size_t k = 0; t->deps[k]; k++) printf(" %s", t->deps[k]);
        printf("\n");
    }
    /* vars */
    if (t->vars) {
        printf("      vars:");
        for (size_t k = 0; t->vars[k]; k++) printf("  %s=<value>", t->vars[k]);
        printf("\n");
    }
}

/* nfocused == 0 → full help; nfocused > 0 → show only matching targets, no groups/arguments sections */
static NOBEX__UNUSED void _nobex_print_help(NobexGraph *g, const char *default_group,
                                             const char *prog,
                                             const char **focused, size_t nfocused)
{
    printf("Usage: %s [flags] [target|@group...]\n\n", prog);

    if (nfocused == 0) {
        /* collect group names */
        const char *seen[256]; size_t n = 0;
        for (size_t i = 0; i < g->count; i++) {
            NobexTarget *t = g->items[i];
            const char *_dg_arr[2] = { default_group, NULL };
            const char **gs = t->groups ? t->groups : _dg_arr;
            for (size_t k = 0; gs[k]; k++) {
                bool found = false;
                for (size_t j = 0; j < n; j++) if (strcmp(seen[j], gs[k]) == 0) { found = true; break; }
                if (!found && n < 256) seen[n++] = gs[k];
            }
        }
        printf("Groups:\n");
        for (size_t gi = 0; gi < n; gi++) {
            const char *grp = seen[gi];
            printf("    %-20s%s  ", grp, strcmp(grp, default_group) == 0 ? "(default) " : "          ");
            bool first = true;
            for (size_t i = 0; i < g->count; i++) {
                if (_nobex_target_in_group(g->items[i], grp)) {
                    if (!first) printf(", ");
                    printf("%s", g->items[i]->name);
                    first = false;
                }
            }
            printf("\n");
        }
        printf("\nTargets:\n");
        for (size_t i = 0; i < g->count; i++)
            _nobex_print_target_detail(g->items[i], default_group);
        printf("\nArguments:\n");
        printf("    target         Build a named target directly\n");
        printf("    @group         Build all targets in a group\n");
        printf("    key=value      Set a variable consumed by a target (e.g. install_dir=~/tools)\n");
    } else {
        /* focused: only matching targets, no groups or arguments sections */
        bool any = false;
        for (size_t i = 0; i < g->count; i++) {
            NobexTarget *t = g->items[i];
            for (size_t f = 0; f < nfocused; f++) {
                const char *a = focused[f];
                bool matches = (a[0] == '@')
                    ? _nobex_target_in_group(t, a + 1)
                    : strcmp(t->name, a) == 0;
                if (matches) {
                    if (!any) { printf("Targets:\n"); any = true; }
                    _nobex_print_target_detail(t, default_group);
                    break;
                }
            }
        }
    }

    printf("\nFlags:\n");
    printf("    --version, -V  Show build timestamp\n");
    printf("    --help, -h     Show this help; combine with targets/groups for details\n");
    printf("    --list, -l     List targets\n");
    printf("    -jN            Parallelism (e.g. -j4)\n");
    printf("    -B             Force rebuild, ignore mtimes\n");
    printf("    --dry-run      Print commands without executing\n");
    printf("    --verbose, -v  Print each command before running\n");
    printf("    --watch        Watch mode (mtime poll)\n");
}

static NOBEX__UNUSED void _nobex_print_list(NobexGraph *g)
{
    for (size_t i = 0; i < g->count; i++) {
        NobexTarget *t = g->items[i];
        printf("%-20s", t->name);
        if (t->groups) {
            printf("  [");
            for (size_t j = 0; t->groups[j]; j++) { if (j) printf(","); printf("%s", t->groups[j]); }
            printf("]");
        } else { printf("  [%s]", _nobex_default_group ? _nobex_default_group : "build"); }
        if (t->description) printf("  %s", t->description);
        if (t->vars) {
            printf("  vars:");
            for (size_t k = 0; t->vars[k]; k++) printf(" %s", t->vars[k]);
        }
        printf("\n");
    }
}

/* ── Watch loop ── */

#include <signal.h>

static NOBEX__UNUSED volatile int _nobex_watch_running = 1;

#ifdef _WIN32
static BOOL WINAPI _nobex_ctrl_handler(DWORD type)
{
    if (type == CTRL_C_EVENT) { _nobex_watch_running = 0; return TRUE; }
    return FALSE;
}
#else
static NOBEX__UNUSED void _nobex_sigint(int sig) { (void)sig; _nobex_watch_running = 0; }
#endif

static NOBEX__UNUSED time_t _nobex_file_mtime(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0) return 0;
#ifdef __APPLE__
    return st.st_mtimespec.tv_sec;
#else
    return st.st_mtime;
#endif
}

typedef struct { const char *path; time_t mtime; } _NobexWatchFile;

static NOBEX__UNUSED bool _nobex_watch_loop(NobexGraph *subgraph, NobexContext *ctx)
{
#ifdef _WIN32
    SetConsoleCtrlHandler(_nobex_ctrl_handler, TRUE);
#else
    signal(SIGINT, _nobex_sigint);
#endif

    _NobexWatchFile *files = NULL;
    size_t nfiles = 0, fcap = 0;

    for (size_t i = 0; i < subgraph->count; i++) {
        NobexTarget *t = subgraph->items[i];
        if (t->watch.skip || !t->sources) continue;
        for (size_t j = 0; t->sources[j]; j++) {
            if (nfiles >= fcap) {
                fcap = fcap == 0 ? 16 : fcap * 2;
                files = (_NobexWatchFile*)NOB_REALLOC(files, fcap * sizeof(*files));
            }
            files[nfiles++] = (_NobexWatchFile){ t->sources[j], _nobex_file_mtime(t->sources[j]) };
        }
    }

    nob_log(NOB_INFO, "nobex: watch started — Ctrl+C to stop");

    while (_nobex_watch_running) {
        bool changed = false;
        for (size_t i = 0; i < nfiles; i++) {
            time_t m = _nobex_file_mtime(files[i].path);
            if (m != files[i].mtime) {
                nob_log(NOB_INFO, "nobex: change detected in '%s'", files[i].path);
                files[i].mtime = m;
                changed = true;
            }
        }

        if (changed) {
            uint32_t deb = NOBEX_DEFAULT_DEBOUNCE_MS;
            struct timespec ts = { 0, (long)deb * 1000000L };
            nanosleep(&ts, NULL);

            for (size_t i = 0; i < subgraph->count; i++) {
                NobexTarget *t = subgraph->items[i];
                if (!t->watch.skip && t->watch.on_change) t->watch.on_change(t, ctx);
            }

            bool ok = _nobex_graph_run(subgraph, ctx);

            for (size_t i = 0; i < subgraph->count; i++) {
                NobexTarget *t = subgraph->items[i];
                if (t->watch.skip) continue;
                if ( ok && t->watch.on_built) t->watch.on_built(t, ctx);
                if (!ok && t->watch.on_error) t->watch.on_error(t, ctx);
            }
        }

        uint32_t poll = NOBEX_DEFAULT_POLL_MS;
        struct timespec ts = { 0, (long)poll * 1000000L };
        nanosleep(&ts, NULL);
    }

    NOB_FREE(files);
    nob_log(NOB_INFO, "nobex: watch stopped");
    return true;
}

/* ── Self-rebuild ──
 *
 * Cannot use NOB_GO_REBUILD_URSELF because __FILE__ inside this header
 * resolves to "nobex.h", not "nob.c". We explicitly search for nob.c /
 * build.c and rebuild when they are newer than the running binary.
 */
static NOBEX__UNUSED void _nobex_self_rebuild(int argc, char **argv)
{
    const char *sources[] = { "nob.c", "build.c", NULL };
    const char *binary    = argv[0];

    /* suppress INFO noise (e.g. mkdir) while only doing mtime checks */
    Nob_Log_Level saved = nob_minimal_log_level;
    nob_minimal_log_level = NOB_WARNING;

    for (size_t i = 0; sources[i]; i++) {
        if (!nob_file_exists(sources[i])) continue;
        int r = nob_needs_rebuild1(binary, sources[i]);
        if (r < 0) exit(1);
        if (!r) { nob_minimal_log_level = saved; break; }

        nob_minimal_log_level = saved;
        nob_log(NOB_INFO, "nobex: rebuilding %s -> %s", sources[i], binary);

        const char *old = nob_temp_sprintf("%s.old", binary);
        if (!nob_rename(binary, old)) exit(1);

        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, NOB_REBUILD_URSELF(binary, sources[i]));
        if (!nob_cmd_run(&cmd)) { nob_rename(old, binary); exit(1); }

        Nob_Cmd re = {0};
        nob_cmd_append(&re, binary);
        for (int j = 1; j < argc; j++) nob_cmd_append(&re, argv[j]);
        if (!nob_cmd_run(&re, .dont_reset = true)) exit(1);
        exit(0);
    }
}

static NOBEX__UNUSED int _nobex_parse_jobs(const char *s)
{
    if (!s || *s == '\0') return 1;
    int n = atoi(s);
    return n > 0 ? n : 1;
}

#endif /* NOBEX_IMPLEMENTATION || NOBEX_CLI */

/* =========================================================
 * K — Project main() (NOBEX_IMPLEMENTATION)
 * ========================================================= */

#if defined(NOBEX_IMPLEMENTATION) && !defined(NOBEX_NO_MAIN)


int main(int argc, char **argv)
{
    _nobex_self_rebuild(argc, argv);

    NobexGraph g = {0};
    _nobex_collect_targets(&g);

    if (g.count == 0) {
        nob_log(NOB_ERROR, "nobex: no targets declared");
        return 1;
    }
    if (!_nobex_validate_graph(&g)) return 1;

    bool dry_run = false, verbose = false, watch = false, force = false;
    bool show_help = false, show_list = false, show_version = false;
    int  jobs = 1;
    const char *req_args[64];  /* positional args: "name" = target, "@group" = group */
    size_t nargs = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if      (strcmp(a, "--help")    == 0 || strcmp(a, "-h") == 0) show_help    = true;
        else if (strcmp(a, "--list")    == 0 || strcmp(a, "-l") == 0) show_list    = true;
        else if (strcmp(a, "--version") == 0 || strcmp(a, "-V") == 0) show_version = true;
        else if (strcmp(a, "--dry-run") == 0) dry_run = true;
        else if (strcmp(a, "--verbose") == 0 || strcmp(a, "-v") == 0) verbose = true;
        else if (strcmp(a, "--watch")   == 0) watch = true;
        else if (strcmp(a, "-B")        == 0) force = true;
        else if (strncmp(a, "-j", 2)   == 0) {
            if (a[2] != '\0') {
                jobs = _nobex_parse_jobs(a + 2);
            } else if (i + 1 < argc) {
                jobs = _nobex_parse_jobs(argv[++i]);
            }
        } else if (a[0] != '-' && nargs < 64) {
            req_args[nargs++] = a;
        } else {
            nob_log(NOB_WARNING, "nobex: unknown flag '%s'", a);
        }
    }

    /* split req_args into key=value pairs (store) and positional targets */
    const char *cli_var_keys[64];
    const char *cli_var_vals[64];
    size_t      nvars = 0;
    const char *pos_args[64];
    size_t      npos = 0;
    for (size_t i = 0; i < nargs; i++) {
        const char *a  = req_args[i];
        const char *eq = strchr(a, '=');
        if (eq && eq != a && a[0] != '@') {
            /* key=value — only if key contains no spaces/slashes (not a path) */
            bool valid = true;
            for (const char *p = a; p < eq; p++) {
                if (*p == '/' || *p == '\\' || *p == ' ') { valid = false; break; }
            }
            if (valid && nvars < 64) {
                cli_var_keys[nvars] = nob_temp_sprintf("%.*s", (int)(eq - a), a);
                cli_var_vals[nvars] = eq + 1;
                nvars++;
                continue;
            }
        }
        if (npos < 64) pos_args[npos++] = a;
    }
    /* rebuild req_args/nargs from positional-only */
    nargs = npos;
    for (size_t i = 0; i < npos; i++) req_args[i] = pos_args[i];

    const char *default_group = _nobex_default_group ? _nobex_default_group : "build";

    if (show_version) { printf("nobex built %s %s\n", __DATE__, __TIME__); return 0; }
    if (show_help) {
        _nobex_print_help(&g, default_group, argv[0], req_args, nargs);
        return 0;
    }
    if (show_list) { _nobex_print_list(&g); return 0; }

    /* default: run the default group */
    if (nargs == 0) { req_args[0] = nob_temp_sprintf("@%s", default_group); nargs = 1; }

    NobexStore   store = {0};
    NobexContext ctx   = { .graph = &g, .store = &store,
                           .jobs = jobs, .dry_run = dry_run, .verbose = verbose,
                           .force = force };

    /* Collect the set of targets that will run, to validate CLI vars */
    NobexTarget *will_run[256]; size_t nwill = 0;
    for (size_t i = 0; i < nargs && nwill < 256; i++) {
        const char *a = req_args[i];
        if (a[0] == '@') {
            const char *grp = a + 1;
            for (size_t j = 0; j < g.count && nwill < 256; j++)
                if (_nobex_target_in_group(g.items[j], grp))
                    will_run[nwill++] = g.items[j];
        } else {
            NobexTarget *t = _nobex_graph_find(&g, a);
            if (t && nwill < 256) will_run[nwill++] = t;
        }
    }
    /* Inject only vars declared by at least one target that will run.
     * Warn about vars passed on the CLI that no scheduled target declares. */
    for (size_t i = 0; i < nvars; i++) {
        bool claimed = false;
        for (size_t j = 0; j < nwill && !claimed; j++) {
            NobexTarget *t = will_run[j];
            if (!t->vars) continue;
            for (size_t k = 0; t->vars[k] && !claimed; k++)
                if (strcmp(t->vars[k], cli_var_keys[i]) == 0) claimed = true;
        }
        if (claimed) {
            nobex_set(&ctx, cli_var_keys[i], _nobex_expand_path(cli_var_vals[i]));
        } else {
            nob_log(NOB_WARNING, "nobex: variable '%s' is not used by any scheduled target",
                    cli_var_keys[i]);
        }
    }

    Nob_Log_Level saved = nob_minimal_log_level;
    nob_minimal_log_level = NOB_WARNING;
    nob_mkdir_if_not_exists(NOBEX_CACHE_DIR);
    nob_minimal_log_level = saved;

    /* execute positional args in order */
    for (size_t i = 0; i < nargs; i++) {
        const char *a = req_args[i];
        if (a[0] == '@') {
            /* group — expand and run all matching targets in graph order */
            const char *grp = a + 1;
            bool found = false;
            for (size_t j = 0; j < g.count; j++) {
                if (_nobex_target_in_group(g.items[j], grp)) {
                    found = true;
                    if (!_nobex_target_build(g.items[j], &ctx)) return 1;
                }
            }
            if (!found) {
                nob_log(NOB_ERROR, "nobex: no targets found for group '%s'", grp);
                return 1;
            }
        } else {
            /* named target */
            NobexTarget *t = _nobex_graph_find(&g, a);
            if (!t) {
                nob_log(NOB_ERROR, "nobex: target '%s' not found", a);
                return 1;
            }
            if (watch) {
                nob_log(NOB_WARNING, "nobex: --watch is not supported with named targets");
                return 1;
            }
            if (!_nobex_target_build(t, &ctx)) return 1;
        }
    }

    return 0;
}

#endif /* NOBEX_IMPLEMENTATION */

/* =========================================================
 * L — Standalone CLI main() (NOBEX_CLI)
 * ========================================================= */

#ifdef NOBEX_CLI

static NOBEX__UNUSED void _nobex_cli_exec(const char *binary, int argc, char **argv)
{
#ifdef _WIN32
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, binary);
    for (int i = 0; i < argc; i++) nob_cmd_append(&cmd, argv[i]);
    if (!nob_cmd_run(&cmd, .dont_reset = true)) exit(1);
    exit(0);
#else
    const char **args = (const char**)NOB_REALLOC(NULL, ((size_t)argc + 2) * sizeof(char*));
    args[0] = binary;
    for (int i = 0; i < argc; i++) args[i + 1] = argv[i];
    args[argc + 1] = NULL;
    execv(binary, (char* const*)args);
    nob_log(NOB_ERROR, "[nobex] execv failed: %s", strerror(errno));
    fprintf(stderr,"while running cmd: %s", binary);
    for (int i = 0; i < argc; i++) fprintf(stderr," %s", argv[i]);
    fprintf(stderr,"\n");
    exit(1);
#endif
}


int main(int argc, char **argv)
{


    nob_shift(argv, argc); /* consume program name */

    /* intercept --version before forwarding to the build script */
    for (int _i = 0; _i < argc; _i++) {
        if (strcmp(argv[_i], "--version") == 0 || strcmp(argv[_i], "-V") == 0) {
            printf("nobex built %s %s\n", __DATE__, __TIME__);
            return 0;
        }
    }

    const char *source = NULL;
    if (argc > 0 && strcmp(argv[0], "run") == 0) {
        nob_shift(argv, argc);
        if (argc > 0) { source = argv[0]; nob_shift(argv, argc); }
    }

    if (!source) {
        if      (nob_file_exists("nob.c"))   source = "nob.c";
        else if (nob_file_exists("build.c")) source = "build.c";
        else {
            nob_log(NOB_ERROR, "nobex: no nob.c or build.c found");
            return 1;
        }
    }

    Nob_Log_Level _cli_saved = nob_minimal_log_level;
    nob_minimal_log_level = NOB_WARNING;
    nob_mkdir_if_not_exists(NOBEX_CACHE_DIR);
    nob_minimal_log_level = _cli_saved;

    const char *base   = nob_temp_file_name(source);
    const char *dot    = strrchr(base, '.');
    const char *stem   = dot ? nob_temp_sprintf("%.*s", (int)(dot - base), base) : base;
    const char *binary = nob_temp_sprintf("%s/%s", NOBEX_CACHE_DIR, stem);

    int needs = nob_needs_rebuild1(binary, source);
    if (needs < 0) return 1;

    if (needs || !nob_file_exists(binary)) {
        nob_log(NOB_INFO, "nobex: compiling %s -> %s", source, binary);

        const char *extra_c = nobex_xflags_get(source, "cflags");
        const char *extra_l = nobex_xflags_get(source, "lflags");
        Nob_Cmd cmd = {0};
        nob_cc(&cmd);
        nob_cmd_append(&cmd, source);
        nob_cc_output(&cmd, binary);
        if (extra_c) nob_cmd_append(&cmd, extra_c);
        if (extra_l) nob_cmd_append(&cmd, extra_l);

        if (!nob_cmd_run(&cmd)) {
            nob_log(NOB_ERROR, "nobex: compilation of '%s' failed", source);
            return 1;
        }
    }

    _nobex_cli_exec(binary, argc, argv);
    return 0;
}

#endif /* NOBEX_CLI */
