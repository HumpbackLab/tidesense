// Independent macro-OFF compilation of the actual production source.
#undef SCHUR_OPT_WARP_AFFINE_NEON
#define SCHUR_OPT_WARP_AFFINE_NEON 0
#define warp warp_reference
#include SCHUR_WARP_SOURCE
