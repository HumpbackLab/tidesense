#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "loader.h"

static void *read_image(const char *path, __u32 *size)
{
    struct stat st;
    FILE *f = fopen(path, "rb");
    void *data;
    if (!f || fstat(fileno(f), &st) || st.st_size < 64 || st.st_size > C906_FIRMWARE_SIZE) {
        fprintf(stderr, "invalid flat firmware: %s\n", path);
        if (f) fclose(f);
        return NULL;
    }
    *size = st.st_size;
    data = malloc(*size);
    if (!data || fread(data, 1, *size, f) != *size) { free(data); data = NULL; }
    fclose(f);
    return data;
}

int main(int argc, char **argv)
{
    int fd, ret = 1;
    if (argc < 2 ||
        (strcmp(argv[1], "status") && strcmp(argv[1], "load")) ||
        (!strcmp(argv[1], "status") && argc != 2) ||
        (!strcmp(argv[1], "load") && argc != 3)) {
        fprintf(stderr, "usage: %s status\n       %s load firmware.bin\n",
                argv[0], argv[0]);
        return 2;
    }
    fd = open("/dev/c906-loader", O_RDWR);
    if (fd < 0) { perror("open /dev/c906-loader"); return 1; }
    if (!strcmp(argv[1], "status")) {
        struct c906_loader_status s = {0};
        if (ioctl(fd, C906_IOCTL_STATUS, &s)) perror("C906 status");
        else {
            printf("phase=%u echo=%d echo_us=%llu config=%08x magic=%08x reload=%u\n",
                   s.gate_phase, s.echo_result, (unsigned long long)s.echo_us,
                   s.transfer_config, s.firmware_magic, s.reload_supported);
            ret = s.echo_result ? 1 : 0;
        }
    } else {
        struct c906_reload_request r = {0};
        void *image = read_image(argv[2], &r.image_bytes);
        if (!image) goto free_images;
        r.image = (uintptr_t)image;
        if (ioctl(fd, C906_IOCTL_RELOAD, &r)) perror("C906 reload");
        else {
            printf("result=%d park_us=%llu copy_us=%llu startup_us=%llu total_us=%llu config=%08x\n",
                   r.result, (unsigned long long)r.park_us, (unsigned long long)r.copy_us,
                   (unsigned long long)r.startup_us, (unsigned long long)r.total_us,
                   r.transfer_config);
            ret = r.result ? 1 : 0;
        }
free_images:
        free(image);
    }
    close(fd);
    return ret;
}
