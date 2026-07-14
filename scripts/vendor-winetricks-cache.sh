#!/usr/bin/env bash
# Populate vendor/winetricks-cache/ with the redists setup-prefix.sh needs (corefonts, vcrun2022,
# mfc42) so prefix creation is offline. Downloads via the bundled winetricks; touches no prefix.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
dest="$root/vendor/winetricks-cache"
mkdir -p "$dest"

tmpc="$(mktemp -d)"
export XDG_CACHE_HOME="$tmpc"
# let winetricks download each verb's files into the cache
for v in corefonts vcrun2022 mfc42; do
    echo "== fetching $v =="
    bash "$root/vendor/winetricks" --no-clean -k "$v" >/dev/null 2>&1 || true
done
# copy whatever landed in the cache for our verbs
for v in corefonts vcrun2022 mfc42; do
    [ -d "$tmpc/winetricks/$v" ] && cp -a "$tmpc/winetricks/$v" "$dest/" && echo "vendored: $v"
done
rm -rf "$tmpc"
echo "cache now: $(du -sh "$dest" | cut -f1)"
