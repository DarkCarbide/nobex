//> lflags : -lpthread

#define NOB_IMPLEMENTATION
#include "nob.h"

#define NOBEX_DEFAULT_GROUP "all"
#define NOBEX_IMPLEMENTATION
#include "nobex.h"

bool clean(NobexContext *ctx)
{
    (void)ctx;
    nob_delete_file("nobex");
    return true;
}

NOB_PHONY(clean, .groups = GROUPS("clean"));

NOB_ARTIFACT(nobex,
    .sources    = SRCS("nobex.h"),
    .output     = "nobex",
    .type       = TARGET_EXECUTABLE,
    .cflags     = FLAGS("-x", "c", "-DNOBEX_CLI"),
    .lflags     = FLAGS("-lpthread"),
    .is_default = true,
);

NOB_ARTIFACT(test_xflags,
    .sources = SRCS("test_xflags.c"),
    .output  = "test_xflags",
    .type    = TARGET_EXECUTABLE,
);

bool test(NobexContext *ctx)
{
    if (!nobex_run(ctx, "test_xflags")) return false;

    nob_log(NOB_INFO, "Running test cases\n");
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "./test_xflags");
    return nob_cmd_run(&cmd);
}

NOB_PHONY(test, .groups = GROUPS(NOBEX_DEFAULT_GROUP, "test"));
