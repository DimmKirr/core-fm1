/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Deterministic test input, not a port of the hardware RNG implementation. */
#pragma once
namespace marbles {
class RandomStream {
 public:
  uint16_t words[32] = {}; unsigned at = 0;
  float GetFloat() { return words[at++ & 31u] / 65536.0f; }
};
}
