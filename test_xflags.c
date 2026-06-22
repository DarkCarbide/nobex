//> cflags : -Wall -Wextra
//> libs   : -lm

/* test_xflags.c — unit tests for the xflags parser embedded in nobex.h.
 *
 * Build & run:
 *   gcc -Wall -Wextra test_xflags.c -o test_xflags && ./test_xflags
 *
 * nob.h and nobex.h must be in the same directory.
 * Does NOT define NOBEX_IMPLEMENTATION — provides its own main().
 */

#define NOB_IMPLEMENTATION
#include "nob.h"

/* Pull the implementation block without emitting nobex's main(). */
#define NOBEX_IMPLEMENTATION
#define NOBEX_NO_MAIN
#include "nobex.h"

/* ── helpers ── */

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(label, cond) do { \
    if (cond) { \
        fprintf(stdout, "  PASS  %s\n", (label)); \
        g_passed++; \
    } else { \
        fprintf(stdout, "  FAIL  %s\n", (label)); \
        g_failed++; \
    } \
} while (0)

#define CHECK_STR(label, got, want) do { \
    const char *_g = (got), *_w = (want); \
    bool _ok = (_g != NULL) && strcmp(_g, _w) == 0; \
    if (_ok) { \
        fprintf(stdout, "  PASS  %s\n", (label)); \
        g_passed++; \
    } else { \
        fprintf(stdout, "  FAIL  %s  (got='%s', want='%s')\n", (label), \
                _g ? _g : "(null)", _w); \
        g_failed++; \
    } \
} while (0)

/* ── callback helper ── */

typedef struct {
    const char *keys[16];
    const char *values[16];
    size_t      count;
} Collected;

static void collect_cb(const char *key, const char *value, void *ud)
{
    Collected *c = (Collected*)ud;
    if (c->count < 16) {
        c->keys[c->count]   = key;
        c->values[c->count] = value;
        c->count++;
    }
}

/* ── test suites ── */

static void test_get(void)
{
    printf("\n[nobex_xflags_get]\n");

    /* This file has //> cflags and //> libs at the very top. */
    const char *cf = nobex_xflags_get(__FILE__, "cflags");
    CHECK_STR("reads 'cflags' from own source file", cf, "-Wall -Wextra");

    const char *libs = nobex_xflags_get(__FILE__, "libs");
    CHECK_STR("reads 'libs' from own source file", libs, "-lm");

    CHECK("returns NULL for unknown key",   nobex_xflags_get(__FILE__, "nope") == NULL);
    CHECK("returns NULL for missing file",  nobex_xflags_get("/no/such/file.c", "cflags") == NULL);
}

static void test_each_order(void)
{
    printf("\n[nobex_xflags_each — order and completeness]\n");

    Collected c = {0};
    nobex_xflags_each(__FILE__, collect_cb, &c);

    CHECK("collects exactly 2 entries",      c.count == 2);
    if (c.count >= 1) {
        CHECK_STR("entry[0] key",   c.keys[0],   "cflags");
        CHECK_STR("entry[0] value", c.values[0], "-Wall -Wextra");
    }
    if (c.count >= 2) {
        CHECK_STR("entry[1] key",   c.keys[1],   "libs");
        CHECK_STR("entry[1] value", c.values[1], "-lm");
    }
}

static void test_no_xflags(void)
{
    printf("\n[nobex_xflags — file without xflags]\n");

    /* nob.h starts with a C block comment, not //> */
    CHECK("get returns NULL on nob.h",      nobex_xflags_get("nob.h", "cflags") == NULL);

    Collected c = {0};
    nobex_xflags_each("nob.h", collect_cb, &c);
    CHECK("each collects 0 entries on nob.h", c.count == 0);
}

static void test_edge_cases(void)
{
    printf("\n[nobex_xflags — edge cases via temp file]\n");

    const char *tmp = "/tmp/nobex_xflags_edge.c";

    /* ── stops at blank line ── */
    const char *src1 =
        "//> key1 : value1\n"
        "//> key2  :  padded value  \n"
        "//> key3:nospace\n"
        "\n"                             /* blank line — parser must stop here */
        "//> hidden : should not appear\n";
    nob_write_entire_file(tmp, src1, strlen(src1));

    Collected c1 = {0};
    nobex_xflags_each(tmp, collect_cb, &c1);
    CHECK("stops at blank line — 3 entries",            c1.count == 3);
    if (c1.count >= 1) CHECK_STR("key1 value",          c1.values[0], "value1");
    if (c1.count >= 2) CHECK_STR("key2 trims whitespace", c1.values[1], "padded value");
    if (c1.count >= 3) CHECK_STR("key3 no-space colon", c1.values[2], "nospace");
    CHECK("hidden key not visible",  nobex_xflags_get(tmp, "hidden") == NULL);

    /* ── stops at non-//> line ── */
    const char *src2 =
        "//> first : a\n"
        "// ordinary comment\n"
        "//> second : b\n";
    nob_write_entire_file(tmp, src2, strlen(src2));

    Collected c2 = {0};
    nobex_xflags_each(tmp, collect_cb, &c2);
    CHECK("stops at non-//> line — 1 entry", c2.count == 1);

    /* ── empty value stops parsing ── */
    const char *src3 = "//> empty :  \n//> after : x\n";
    nob_write_entire_file(tmp, src3, strlen(src3));

    Collected c3 = {0};
    nobex_xflags_each(tmp, collect_cb, &c3);
    CHECK("empty value stops parsing", c3.count == 0);

    /* ── empty key stops parsing ── */
    const char *src4 = "//> : value\n//> after : x\n";
    nob_write_entire_file(tmp, src4, strlen(src4));

    Collected c4 = {0};
    nobex_xflags_each(tmp, collect_cb, &c4);
    CHECK("empty key stops parsing", c4.count == 0);

    nob_delete_file(tmp);
}

static void test_store(void)
{
    printf("\n[NobexStore — set/get/overwrite]\n");

    NobexStore   store = {0};
    NobexGraph   g     = {0};
    NobexContext ctx   = { .graph = &g, .store = &store };

    nobex_set(&ctx, "cc.flags", "-O2");
    CHECK_STR("get after first set",          nobex_get(&ctx, "cc.flags"), "-O2");

    nobex_set(&ctx, "cc.flags", "-O3");
    CHECK_STR("set overwrites existing key",  nobex_get(&ctx, "cc.flags"), "-O3");

    nobex_set(&ctx, "libs", "-lm -lpthread");
    CHECK_STR("second key readable",          nobex_get(&ctx, "libs"), "-lm -lpthread");

    CHECK("get returns NULL for unknown key", nobex_get(&ctx, "unknown") == NULL);

    NOB_FREE(store.keys);
    NOB_FREE(store.values);
}

/* ── entry point ── */

int main(void)
{
    printf("=== nobex xflags test suite ===\n");

    test_get();
    test_each_order();
    test_no_xflags();
    test_edge_cases();
    test_store();

    printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
