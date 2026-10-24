#!/usr/bin/env bash
# Local Linux LÖVE build; patch a project-local copy, leaving upstream untouched.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
repo_root=$PWD
source_dir=${LOVE_SOURCE_DIR:-/home/aaron-ma/love}
expected_revision=fc42127510659651ee692ab820992fd978fae92c
build_root="$repo_root/artifacts/love12"
download_deps=false
configure_only=false
while (($#)); do
  case "$1" in
    --source) source_dir=${2:?Missing --source path}; shift 2 ;;
    --download-deps) download_deps=true; shift ;;
    --configure-only) configure_only=true; shift ;;
    *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
  esac
done
source_dir=$(realpath "$source_dir")
for tool in cmake ninja pkg-config git gcc c++ sha256sum patch tar; do
  command -v "$tool" >/dev/null || { printf 'Missing build tool: %s\n' "$tool" >&2; exit 1; }
done
[[ -f "$source_dir/CMakeLists.txt" ]] || { printf 'Missing LÖVE source: %s\n' "$source_dir" >&2; exit 1; }
revision=$(git -C "$source_dir" rev-parse HEAD)
[[ "$revision" == "$expected_revision" ]] || { printf 'Expected pinned LÖVE revision %s; found %s\n' "$expected_revision" "$revision" >&2; exit 1; }
[[ -z "$(git -C "$source_dir" status --porcelain --untracked-files=no)" ]] || { printf 'LÖVE source has tracked changes; use a clean pinned checkout.\n' >&2; exit 1; }
mkdir -p "$build_root/deps" "$build_root/debs" "$build_root/install"
if "$download_deps"; then
  for tool in apt-get dpkg-deb dpkg-architecture; do command -v "$tool" >/dev/null || { printf 'Missing Debian dependency tool: %s\n' "$tool" >&2; exit 1; }; done
  # Both headers and matching runtime libraries keep extracted .so symlinks valid.
  (
    cd "$build_root/debs"
    apt-get -o Acquire::Retries=0 -o Acquire::http::Timeout=15 download \
      libharfbuzz-dev libharfbuzz0b libopenal-dev libopenal1 \
      libmodplug-dev libmodplug1 libtheora-dev libtheora0 \
      libvorbis-dev libvorbis0a libvorbisenc2 libvorbisfile3 libogg-dev libogg0
    for package in ./*.deb; do dpkg-deb -x "$package" "$build_root/deps"; done
  )
fi
# SDL3, FreeType and LuaJIT development files must already be available.
pkg-config --atleast-version=3.1.3 sdl3 || { printf 'SDL3 >=3.1.3 development files are required.\n' >&2; exit 1; }
pkg-config --exists freetype2 luajit || { printf 'FreeType and LuaJIT development files are required.\n' >&2; exit 1; }
prefix_path="$build_root/deps/usr;/usr/local"
patch_file="$repo_root/tools/patches/love12-fastsim.patch"
patch_hash=$(sha256sum "$patch_file" | cut -d ' ' -f 1)
patched_source="$build_root/source"
if [[ ! -f "$patched_source/.fastsim-patch" ]] || [[ "$(cat "$patched_source/.fastsim-patch")" != "$revision:$patch_hash" ]]; then
  rm -rf -- "$patched_source"
  mkdir -p "$patched_source"
  git -C "$source_dir" archive HEAD | tar -x -C "$patched_source"
  # Upstream mixes CRLF/LF; the checked-in patch uses normalized LF.
  while IFS= read -r file; do sed -i 's/\r$//' "$patched_source/$file"; done < <(sed -n 's|^--- a/||p' "$patch_file")
  patch --batch --forward -p1 -d "$patched_source" < "$patch_file"
  printf '%s:%s' "$revision" "$patch_hash" > "$patched_source/.fastsim-patch"
fi
cmake -S "$patched_source" -B "$build_root/build-fastsim" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DLOVE_JIT=ON \
  -DCMAKE_INSTALL_PREFIX="$build_root/install" \
  "-DCMAKE_PREFIX_PATH=$prefix_path" \
  "-DCMAKE_INSTALL_RPATH=$build_root/install/lib;$build_root/deps/usr/lib/$(gcc -dumpmachine)" \
  2>&1 | tee "$build_root/configure.log" || {
    printf '\nConfiguration failed. See artifacts/love12/configure.log.\n' >&2
    printf 'For missing HarfBuzz/audio/codec headers, run: ./tools/build_love12.sh --download-deps\n' >&2
    exit 1
  }
printf 'source=%s\nrevision=%s\nLuaJIT=ON\nVulkan=enabled by upstream Linux config.h\nfastsimPatchSha256=%s\n' "$source_dir" "$revision" "$patch_hash" > "$build_root/build-info.txt"
sha256sum "$source_dir/CMakeLists.txt" "$source_dir/src/common/config.h" >> "$build_root/build-info.txt"
if "$configure_only"; then exit 0; fi
cmake --build "$build_root/build-fastsim" --parallel "${LOVE_BUILD_JOBS:-4}" 2>&1 | tee "$build_root/build.log"
cmake --install "$build_root/build-fastsim" 2>&1 | tee "$build_root/install.log"
"$build_root/install/bin/love" --version
printf '\nBuilt local runtime. Validate with:\n'
printf 'LOVE_BIN=./artifacts/love12/install/bin/love ./tools/run.sh --compute-smoke\n'
printf 'LOVE_BIN=./artifacts/love12/install/bin/love ./tools/run.sh --warlock-smoke\n'
