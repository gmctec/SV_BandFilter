# Platform differences: Windows and macOS

**The source code is identical on every platform.** Windows, macOS and
Linux builds compile the same files with no platform `#ifdef`s:

| File | Platform-specific code |
|---|---|
| `src/bandfilter_core.c` / `.h` | None |
| `src/bandfilter_ladspa.c` | None |
| `include/ladspa.h` | None (unchanged upstream LADSPA header) |
| `tests/test_response.c` | None |

All differences are in the **build, packaging and installation** layer,
listed below.

## Build

| Aspect | Windows | macOS |
|---|---|---|
| Compiler | MSVC (Visual Studio, `cl.exe`) | Apple Clang (Xcode command-line tools) |
| Output file | `sv_bandfilter.dll` | `sv_bandfilter.dylib` |
| CPU architectures | x86-64 | Universal: arm64 (Apple Silicon) + x86-64 (Intel) in one file |
| Minimum OS | Windows 10 (MSVC default) | macOS 11 Big Sur (`MACOSX_DEPLOYMENT_TARGET=11.0`) |
| Exporting `ladspa_descriptor` | Export list in `windows/sv_bandfilter.def`, passed to the linker by `meson.build`. Windows DLLs export nothing unless told to. | No extra file. macOS exports public functions by default (`gnu_symbol_visibility: 'default'` in `meson.build`). |
| Runtime libraries | `KERNEL32.dll` and the C runtime only | `libSystem` only (part of macOS) |
| Build command | `meson setup build` / `meson compile -C build`, run from a "x64 Native Tools Command Prompt for VS" | `packaging/macos/build_pkg.sh` (adds the universal-binary flags, signing and packaging) |
| CI workflow | `.github/workflows/build.yml` (windows-latest) | `.github/workflows/build.yml` (native arm64 build) and `.github/workflows/package-macos.yml` (universal installer) |

Two portability details sit in the shared source, valid on every platform
rather than special-cased:

- `M_PI` is defined in the source if the compiler's `<math.h>` lacks it
  (MSVC does not define it by default).
- Complex arithmetic uses a small local struct instead of C99
  `<complex.h>`, which MSVC does not support.

## Code signing

| Aspect | Windows | macOS |
|---|---|---|
| Signature on the plugin | None | Ad-hoc signature (`codesign --sign -`). Apple Silicon will not load code with no signature at all. |
| Why that is enough | Windows does not check DLL signatures for this | Sonic Visualiser's macOS build is signed with the `disable-library-validation` entitlement (`deploy/macos/Entitlements.plist` in its repo), so it may load plugins signed by anyone, including ad-hoc. |
| Installer signature | Not applicable (no installer; copy the DLL) | The `.pkg` is unsigned and not notarized, so Gatekeeper warns on first open. See *Install* below. |

## Install

| Aspect | Windows | macOS |
|---|---|---|
| Package format | `sv_bandfilter.dll` in a zip | `SV_BandFilter-<version>-macos-universal.pkg` installer, or a `.zip` with the plain `.dylib` |
| Install folder | `C:\Program Files\LADSPA Plugins` (or `C:\Program Files\Audacity\Plug-Ins`) | `.pkg`: `/Library/Audio/Plug-Ins/LADSPA` (all users). Manual: `~/Library/Audio/Plug-Ins/LADSPA` (your account only) |
| Admin rights | Needed to copy into Program Files | Installer asks for an administrator password (system folder); not needed for the per-user folder |
| Override folder | `LADSPA_PATH` environment variable | `LADSPA_PATH` environment variable |
| First-run security prompt | None for the DLL itself | Unsigned `.pkg`: double-clicking is blocked once. Open **System Settings → Privacy & Security**, scroll to the message about the package, click **Open Anyway**. Alternatively install from Terminal: `sudo installer -pkg SV_BandFilter-*.pkg -target /` |
| Uninstall | Delete the DLL | `sudo rm /Library/Audio/Plug-Ins/LADSPA/sv_bandfilter.dylib` and `sudo pkgutil --forget io.github.gmctec.sv-bandfilter` |

## Behaviour

Identical. Sonic Visualiser builds the same dialog (knob and number box
for each frequency, a checkbox for *Remove band*) on both platforms, and
the filter produces the same output, since it is the same code compiled
with IEEE double-precision arithmetic on both.
