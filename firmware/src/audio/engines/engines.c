/* SPDX-License-Identifier: GPL-3.0-only */
/* Engine registry: conditional includes based on CONFIG_ENGINE_*.
 * Each engine provides a static const engine_t and is added to ENGINES[]. */

#ifdef CONFIG_ENGINE_ANALOG
#include "upstream/felucca-engines/src/eng_analog.c"
#endif

#ifdef CONFIG_ENGINE_NOISE
#include "upstream/felucca-engines/src/eng_noise.c"
#endif

#ifdef CONFIG_ENGINE_LOFI
#include "upstream/felucca-engines/src/eng_lofi.c"
#endif

#ifdef CONFIG_ENGINE_TRIO
#include "upstream/felucca-engines/src/eng_trio.c"
#endif

#ifdef CONFIG_ENGINE_FM6
#if defined(CONFIG_ENGINE_FM6_FMLAB)
#include "fm6/fm6_fmlab.c"
#elif defined(CONFIG_ENGINE_FM6_FELUCCA)
#include "fm6/fm6_felucca.c"
#elif defined(CONFIG_ENGINE_FM6_MELODEE)
#include "fm6/fm6_melodee.c"
#endif
#endif

#ifdef CONFIG_ENGINE_STUB
#include "eng_stub.c"
#endif

/* registry: order = engine numbers stored in presets and projects */
static const engine_t *const ENGINES[] = {
#ifdef CONFIG_ENGINE_ANALOG
    &ENG_ANALOG,
#endif
#ifdef CONFIG_ENGINE_NOISE
    &ENG_NOISE,
#endif
#ifdef CONFIG_ENGINE_LOFI
    &ENG_LOFI,
#endif
#ifdef CONFIG_ENGINE_TRIO
    &ENG_TRIO,
#endif
#ifdef CONFIG_ENGINE_FM6
    &ENG_FM6,
#endif
#ifdef CONFIG_ENGINE_STUB
    &ENG_STUB,
#endif
};

#undef NENGINES
#define NENGINES (sizeof(ENGINES) / sizeof(ENGINES[0]))
