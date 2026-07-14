#!/usr/bin/env bash
set -u
umask 077

OUTPUT_DIR="${1:-$HOME/Desktop}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$OUTPUT_DIR/ableton-beta-macos-$STAMP.txt"
ID_DIR="${XDG_CONFIG_HOME:-$HOME/Library/Application Support}/ableton-beta"
TESTER_ID_FILE="$ID_DIR/tester-id-v2"
MACHINE_ID_FILE="$ID_DIR/machine-id"
mkdir -p "$OUTPUT_DIR" "$ID_DIR"
chmod 700 "$ID_DIR" 2>/dev/null || true

random_id() {
    if command -v uuidgen >/dev/null 2>&1; then
        uuidgen
    elif command -v openssl >/dev/null 2>&1; then
        openssl rand -hex 16
    else
        return 1
    fi
}

if [ -s "$TESTER_ID_FILE" ]; then
    TESTER_ID="$(head -n 1 "$TESTER_ID_FILE")"
else
    TESTER_TOKEN="$(random_id | tr -cd '[:xdigit:]' | tr '[:lower:]' '[:upper:]')" || exit 1
    [ "${#TESTER_TOKEN}" -ge 20 ] || exit 1
    TESTER_ID="BETA-$(printf '%.20s' "$TESTER_TOKEN")"
    printf '%s\n' "$TESTER_ID" > "$TESTER_ID_FILE"
fi

if [ -s "$MACHINE_ID_FILE" ]; then
    MACHINE_ID="$(head -n 1 "$MACHINE_ID_FILE")"
else
    MACHINE_ID="$(random_id)" || exit 1
    printf '%s\n' "$MACHINE_ID" > "$MACHINE_ID_FILE"
fi

header() {
    printf '\n[%s]\n' "$1"
}

collect() {
    command -v "$1" >/dev/null 2>&1 || return 0
    "$@" 2>&1 || true
}

escape_ere() {
    printf '%s' "$1" | sed 's/[][\\.^$*+?{}()|\/#]/\\&/g'
}

redact() {
    local home_re
    home_re="$(escape_ere "$HOME")"

    sed_args=( -E )
    case "$HOME" in
        /*) sed_args+=( -e "s#${home_re}#<HOME>#g" ) ;;
    esac
    sed_args+=(
        -e 's#(/Users/)[^/[:space:]]+#\1<USER>#g'
        -e 's#([[:xdigit:]]{2}:){5}[[:xdigit:]]{2}#<MAC>#g'
        -e 's#[[:alnum:]._%+-]+@[[:alnum:].-]+\.[[:alpha:]]{2,}#<EMAIL>#g'
        -e '/^[[:space:]]*([^:=]*([Ss]erial|UUID|UDID|WWN|GUID|[Uu]nique [Ii][Dd]|[Aa]sset[ _-]?[Tt]ag|[Pp]rocessor[Ii][Dd]|[Ii]dentifying[Nn]umber|[Ii]nstance[Ii][Dd]|PNPDeviceID|[Aa]ddress|Location ID|Mount Point|BSD Name|Device Identifier)[^:=]*)[:=]/d'
        -e 's#(password|passwd|token|secret|api[ _-]?key|machineguid|unlock\.json|ableton[ _-]?(serial|licen[cs]e)|licen[cs]e[ _-]?key)[^[:cntrl:]]*#\1=<REDACTED>#Ig'
    )
    sed "${sed_args[@]}"
}

omit_unique_identifiers() {
    sed -E \
        -e '/^[[:space:]]*([^:]*[Ss]erial[^:]*|[^:]*UUID[^:]*|[^:]*UDID[^:]*|[^:]*GUID[^:]*|[^:]*[Uu]nique [Ii][Dd][^:]*|UID|WWN|[^:]*[Aa]ddress[^:]*|Location ID|Mount Point|BSD Name|Device Identifier):/d'
}

collect_profile() {
    collect system_profiler -detailLevel full "$@" | omit_unique_identifiers
}

collect_bluetooth_profile() {
    collect_profile SPBluetoothDataType |
        sed -E 's#^          [^:]+:$#          Device:#'
}

collect_storage_profile() {
    collect_profile SPStorageDataType |
        sed -E 's#^    [^:]+:$#    Storage Volume:#'
}

collect_nvme_profile() {
    collect_profile SPNVMeDataType |
        sed -E 's#^            [^:]+:$#            Volume:#'
}

collect_audio_profile() {
    collect_profile SPAudioDataType |
        sed -E 's#^        [^:]+:$#        Audio Device:#'
}

{
    printf 'collected_utc='; date -u '+%Y-%m-%dT%H:%M:%SZ'

    header IDENTIFIERS
    printf 'tester_id=%s\nmachine_id=%s\n' "$TESTER_ID" "$MACHINE_ID"

    header SYSTEM
    collect sw_vers
    collect uname -rm
    for key in hw.model hw.logicalcpu hw.memsize hw.packages hw.physicalcpu; do
        value="$(sysctl -n "$key" 2>/dev/null || true)"
        [ -n "$value" ] && printf '%s=%s\n' "$key" "$value"
    done

    header PLATFORM_HARDWARE
    collect_profile SPHardwareDataType
    collect_storage_profile
    collect_nvme_profile
    collect_profile SPUSBDataType
    collect_profile SPThunderboltDataType
    collect_bluetooth_profile

    header DISPLAY
    collect_profile SPDisplaysDataType

    header AUDIO
    collect_audio_profile

    header ABLETON
    for root in /Applications "$HOME/Applications"; do
        [ -d "$root" ] || continue
        find "$root" -type d -name 'Ableton Live*.app' -prune -exec basename {} \; 2>/dev/null
    done | sort -u

    preferences="$HOME/Library/Preferences/Ableton"
    if [ -d "$preferences" ]; then
        find "$preferences" -type f \
            \( -name 'Options.txt' -o -name 'PluginScanDb.txt' \
            -o -name 'PluginScanner.txt' -o -name 'Log.txt' \) -print 2>/dev/null |
            while IFS= read -r file; do
                printf '%s/%s\n' "$(basename "$(dirname "$file")")" "$(basename "$file")"
            done | sort -u

    fi

    header PLUGINS
    for root in \
        /Library/Audio/Plug-Ins/Components \
        /Library/Audio/Plug-Ins/VST \
        /Library/Audio/Plug-Ins/VST3 \
        "$HOME/Library/Audio/Plug-Ins/Components" \
        "$HOME/Library/Audio/Plug-Ins/VST" \
        "$HOME/Library/Audio/Plug-Ins/VST3"
    do
        [ -d "$root" ] || continue
        find "$root" -type d \
            \( -name '*.component' -o -name '*.vst' -o -name '*.vst3' \) \
            -prune -exec basename {} \; 2>/dev/null
    done | sort -u
} 2>&1 | redact > "$OUT"
