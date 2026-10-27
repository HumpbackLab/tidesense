#define _POSIX_C_SOURCE 200809L
#include "tide_cam.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void message(char *error, uint32_t size, const char *value)
{
    if (error && size) snprintf(error, size, "%s", value);
}

int tide_cam_config_load_file(const char *path, tide_cam_config *config,
                              char *error, uint32_t error_size)
{
    if (!path || !*path || !config) {
        message(error, error_size, "Configuration path and config are required");
        return TIDE_CAM_INVALID;
    }
    FILE *file = fopen(path, "r");
    if (!file) {
        int code = errno;
        if (error && error_size)
            snprintf(error, error_size, "%s: %s", path, strerror(code));
        return code == ENOENT ? TIDE_CAM_NOT_FOUND : TIDE_CAM_IO;
    }
    char line[256];
    unsigned number = 0, found = 0;
    uint32_t limit = 0;
    int status = TIDE_CAM_OK;
    while (fgets(line, sizeof(line), file)) {
        ++number;
        size_t length = strlen(line);
        if (length && line[length - 1] != '\n' && !feof(file)) {
            status = TIDE_CAM_INVALID;
            break;
        }
        char *start = line;
        while (isspace((unsigned char)*start)) ++start;
        if (!*start || *start == '#') continue;
        char *equal = strchr(start, '=');
        if (!equal || found) { status = TIDE_CAM_INVALID; break; }
        char *key_end = equal;
        while (key_end > start && isspace((unsigned char)key_end[-1])) --key_end;
        if ((size_t)(key_end - start) != strlen("exposure_max_us") ||
            strncmp(start, "exposure_max_us", key_end - start)) {
            status = TIDE_CAM_INVALID;
            break;
        }
        char *value = equal + 1;
        while (isspace((unsigned char)*value)) ++value;
        if (*value < '1' || *value > '9') { status = TIDE_CAM_INVALID; break; }
        errno = 0;
        char *end;
        unsigned long parsed = strtoul(value, &end, 10);
        while (isspace((unsigned char)*end)) ++end;
        if (errno || *end || parsed > 50000 || parsed > UINT_MAX) {
            status = TIDE_CAM_INVALID;
            break;
        }
        limit = (uint32_t)parsed;
        found = 1;
    }
    if (ferror(file)) status = TIDE_CAM_IO;
    if (fclose(file)) status = TIDE_CAM_IO;
    if (status == TIDE_CAM_OK && !found) status = TIDE_CAM_INVALID;
    if (status != TIDE_CAM_OK) {
        if (status == TIDE_CAM_IO) message(error, error_size, "Cannot read camera settings");
        else if (error && error_size)
            snprintf(error, error_size, "%s:%u: expected one exposure_max_us=1..50000", path, number);
        return status;
    }
    config->exposure_max_us = limit;
    message(error, error_size, "");
    return TIDE_CAM_OK;
}
