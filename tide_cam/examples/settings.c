#define _POSIX_C_SOURCE 200809L
#include "tide_cam.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int show(const char *path)
{
    tide_cam_config config = {.exposure_max_us = TIDE_CAM_DEFAULT_EXPOSURE_MAX_US};
    char error[256];
    int status = tide_cam_config_load_file(path, &config, error, sizeof(error));
    if (status != TIDE_CAM_OK && status != TIDE_CAM_NOT_FOUND) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    printf("exposure_max_us=%u\nsource=%s\n", config.exposure_max_us,
           status == TIDE_CAM_NOT_FOUND ? "default" : "file");
    return 0;
}

static int save(const char *path, const char *value)
{
    char directory[PATH_MAX], temporary[PATH_MAX];
    size_t length = strlen(path);
    if (!length || length >= sizeof(directory) || path[0] != '/') {
        fprintf(stderr, "Settings path must be absolute\n");
        return 1;
    }
    memcpy(directory, path, length + 1);
    char *slash = strrchr(directory, '/');
    if (!slash || slash == directory) {
        fprintf(stderr, "Invalid settings directory\n");
        return 1;
    }
    *slash = '\0';
    if (mkdir(directory, 0700) && errno != EEXIST) {
        perror(directory);
        return 1;
    }
    if (snprintf(temporary, sizeof(temporary), "%s/.tide-cam.XXXXXX", directory) >=
        (int)sizeof(temporary)) return 1;
    int fd = mkstemp(temporary);
    if (fd < 0) { perror("mkstemp"); return 1; }
    int result = 1;
    if (dprintf(fd, "exposure_max_us=%s\n", value) < 0 || fsync(fd)) {
        fprintf(stderr, "Cannot write camera settings\n");
        goto cleanup;
    }
    if (close(fd)) { fd = -1; perror("close"); goto cleanup; }
    fd = -1;
    tide_cam_config config = {.exposure_max_us = TIDE_CAM_DEFAULT_EXPOSURE_MAX_US};
    char error[256];
    if (tide_cam_config_load_file(temporary, &config, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        goto cleanup;
    }
    if (rename(temporary, path)) { perror("rename"); goto cleanup; }
    printf("exposure_max_us=%u\nsource=file\n", config.exposure_max_us);
    result = 0;
cleanup:
    if (fd >= 0) close(fd);
    if (result) unlink(temporary);
    return result;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "get")) return show(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "set")) return save(argv[2], argv[3]);
    fprintf(stderr, "Usage: tide_cam_settings get PATH | set PATH EXPOSURE_MAX_US\n");
    return 2;
}
