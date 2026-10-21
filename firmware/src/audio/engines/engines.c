/* SPDX-License-Identifier: GPL-3.0-only */
/* Engine registry: conditional includes based on CONFIG_ENGINE_*.
 * Each engine provides a static const engine_t and is added to ENGINES[]. */

#ifdef CONFIG_ENGINE_NOISE
#include "eng_noise.c"
#endif

#ifdef CONFIG_ENGINE_STUB
#include "eng_stub.c"
#endif

/* registry: order = engine numbers stored in presets and projects */
static const engine_t *const ENGINES[] = {
#ifdef CONFIG_ENGINE_NOISE
    &ENG_NOISE,
#endif
#ifdef CONFIG_ENGINE_STUB
    &ENG_STUB,
#endif
};

#undef NENGINES
#define NENGINES (sizeof(ENGINES) / sizeof(ENGINES[0]))
