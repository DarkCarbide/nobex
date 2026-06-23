//> lflags : -lpthread

#define NOB_IMPLEMENTATION
#include "nob.h"

#define NOBEX_DEFAULT_GROUP "all"
#define NOBEX_IMPLEMENTATION
#include "nobex.h"

#define BUILD_DIR "build"

bool clean(NobexContext *ctx)
{
    (void)ctx;
    if(nob_file_exists(BUILD_DIR "/nobex")) nob_delete_file(BUILD_DIR "/nobex");
    if(nob_file_exists(BUILD_DIR "/nobex.h.o")) nob_delete_file(BUILD_DIR "/nobex.h.o");
    if(nob_file_exists(BUILD_DIR "/test_xflags")) nob_delete_file(BUILD_DIR "/test_xflags");
    if(nob_file_exists(BUILD_DIR "/test_xflags.c.o")) nob_delete_file(BUILD_DIR "/test_xflags.c.o");
    return true;
}

NOB_PHONY(clean, .groups = GROUPS("clean"), .description = "remove build artifacts");

NOB_ARTIFACT(nobex,
    .sources     = SRCS("nobex.h"),
    .inputs      = INPUTS("nob.h"),
    .output      = BUILD_DIR "/nobex",
    .type        = TARGET_EXECUTABLE,
    .cflags      = FLAGS("-x", "c", "-DNOBEX_CLI"),
    .lflags      = FLAGS("-lpthread"),
    .description = "standalone nobex CLI binary",
);

NOB_ARTIFACT(test_xflags,
    .sources     = SRCS("test_xflags.c"),
    .inputs      = INPUTS("nobex.h", "nob.h"),
    .output      = BUILD_DIR "/test_xflags",
    .type        = TARGET_EXECUTABLE,
    .description = "xflags parser unit tests",
);

bool install(NobexContext *ctx)
{
    (void)ctx;
    if(nob_file_exists(BUILD_DIR "/nobex")) return nob_copy_file(BUILD_DIR "/nobex", "/usr/local/bin/nobex");
    nob_log(NOB_ERROR,"Could not install because there no executable built");
    return 0;
}

NOB_PHONY(install, .deps = DEPS("nobex"), .groups = GROUPS("install"), .description = "install nobex to /usr/local/bin");

bool test(NobexContext *ctx)
{
    if (!nobex_run(ctx, "test_xflags")) return false;

    nob_log(NOB_INFO, "Running test cases\n");
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, BUILD_DIR "/test_xflags");
    return nob_cmd_run(&cmd);
}

NOB_PHONY(test, .groups = GROUPS("test"), .description = "build and run test_xflags");
