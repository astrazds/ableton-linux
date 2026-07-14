#!/usr/bin/env bash
# Remove what install.sh added. The Wine prefix (~/.wine-ableton) is kept unless you pass --prefix.
set -euo pipefail
OPT="$HOME/.local/opt/wine-d2d1-nspa-11.11"
BIN="$HOME/.local/bin/ableton-live"
APPS="$HOME/.local/share/applications"

rm -rf "$OPT"        && echo "removed $OPT"
for d in "$OPT"-rollback-* "$OPT".failed-*; do
    [ -e "$d" ] || continue     # unmatched glob stays literal; skip, don't abort
    rm -rf "$d" && echo "removed $d"
done
rm -f  "$BIN"        && echo "removed $BIN"
rm -f  "$BIN".rollback-*
rm -rf "$HOME/.local/share/ableton-wine" && echo "removed ~/.local/share/ableton-wine"
rm -f  "$APPS/ableton-live.desktop" "$APPS/wine-protocol-ableton.desktop"
update-desktop-database "$APPS" 2>/dev/null || true
echo "removed desktop entries"

if [ "${1:-}" = "--prefix" ]; then
    pfx="${ABLETON_WINEPREFIX:-$HOME/.wine-ableton}"
    read -rp "Also delete $pfx? This removes your Live installation AND its authorization. [y/N] " a
    case "$a" in
        [yY]|[yY][eE][sS]) rm -rf "$pfx" && echo "removed $pfx" ;;
        *) echo "kept $pfx" ;;
    esac
fi
echo "done."
