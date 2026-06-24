//> lflags : -lpthread

#define NOB_IMPLEMENTATION
#include "nob.h"

#define NOBEX_DEFAULT_GROUP "all"
#define NOBEX_IMPLEMENTATION
#include "nobex.h"

#define BUILD_DIR "build"

bool clean(NobexContext *ctx)
{
    return nobex_cleanup(BUILD_DIR "/*.o", BUILD_DIR "/nobex", BUILD_DIR "/test_xflags");
}

bool install(NobexContext *ctx)
{
    const char *dest = nobex_get(ctx, "install_dir");
    if (!dest) dest = "/usr/local/bin";
    if (!nob_file_exists(BUILD_DIR "/nobex")) {
        nob_log(NOB_ERROR, "Could not install because there is no executable built");
        return false;
    }
    const char *target_path = nob_temp_sprintf("%s/nobex", dest);
    return nob_copy_file(BUILD_DIR "/nobex", target_path);
}


bool test(NobexContext *ctx)
{
    if (!nobex_run(ctx, "test_xflags")) return false;

    nob_log(NOB_INFO, "Running test cases\n");
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, BUILD_DIR "/test_xflags");
    return nob_cmd_run(&cmd);
}

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

NOB_PHONY(clean, .groups = GROUPS("clean"), .description = "remove build artifacts");
NOB_PHONY(install, .deps = DEPS("nobex"), .groups = GROUPS("install"), .description = "install nobex to /usr/local/bin", .vars = VARS("install_dir"));
NOB_PHONY(test, .groups = GROUPS("test"), .description = "build and run test_xflags");
