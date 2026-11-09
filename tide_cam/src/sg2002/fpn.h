#ifndef TIDE_CAM_FPN_H
#define TIDE_CAM_FPN_H

#include <stdint.h>

/* Dark- and flat-field calibration for the monochrome OV9281 path: a global
 * black balance, per-column Q8 fixed-pattern offsets, and per-column Q12 flat
 * gains at several analog gain settings. tide_fpn_apply interpolates by the
 * AE-reported analog gain, subtracts black, attenuates the column offsets as
 * signal rises above black, then applies the flat-field column gain. */
typedef struct tide_fpn tide_fpn;

/* dir holds cam0.fpn / cam1.fpn. NULL/empty dir, missing files or unusable
 * files return NULL; malformed files also print one warning. */
tide_fpn *tide_fpn_load(const char *dir, int pipe, uint32_t width, uint32_t height);
void tide_fpn_free(tide_fpn *fpn);

/* Correct y in place by subtracting black, attenuating dark column offsets by
 * signal level, then multiplying by the flat-field column gain. Returns 1 if
 * any pixel was written, 0 if the gain is unusable or no correction applies. */
int tide_fpn_apply(tide_fpn *fpn, uint32_t gain_q10, uint8_t *y,
                   uint32_t width, uint32_t height, uint32_t stride);

#endif
