#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# SV_BandFilter - Copyright (C) 2026 gmctec
#
# Builds the plugin as a universal macOS binary (Apple Silicon + Intel),
# runs the test suite, and wraps the result in an installer package.
# Uses the same source files as every other platform; nothing here
# changes or patches source code.
#
# Run on a Mac with Xcode command-line tools, Meson and Ninja installed:
#   packaging/macos/build_pkg.sh
#
# Output (in dist/):
#   SV_BandFilter-<version>-macos-universal.pkg  installer
#   SV_BandFilter-<version>-macos-universal.zip  plain .dylib + docs

set -euo pipefail

cd "$(dirname "$0")/../.."
repo_root="$(pwd)"

: "${MACOSX_DEPLOYMENT_TARGET:=11.0}"   # oldest macOS supported
export MACOSX_DEPLOYMENT_TARGET

archs="-arch arm64 -arch x86_64"
build_dir="build-macos-universal"
dist_dir="dist"
plugin="sv_bandfilter.dylib"
pkg_id="io.github.gmctec.sv-bandfilter"
install_dir="/Library/Audio/Plug-Ins/LADSPA"

echo "== Configure (universal, macOS ${MACOSX_DEPLOYMENT_TARGET}+)"
rm -rf "$build_dir"
meson setup "$build_dir" --buildtype release \
    -Dc_args="$archs -mmacosx-version-min=${MACOSX_DEPLOYMENT_TARGET}" \
    -Dc_link_args="$archs -mmacosx-version-min=${MACOSX_DEPLOYMENT_TARGET}"

echo "== Build"
meson compile -C "$build_dir"

echo "== Check architectures"
lipo -info "$build_dir/$plugin"
for a in arm64 x86_64; do
    lipo "$build_dir/$plugin" -verify_arch "$a" \
        || { echo "missing $a slice"; exit 1; }
done

echo "== Test (native slice)"
meson test -C "$build_dir" --print-errorlogs

echo "== Test (Intel slice, if Rosetta is available)"
if arch -x86_64 /usr/bin/true 2>/dev/null; then
    arch -x86_64 "$build_dir/test_response" | tail -1
else
    echo "Rosetta not available here; Intel slice not run"
fi

echo "== Ad-hoc sign"
# Apple Silicon refuses to load code with no signature at all. An ad-hoc
# signature is enough for Sonic Visualiser, whose signing entitlements
# disable library validation so it can load third-party plugins.
codesign --force --sign - "$build_dir/$plugin"
codesign --verify --verbose "$build_dir/$plugin"

version="$(meson introspect "$build_dir" --projectinfo \
    | sed -n 's/.*"version": *"\([^"]*\)".*/\1/p')"
[ -n "$version" ] || { echo "could not read version"; exit 1; }
base="SV_BandFilter-${version}-macos-universal"

echo "== Package $base"
rm -rf "$dist_dir"
mkdir -p "$dist_dir"

pkg_root="$(mktemp -d)"
trap 'rm -rf "$pkg_root"' EXIT
mkdir -p "$pkg_root$install_dir"
cp "$build_dir/$plugin" "$pkg_root$install_dir/"
chmod 644 "$pkg_root$install_dir/$plugin"

pkgbuild \
    --root "$pkg_root" \
    --identifier "$pkg_id" \
    --version "$version" \
    --install-location / \
    --ownership recommended \
    "$dist_dir/$base.pkg"

zip_stage="$(mktemp -d)"
mkdir -p "$zip_stage/$base"
cp "$build_dir/$plugin" LICENSE NOTICE README.md PLATFORM_DIFFERENCES.md \
    "$zip_stage/$base/"
( cd "$zip_stage" && zip -qr "$repo_root/$dist_dir/$base.zip" "$base" )
rm -rf "$zip_stage"

echo "== Contents of $base.pkg"
pkgutil --payload-files "$dist_dir/$base.pkg"

( cd "$dist_dir" && shasum -a 256 ./* | tee SHA256SUMS.txt )
echo "== Done: $dist_dir/"
