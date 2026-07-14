#!/bin/sh
# Ableton-on-Wine single-file installer (self-extracting).
# Usage:  sh ableton-wine-setup-@VERSION@.run [options]
# Options:
#   --runtime-only   install the patched Wine only; skip making the Wine prefix
#   --no-launch      never run the Ableton installer (zip/exe) automatically
#   --extract DIR    unpack this installer's files into DIR and exit
#   --uninstall      remove the installed Wine, launcher, and menu entries
#   --help           this text
# Environment:
#   ABLETON_DPI_MODE  auto|preserve|100|fractional (overrides scale auto-detection)
# Everything after the marker line is a tar archive; this header never changes it.
[ -n "${BASH_VERSION:-}" ] || exec bash "$0" "$@"
set -euo pipefail

VERSION="@VERSION@"
PAYLOAD_SHA="@PAYLOAD_SHA@"
RUNTIME_NAME="wine-d2d1-nspa-11.11"

self="$(readlink -f -- "$0")"
stick_dir="$(dirname -- "$self")"

say()  { printf '%s\n' "$*"; }
fail() { printf '!! %s\n' "$*" >&2; exit 1; }

mode=install
do_launch=1
extract_dir=""
while [ $# -gt 0 ]; do
    case "$1" in
        --help|-h)      head -12 "$self" | sed -n '2,12{s/^# \{0,1\}//;p}'; exit 0 ;;
        --runtime-only) mode=runtime ;;
        --no-launch)    do_launch=0 ;;
        --uninstall)    mode=uninstall ;;
        --extract)      mode=extract; extract_dir="${2:?--extract needs a directory}"; shift ;;
        *)              fail "unknown option: $1 (try --help)" ;;
    esac
    shift
done

say "== Ableton-on-Wine installer $VERSION =="

# --- find the Ableton payload next to this file, up front ---------------------
find_live_payload() {
    live_exe=""; live_zip=""
    local f base
    for f in "$stick_dir"/*; do
        [ -f "$f" ] || continue
        base="$(basename "$f" | tr '[:upper:]' '[:lower:]')"
        case "$base" in
            ableton_live_suite_12*.zip|ableton_live*12*.zip) live_zip="$f" ;;
            *ableton*.exe|*live*.exe)                        live_exe="$f" ;;
        esac
    done
}
manual_install=1
if [ "$mode" = install ] && [ "$do_launch" -eq 1 ]; then
    find_live_payload
    if [ -z "$live_exe$live_zip" ] && [ -t 0 ]; then
        say ""
        say "No Ableton installer found next to this file"
        say "(looked for ableton_live_suite_12*.zip or an Ableton .exe in $stick_dir)."
        say "Put it here and press Enter. Or press Enter without it — the"
        say "manual install commands are printed at the end."
        printf '> '
        read -r _ || true
        find_live_payload
    fi
    if [ -n "$live_exe$live_zip" ]; then
        manual_install=0
        say "-- will install: $(basename "${live_exe:-$live_zip}")"
    fi
fi

# --- unpack the embedded kit ------------------------------------------------
workdir="$(mktemp -d "${TMPDIR:-/tmp}/ableton-setup.XXXXXX")"
cleanup() {
    rc=$?
    if [ "$rc" -eq 0 ]; then rm -rf "$workdir"
    else say "(kept $workdir for inspection — the failure details are above)"; fi
    exit "$rc"
}
trap cleanup EXIT

offset="$(awk '/^__PAYLOAD_BELOW__$/{print NR+1; exit}' "$self")"
[ -n "$offset" ] || fail "this installer file is incomplete — copy or download the .run file again"
say "-- checking the installer's own files and unpacking them"
tail -n +"$offset" "$self" > "$workdir/payload.tar"
actual="$(sha256sum "$workdir/payload.tar" | awk '{print $1}')"
[ "$actual" = "$PAYLOAD_SHA" ] || fail "this installer file failed its integrity check (it was probably damaged while copying) — copy the .run file again and retry"
kit="$workdir/kit"
mkdir -p "$kit"
tar -xf "$workdir/payload.tar" -C "$kit"
rm -f "$workdir/payload.tar"

if [ "$mode" = extract ]; then
    mkdir -p "$extract_dir"
    cp -a "$kit/." "$extract_dir/"
    say "OK: kit extracted to $extract_dir"
    exit 0
fi
if [ "$mode" = uninstall ]; then
    bash "$kit/scripts/uninstall.sh" "$@"
    exit 0
fi

# --- host checks -------------------------------------------------------------
say "-- checking this machine"
[ "$(uname -m)" = x86_64 ] || fail "this installer needs a 64-bit Intel/AMD machine (x86_64); this machine is $(uname -m)"
command -v tar  >/dev/null || fail "the 'tar' program is missing — install it with your package manager, then rerun"
command -v zstd >/dev/null || fail "the 'zstd' program is missing (package name: zstd) — install it, then rerun; it unpacks the Wine files"
glibc="$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+$' || true)"
if [ -n "$glibc" ]; then
    case "$glibc" in
        2.[0-2]?|2.3[0-4]) fail "glibc $glibc is too old (need 2.35+, i.e. a 2022-or-newer distribution)" ;;
    esac
    say "   glibc $glibc: ok"
fi
# no grep -q: under pipefail it SIGPIPEs ldconfig and falsely fires this warning
if ! ldconfig -p 2>/dev/null | grep 'libjack\.so\.0' >/dev/null; then
    say "   WARNING: one sound-system piece is missing: PipeWire's JACK library."
    say "   Everything will install fine, but Live will have NO SOUND until you"
    say "   add it (package: pipewire-jack). On the Steam Deck, run:"
    say "     sudo steamos-readonly disable"
    say "     sudo pacman-key --init && sudo pacman-key --populate archlinux holo"
    say "     sudo pacman -S pipewire-jack"
    say "     sudo steamos-readonly enable"
fi
# Bundled static cabextract covers machines that lack the host package.
if ! command -v cabextract >/dev/null; then
    say "   this machine has no 'cabextract' — using the copy bundled in this installer"
fi
export PATH="$kit/bin:$PATH"

# --- install the runtime ------------------------------------------------------
say "-- installing the patched Wine (goes to ~/.local/opt, touches nothing else)"
bash "$kit/scripts/install.sh"
[ "$mode" = runtime ] && { say "OK: the patched Wine is installed (--runtime-only: stopped before creating the Wine prefix)"; exit 0; }

# --- create the prefix --------------------------------------------------------
# Seed ABLETON_DPI_MODE from the detected display scale; the launcher re-detects on every start.
if [ -z "${ABLETON_DPI_MODE:-}" ]; then
    . "$kit/scripts/detect-scale.sh"
    scale="$(ableton_detect_scale)" || scale=""
    case "$scale" in
        1)    export ABLETON_DPI_MODE=100;        say "-- display scale: 100% (auto-detected)" ;;
        1.25) export ABLETON_DPI_MODE=fractional; say "-- display scale: 125% (auto-detected)" ;;
        *)
            if [ -d "$HOME/.wine-ableton" ]; then
                export ABLETON_DPI_MODE=preserve
                say "-- display scale: ${scale:-could not be detected}${scale:+ (only 100% and 125% are calibrated)}; keeping your existing display settings"
            else
                export ABLETON_DPI_MODE=100
                say "-- display scale: ${scale:-could not be detected}${scale:+ (only 100% and 125% are calibrated)}; starting the new prefix at 100%"
                say "   (the launcher re-checks your display on every start, so this corrects itself)"
            fi ;;
    esac
fi
say "-- creating the Wine prefix — Live's private 'C: drive' at ~/.wine-ableton"
say "   (fonts and runtime pieces install now; this takes a few minutes)"
bash "$kit/scripts/setup-prefix.sh"

# --- install Ableton Live from the stick ---------------------------------------
live_installed=0
if [ "$manual_install" -eq 0 ]; then
    if [ -z "$live_exe" ] && [ -n "$live_zip" ]; then
        say "-- unpacking $(basename "$live_zip")"
        unpack_dir="${XDG_CACHE_HOME:-$HOME/.cache}/ableton-wine-setup/live-installer"
        rm -rf "$unpack_dir"; mkdir -p "$unpack_dir"
        if command -v unzip >/dev/null;   then unzip -q "$live_zip" -d "$unpack_dir"
        elif command -v bsdtar >/dev/null; then bsdtar -xf "$live_zip" -C "$unpack_dir"
        elif command -v python3 >/dev/null; then python3 -m zipfile -e "$live_zip" "$unpack_dir"
        else say "!! no program available to unpack the zip (looked for unzip, bsdtar, python3) — manual steps will be printed at the end"; manual_install=1
        fi
        if [ "$manual_install" -eq 0 ]; then
            live_exe="$(find "$unpack_dir" -iname '*.exe' | head -1)"
            [ -n "$live_exe" ] || { say "!! that zip holds no installer (.exe) — is it the right download from ableton.com?"; manual_install=1; }
        fi
    fi
    if [ -n "$live_exe" ]; then
        say "-- starting the Ableton installer — from here just click through its window"
        # run from the installer's own directory so its relative payload lookups resolve
        if ( cd "$(dirname -- "$live_exe")" && \
                 WINEPREFIX="$HOME/.wine-ableton" \
                 "$HOME/.local/opt/$RUNTIME_NAME/bin/wine" \
                 "./$(basename -- "$live_exe")" ); then
            live_installed=1
        else
            say "!! the Ableton installer exited with an error — instructions below"
            manual_install=1
        fi
        WINEPREFIX="$HOME/.wine-ableton" \
            "$HOME/.local/opt/$RUNTIME_NAME/bin/wineserver" -w 2>/dev/null || true
        rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/ableton-wine-setup" 2>/dev/null || true
    fi
fi

say ""
say "================================================================"
if [ "$live_installed" -eq 1 ]; then
    say "Done — Ableton Live is installed."
else
    say "Done, except Ableton Live itself. To install it manually:"
    say "  1) unpack your ableton_live_suite_12*.zip:"
    say "       unzip /path/to/ableton_live_suite_12*.zip -d ~/live-installer"
    say "       (no unzip? try: bsdtar -xf FILE.zip -C ~/live-installer)"
    say "  2) run the installer through this Wine, from inside that directory:"
    say "       cd ~/live-installer && WINEPREFIX=~/.wine-ableton \\"
    say "           ~/.local/opt/$RUNTIME_NAME/bin/wine ./*.exe"
fi
say "Launch Live:   ~/.local/bin/ableton-live"
say "Then, inside Live (both matter):"
say "  * Options menu -> untick 'Auto-Scale Plugin Window'"
say "  * Preferences -> Audio -> Driver Type: ASIO -> Device: WineASIO"
say "================================================================"
exit 0
__PAYLOAD_BELOW__
