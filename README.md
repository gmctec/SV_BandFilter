# SV_BandFilter

A LADSPA audio plugin for [Sonic Visualiser](https://github.com/sonic-visualiser/sonic-visualiser)
that **keeps** or **removes** a chosen frequency range.

| Control | Meaning |
|---|---|
| Low frequency (Hz) | Lower edge of the band, 20 Hz - 20 kHz (default ~112 Hz) |
| High frequency (Hz) | Upper edge of the band, 20 Hz - 20 kHz (default ~3557 Hz) |
| Remove band | Off: keep only the band (band-pass). On: remove the band, keep everything else (band-stop). |

No Sonic Visualiser code is changed. Sonic Visualiser already finds LADSPA
plugins on its plugin path, applies them as transforms, and lets you preview
them while you adjust the controls.

## Install

Download the plugin file for your system from the latest successful
[build run](https://github.com/gmctec/SV_BandFilter/actions) (Artifacts
section at the bottom of the run page), or build it yourself (below).

**Windows:** copy `sv_bandfilter.dll` into `C:\Program Files\LADSPA Plugins`
(create the folder if it doesn't exist). Sonic Visualiser also searches
`C:\Program Files\Audacity\Plug-Ins`. To use another folder, set the
`LADSPA_PATH` environment variable to it.

**macOS:** download **SV_BandFilter-macos-universal** from the latest
`package-macos` run and open the `.pkg` installer inside. It installs the
plugin for all users in `/Library/Audio/Plug-Ins/LADSPA` and runs on both
Apple Silicon and Intel Macs (macOS 11 or later). The installer is unsigned,
so macOS blocks it the first time: open **System Settings → Privacy &
Security** and click **Open Anyway**. Or skip the installer and copy
`sv_bandfilter.dylib` from the `.zip` into `~/Library/Audio/Plug-Ins/LADSPA`;
if Gatekeeper then blocks it, run
`xattr -d com.apple.quarantine sv_bandfilter.dylib`.
See [PLATFORM_DIFFERENCES.md](PLATFORM_DIFFERENCES.md) for how the Windows
and macOS builds differ.

**Linux:** copy `sv_bandfilter.so` into `~/.ladspa` or `/usr/local/lib/ladspa`.

Restart Sonic Visualiser after installing.

## Use in Sonic Visualiser

1. Open an audio file.
2. Open the **Transform** menu and find **Band Filter (keep or remove a
   frequency range)**. It is listed with the effects; *Transform → Find a
   Transform...* and typing "band" also finds it.
3. Set the low and high frequencies and the **Remove band** switch. You can
   play the audio while the dialog is open to hear the result.
4. Click OK. The filtered audio appears as a new layer; the original is
   unchanged.

Apply the transform again with other settings to add more filtered
versions to the same session. Each layer has its own play/mute/solo
control, so you can switch between the original and each filtered
version while listening. All of them are saved with the session.

## How it works

- 8th-order Butterworth prototype, transformed to band-pass or band-stop,
  then converted to digital (bilinear transform, pre-warped edges).
  8 biquad sections, double-precision state.
- Band edges are the -3 dB points. Beyond them the response falls by
  roughly 48 dB per octave. Inside a removed band, attenuation is over
  80 dB, and about 150 dB at the band centre.
- No added delay (latency 0). As with any IIR filter, phase shifts near the
  band edges.
- Values are clamped to 1 Hz - 45% of the sample rate. A reversed range is
  swapped. A band narrower than 1% is widened to 1%.
- Mono plugin. Sonic Visualiser runs one instance per channel, so stereo
  and multichannel files work.

Code layout:

| Path | Role |
|---|---|
| `src/bandfilter_core.c/.h` | Filter design and processing. Plain C, no plugin API. |
| `src/bandfilter_ladspa.c` | Thin LADSPA wrapper around the core. |
| `tests/test_response.c` | Drives the plugin as a host would; checks gain in dB at many frequencies, band widths and sample rates. |
| `windows/sv_bandfilter.def` | Windows export list (`ladspa_descriptor`). |

The core has no dependency on LADSPA, so other plugin formats (LV2, CLAP,
VST3) can be added as new wrappers without touching the filter maths.

## Build

Requires a C compiler, [Meson](https://mesonbuild.com) and Ninja
(`python -m pip install meson ninja`).

```
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

On Windows, run these from a "x64 Native Tools Command Prompt for VS"
so the MSVC compiler is on the path.

## Licence

GPL-2.0-or-later, the same licence as Sonic Visualiser. Copyright (C) 2026
gmctec. Free to use, modify and share; keep the copyright notices, and
please credit "SV_BandFilter by gmctec". See `LICENSE` and `NOTICE`.
