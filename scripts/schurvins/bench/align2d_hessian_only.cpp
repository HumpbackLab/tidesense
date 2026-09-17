// Compile the Hessian-only path (old interpolation macro OFF) independently, with private symbol names.
#undef SCHUR_OPT_ALIGN2D_HESSIAN_NEON
#define SCHUR_OPT_ALIGN2D_HESSIAN_NEON 1
#undef SCHUR_OPT_ALIGN2D_NEON
#define SCHUR_OPT_ALIGN2D_NEON 0
#define align1D align1D_hessian_only
#define align2D align2D_hessian_only
#define align2D_SSE2 align2D_SSE2_hessian_only
#define align2D_NEON align2D_NEON_hessian_only
#define alignPyr2D alignPyr2D_hessian_only
#define alignPyr2DVec alignPyr2DVec_hessian_only
#include SCHUR_ALIGNMENT_SOURCE
