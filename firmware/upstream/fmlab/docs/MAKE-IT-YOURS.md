# Make it yours

FM Lab is GPL software and meant to be built on. This is how a copy becomes your firmware, gets released, and gets into the
[FM-1 Firmware Hub](https://fm1.designburgapps.com) next to the others.

## 1. Copy

On GitHub: **Use this template** (or fork). Then build once before changing anything, so you know your setup
works: `./build.sh` and `tests/run_host.sh` (see the [README](../README.md)).

## 2. Name it

- `fmlab.c` is yours: rename it if you like (and the one line in `tools/build.py` that compiles it).
- At its top, before `#include "platform.c"`:

  ```c
  #define APP_NAME "MYSYNTH"        /* the rescue and the crash screen say this */
  ```

- The package file names (`fmlab.fwsc`) are in `tools/build.py`: search for `fmlab`.
- Make your own logo; `assets/logo.svg` is FM Lab's. (Dinghy's is not in this tree: it carries the
  designburg signet, which is not under the GPL. See [LICENSING.md](../LICENSING.md).)

## 3. Your package identity

Every FM-1 package carries an identity like `FM-1_990`. The FM-1 reports the one it runs, and installers and the
hub use it to tell firmwares apart: two firmwares with the same identity look like one.

- FM Lab is `FM-1_97N` (N = the minor version). **Pick your own** in `tools/build.py` (`PRODUCT` and the
  `--release` line under it).
- Keep the form `FM-1_9` + two digits if you can: the web installers then also finish an install that was cut
  off halfway (they resume only the update loaders they know, `FM-1_9NN`).
- Taken so far: `900` SLOOP, `91N` Felucca, `93N` sloopDX, `97N` FM Lab, `98N` (a Doom port), `99N` Dinghy, and others
  in the hub's list. There is no registry yet: look at the hub, and ask there if you are unsure.

## 4. Release

```sh
./build.sh --release 1.0.0      # -> build/fmlab-1.0.0.fwsc, identity FM-1_970, "V1.0.0" in GLOBAL's top corner
```

Without `--release` a build says `DEV` there instead, so a test build on the device is never mistaken for a
release. Add the release to `CHANGELOG.md`. On GitHub: a release with a tag (`v1.0.0`) and the `.fwsc` attached.
Say what changed in its notes (the changelog entry will do): the hub links them.

## 5. Into the hub

The [FM-1 Firmware Hub](https://fm1.designburgapps.com) installs open-source FM-1 firmwares with one click,
and checks every night for new releases. To be listed:

- the source is public and GPL-3.0 (FM Lab already is: keep it so);
- the releases are on GitHub, each with its `.fwsc`;
- a screenshot of your firmware's screen: `tests/run_host.sh` makes one (`build/host/*.png`), or a photo;
- you ask: the hub's *"Made an open-source firmware?"* card says how.

After that every release you make shows up there by itself.

## 6. Keep the credits

Your firmware stands on Felucca (Leo Kuroshita, Hügelton Instruments), SLOOP (isod89) and Dinghy
(zvenson), and its FM engine on Dexed (Google, Pascal Gauthier) by way of Sven Trogus. The GPL asks you to
keep their copyright lines and to give your users your source; it also is just fair. Keep `LICENSING.md` and
the headers, and add yourself.

## Ideas to start from

- Draw the current algorithm: `DX_ALG[32][6]` in `dx7_core.c` has the whole routing, two bits per operator.
- Draw an operator's envelope from its four rates and four levels, with a dot where the note is now.
- Mute and solo an operator: zero its output level in the copy you publish, and put it back on release.
- A and B: keep a second 156-byte voice and swap it in, to hear one edit against another.
- `.syx` import, so a DX7 bank you own loads straight in. `platform.c` eats SysEx for the updater, so this
  needs a hook in `usb.c` first.
- Look at how zp12 or sloopDX do it: same platform, much bigger apps.
