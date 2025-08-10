#pragma once
// Workaround for glibc C23 math functions (sinpi/cospi/sinpif/cospif)
// conflicting with CUDA 12.x math_functions.h declarations.
// We pre-include <math.h> with the symbols temporarily renamed so that
// glibc's prototypes do not collide with CUDA's. We do not use these
// glibc symbols in this project.

// Rename before including glibc <math.h>
#define sinpi  glibc_hidden_sinpi
#define cospi  glibc_hidden_cospi
#define sinpif glibc_hidden_sinpif
#define cospif glibc_hidden_cospif

#include <math.h>

// Restore original names
#undef sinpi
#undef cospi
#undef sinpif
#undef cospif
