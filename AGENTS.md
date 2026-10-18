# Starlight visualisation

- Write original C++17 code for Kodi 21.3 and GLES 2.0 or newer.
- Read only official Kodi headers and docs for the add-on API.
- Do not read other visualisation add-ons, Arctic Fuse, Starlight AF3, TMDb Helper, or jurialmunkey modules.
- Use only Living Room `192.168.0.31` for device tests. Never access Basement `192.168.0.32`.

## Source

- `src/Spectrum.cpp`: bounded PCM windows, channel power, logarithmic frequency bands.
- `src/Starlight.cpp`: Kodi instance, GLES shaders, rounded bars, state restoration.
- `addon.xml`: package metadata; CMake installs this file unchanged.
- `cmake/coreelec-arm.cmake`: CoreELEC toolchain file; set `CE_TOOLCHAIN` for another build tree.
- `tools/stage_armv7.sh`: builds the ARM module and stages the installable add-on into an `armv7` checkout.
- `resources/icon.png`: approved icon from the clean-room Starlight skin.
- `.githooks/publish_check.py`: verbatim shared validator from `kodi.addons/tools/publish_check.py`.
- `.githooks/pre-push`: shared Starlight hook. Enable with `git config core.hooksPath .githooks`.
- Follow `kodi.addons/VERSIONING.md` for future releases. Change the version and news together.
- License: GPL-2.0-or-later (`LICENSE.txt`), as required by the GPL Kodi dev-kit.

## Geometry and cost

- Skin control: `(600,22,720,720)` in the 1920×1080 layout.
- Centre: `(960,382)`. Cover diameter: 320 px. Bar inner radius: 180 px.
- Draw 160 rounded bars. Bar length spans 5–105 px. Glow extends 9 px.
- Leave the centre transparent. Never clear the framebuffer.
- The skin owns the cover, halo, paused ring, and `Player.Visualiser.Disabled` setting.
- Kodi supplies interleaved float PCM and the control viewport.
- Analyse 1024 frames per window. Combine channel power after each FFT to avoid stereo phase cancellation.
- Precompute FFT roots, window coefficients, frequency bands, and bar angles.
- Use one draw call. Keep allocations outside the audio and frame callbacks.
- Request about 30 spectrum refreshes per second. Kodi may redraw for other controls.
- Keep extra Kodi CPU below 15 percentage points and below 15% relative to the same music screen with bars disabled.

## Host checks

Run from this repository:

```sh
python3 tests/run.py
```

The runner creates a temporary host CMake build. CTest checks the documented audio failure cases.
Run the binary API check on the target architecture before Kodi opens a new module:

```sh
ssh root@192.168.0.31 python3 - \
  /storage/.kodi/addons/visualization.starlight/visualization.starlight.so < tests/binary_api.py
```

The API check calls exported functions without creating a Kodi instance.
Keep `ADDON_INSTANCE_VERSION_VISUALIZATION_USED` in the CMake compile definitions.
The dev-kit otherwise reports `0.0.0` for the active visualisation API.

For an enduring CTest report:

```sh
cmake -S . -B /tmp/starlight-visualization/host -DSTARLIGHT_BUILD_ADDON=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/starlight-visualization/host -j2
ctest --test-dir /tmp/starlight-visualization/host --output-on-failure
```

## CoreELEC ARM build and release

Use the existing CoreELEC toolchain. Do not rebuild Kodi or the firmware.
Keep all build output outside `/home/user/p3i`.
The toolchain uses a 32-bit hard-float ABI despite the `armv8a` prefix and the box's aarch64 kernel.
The source compiles for ARMv7 NEON. CoreELEC startup objects carry ARMv8 attributes.
Validate the resulting package on the target CoreELEC image. Generic ARMv7 distributions remain unverified.

`main` holds the source. The `armv7` branch holds the installable add-on (`addon.xml`, `LICENSE.txt`, `visualization.starlight.so`, `resources/`) that `kodi.addons` publishes.
The builder cannot compile C++, so every release stages a fresh binary on `armv7`:

```sh
git worktree add ../visualization.starlight-armv7 armv7
export PROJECT=Amlogic-ce DEVICE=Amlogic-ng ARCH=arm
tools/stage_armv7.sh ../visualization.starlight-armv7
cd ../visualization.starlight-armv7
python3 -m unittest discover -s tests
git commit -am "Release <version> from <source sha>"
```

Push `main` first, then `armv7`. Bump the version and news on `main`; the stage script copies `addon.xml` to `armv7`.

## Device verification

- Use only Living Room `192.168.0.31`.
- Record `musicplayer.visualisation`, `debug.showloginfo`, and `Player.Visualiser.Disabled` before changes.
- Copy the staged add-on folder into `/storage/.kodi/addons/`. Restart Kodi once for discovery.
- Enable `visualization.starlight` through `Addons.SetAddonEnabled`. Select the add-on through `Settings.SetSettingValue`.
- Play an existing Spotify or local queue. Never change Spotify account data.
- Open `visualisation`. Capture `/dev/fb0` through `/tmp/bisect/fbcap.sh`.
- Check several active frames, pause, resume, disabled bars, track changes, stop, and Home.
- Compare the 180 px inner radius and gold-to-frost bars with rounds 36–47.
- Compare 30-second Kodi CPU samples with bars enabled and disabled. Save the top output and computed CPU values.
- Restore the original visualisation unless the original value was empty or a skin-replaced default.
- Restore the skin toggle. Stop playback. Disable debug logs. Return to Starlight Home.
- Save evidence under `/tmp/starlight-visualization/` and report observed results separately from unverified assumptions.
