// Compile the original macro-OFF path independently, with private symbol names.
#undef SCHUR_OPT_ALIGN2D_HESSIAN_NEON
#define SCHUR_OPT_ALIGN2D_HESSIAN_NEON 0
#undef SCHUR_OPT_ALIGN2D_NEON
#define SCHUR_OPT_ALIGN2D_NEON 0
#define align1D align1D_reference
#define align2D align2D_reference
#define align2D_SSE2 align2D_SSE2_reference
#define align2D_NEON align2D_NEON_reference
#define alignPyr2D alignPyr2D_reference
#define alignPyr2DVec alignPyr2DVec_reference
#include SCHUR_ALIGNMENT_SOURCE
