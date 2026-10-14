Melodee 1.3.6 resets unsupported user preset banks at startup instead of leaving
them as OLD FORMAT. This intentionally discards unreadable preset-bank contents.
Keep a backup before upgrading if those bytes may be needed by another firmware.

- The four native FM6 and eight native CZ banks initialize to the same empty
  defaults as a fresh installation when their format is unsupported or damaged.
  Factory sounds remain available separately.
- Unsupported general user-preset banks reset to empty. In a readable bank,
  only unsupported entries are cleared; compatible presets survive.
- Startup shows progress before writes. Each reset uses the existing A/B commit:
  failure retains the previous committed copy and an error remains visible;
  restart retries unfinished resets. Successful resets are not repeated.
- Projects retain OLD FORMAT protection. Compatible settings and preset banks
  are kept. The firmware does not change the system PLL.

The 1.3.5 boot stub, early display and first-boot settings cleanup remain included.
On first installation from stock or Melodee through 1.3.4, interruption before
the boot sector is complete can still require hardware recovery. Keep power
connected throughout installation and startup maintenance.

Automated checks cover bank validation, interrupted resets, compatible presets,
project preservation, both firmware variants and the existing regression suite.
Physical stock-to-Melodee startup and recovery validation remains pending.
