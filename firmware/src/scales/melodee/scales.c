/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee scales stub: 70 scales incl. 54 microtonal, concert pitch 400-480Hz.
 * TODO: port from upstream melodee (scales.h, microtonal.h, tuning.h,
 * scale_picker.c, note_tools.c). */

#define NSCALE 70u
#define NSCALE_12TET 16u

static const char *scale_name(uint32_t idx)
{
    static const char *names[] = {
        "Chromatic", "Major", "Minor", "Dorian", "Mixolydian",
        "Phrygian", "Lydian", "Locrian", "Harmonic Min", "Melodic Min",
        "Pentatonic", "Blues", "Whole Tone", "Diminished", "Augmented",
        "Hungarian"
    };
    if (idx < NSCALE_12TET)
        return names[idx];
    return "Microtonal";
}

static uint32_t scale_mask(uint32_t idx)
{
    static const uint16_t masks[] = {
        0x0FFF, 0x0AB5, 0x05AD, 0x06AD, 0x0AB3,
        0x059B, 0x0AD5, 0x056B, 0x05BD, 0x06B5,
        0x0A89, 0x0A93, 0x0555, 0x0969, 0x0111,
        0x0BB3
    };
    if (idx < NSCALE_12TET)
        return masks[idx];
    return 0x0FFF;
}

static uint32_t scale_root;
static uint32_t scale_index;
static int32_t concert_pitch_hz = 440;

static uint32_t scale_quantize(uint32_t note, uint32_t root, uint32_t mask)
{
    uint32_t rel = (note - root) % 12u;
    if (mask & (1u << rel))
        return note;
    for (uint32_t d = 1; d < 6; d++) {
        if (mask & (1u << ((rel + d) % 12u)))
            return note + d;
        if (mask & (1u << ((rel + 12u - d) % 12u)))
            return note - d;
    }
    return note;
}

static void scales_init(void)
{
    scale_root = 0;
    scale_index = 0;
    concert_pitch_hz = 440;
}
