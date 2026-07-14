#!/usr/bin/env bash
set -u
umask 077

OUTPUT_DIR="${1:-$HOME/Desktop}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$OUTPUT_DIR/ableton-beta-linux-$STAMP.txt"
ID_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/ableton-beta"
TESTER_ID_FILE="$ID_DIR/tester-id-v2"
MACHINE_ID_FILE="$ID_DIR/machine-id"
mkdir -p "$OUTPUT_DIR" "$ID_DIR"
chmod 700 "$ID_DIR" 2>/dev/null || true

random_id() {
    if [ -r /proc/sys/kernel/random/uuid ]; then
        head -n 1 /proc/sys/kernel/random/uuid
    elif command -v uuidgen >/dev/null 2>&1; then
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

collect_pactl_card_types() {
    command -v pactl >/dev/null 2>&1 || return 0
    pactl list cards 2>&1 |
        sed -n -E \
            -e 's/^Card #[0-9]+$/Card type:/p' \
            -e '/^[[:space:]]*(device\.(api|bus|form_factor|product\.(id|name)|vendor\.(id|name))|alsa\.(card_name|long_card_name))[[:space:]]*=/p'
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
        -e 's#(/home/)[^/[:space:]]+#\1<USER>#g'
        -e 's#(/run/user/)[0-9]+#\1<UID>#g'
        -e 's#luks-[0-9a-f-]{16,}#luks-<REDACTED>#Ig'
        -e 's#([[:xdigit:]]{2}:){5}[[:xdigit:]]{2}#<MAC>#g'
        -e 's#[[:alnum:]._%+-]+@[[:alnum:].-]+\.[[:alpha:]]{2,}#<EMAIL>#g'
        -e '/^[[:space:]]*([^:=]*([Ss]erial|UUID|UDID|WWN|GUID|[Uu]nique [Ii][Dd]|[Aa]sset[ _-]?[Tt]ag|[Pp]rocessor[Ii][Dd]|[Ii]dentifying[Nn]umber|[Ii]nstance[Ii][Dd]|PNPDeviceID|[Aa]ddress|Location ID|Mount Point|Device Identifier)[^:=]*)[:=]/d'
        -e 's#(password|passwd|token|secret|api[ _-]?key|machineguid|unlock\.json|ableton[ _-]?(serial|licen[cs]e)|licen[cs]e[ _-]?key)[^[:cntrl:]]*#\1=<REDACTED>#Ig'
    )
    sed "${sed_args[@]}"
}

{
    printf 'collected_utc='; date -u '+%Y-%m-%dT%H:%M:%SZ'

    header IDENTIFIERS
    printf 'tester_id=%s\nmachine_id=%s\n' "$TESTER_ID" "$MACHINE_ID"

    header SYSTEM
    sed -n -E '/^(PRETTY_NAME|ID|ID_LIKE|VERSION_ID)=/p' /etc/os-release 2>/dev/null || true
    collect uname -srmo
    if command -v lscpu >/dev/null 2>&1; then
        lscpu 2>&1 | sed -n -E '/^(Architecture|CPU\(s\)|Vendor ID|Model name|Socket\(s\)|Core\(s\) per socket|Thread\(s\) per core|CPU max MHz|CPU min MHz):/p'
    fi
    collect free -h
    header PLATFORM_HARDWARE
    for field in \
        bios_vendor bios_version bios_date \
        sys_vendor product_name product_version product_sku \
        board_vendor board_name board_version \
        chassis_vendor chassis_type chassis_version
    do
        path="/sys/class/dmi/id/$field"
        if [ -r "$path" ]; then
            value="$(tr -d '\000\n' < "$path")"
            [ -n "$value" ] && printf '%s=%s\n' "$field" "$value"
        fi
    done

    header STORAGE
    if command -v lsblk >/dev/null 2>&1; then
        lsblk -o NAME,TYPE,SIZE,FSTYPE,FSVER,MODEL,VENDOR,REV,TRAN 2>&1 ||
            lsblk -o NAME,TYPE,SIZE,FSTYPE,MODEL,VENDOR,REV,TRAN 2>&1 || true
    fi
    header PCI
    collect lspci -nnk

    header GRAPHICS
    collect glxinfo -B
    collect vulkaninfo --summary
    printf 'desktop=%s\nsession=%s\nwayland=%s\ndisplay=%s\n' \
        "${XDG_CURRENT_DESKTOP:-}" "${XDG_SESSION_TYPE:-}" \
        "${WAYLAND_DISPLAY:-}" "${DISPLAY:-}"

    header AUDIO
    if command -v systemctl >/dev/null 2>&1; then
        for unit in pipewire pipewire-pulse wireplumber; do
            state="$(systemctl --user is-active "$unit" 2>/dev/null || true)"
            printf '%s=%s\n' "$unit" "${state:-unavailable}"
        done
    fi
    collect pipewire --version
    collect wireplumber --version
    collect_pactl_card_types
    collect pw-metadata -n settings
    collect aplay -l
    collect arecord -l

    header MIDI
    collect aconnect -l
    collect amidi -l

    header USB
    collect lsusb
    collect lsusb -t
    for device in /sys/bus/usb/devices/*; do
        [ -r "$device/idVendor" ] || continue
        printf '[USB device]\n'
        for field in manufacturer product idVendor idProduct bcdDevice speed; do
            path="$device/$field"
            if [ -r "$path" ]; then
                value="$(tr -d '\000\n' < "$path")"
                [ -n "$value" ] && printf '%s=%s\n' "$field" "$value"
            fi
        done
    done

    header INPUT
    collect libinput list-devices

    header PACKAGING
    collect flatpak --version
} 2>&1 | redact > "$OUT"
