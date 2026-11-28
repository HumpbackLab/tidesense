#include "tidesense.h"
#include <string.h>

static const struct ts_mode modes[] = {
 { "400p40", 640, 400, 40, 2, 60 },
 { "800p60", 1280, 800, 60, 1, 35 },
 { "480p180", 640, 480, 180, 1, 30 },
 { "400p210", 640, 400, 210, 1, 30 },
};

const struct ts_mode *ts_mode_find(const char *name) {
 for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
  if (!strcmp(name, modes[i].name)) return &modes[i];
 return NULL;
}
