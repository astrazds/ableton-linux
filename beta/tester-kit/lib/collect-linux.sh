#!/usr/bin/env bash

report_command() {
    local label="$1"
    shift

    subsection "$label"
    print_command "$@"
    if have "$1"; then
        "$@" 2>&1 || printf '(command exited %s)\n' "$?"
    else
        printf '(not installed: %s)\n' "$1"
    fi
}

report_file() {
    local label="$1"
    local path="$2"

    subsection "$label"
    if [[ -r "$path" ]]; then
        sed -n '1,240p' "$path"
    else
        printf '(not readable: %s)\n' "$path"
    fi
}

collect_package_versions() {
    local pattern='(alsa|cabextract|glibc|jack|mesa|nvidia|pipewire|portal|vulkan|wayland|wine|wireplumber|xorg|xwayland)'

    subsection "Relevant installed packages"
    if have pacman; then
        pacman -Q 2>&1 | grep -Ei "$pattern" || true
    elif have dpkg-query; then
        dpkg-query -W -f='${binary:Package}\t${Version}\n' 2>&1 | grep -Ei "$pattern" || true
    elif have rpm; then
        rpm -qa --qf '%{NAME}\t%{VERSION}-%{RELEASE}\n' 2>&1 | grep -Ei "$pattern" || true
    elif have zypper; then
        zypper --no-refresh search --installed-only 2>&1 | grep -Ei "$pattern" || true
    else
        printf '(no supported package query found)\n'
    fi
}

collect_portal_config() {
    local path
    local -a portal_paths=(
        "${XDG_CONFIG_HOME:-$HOME/.config}/xdg-desktop-portal/portals.conf"
        "/etc/xdg/xdg-desktop-portal/portals.conf"
        "/usr/share/xdg-desktop-portal/portals.conf"
    )
    subsection "Portal configuration files"
    for path in "${portal_paths[@]}"; do
        if [[ -r "$path" ]]; then
            printf '\n[%s]\n' "$path"
            sed -n '1,160p' "$path"
        fi
    done
}

collect_linux_report() {
    local session_id="${XDG_SESSION_ID:-}"
    local tool

    section "LINUX SYSTEM REPORT"
    printf 'Collected UTC: %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf 'Collected local: %s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')"
    printf 'Tester-ID: %s\n' "${TESTER_ID:-UNASSIGNED}"
    printf 'Machine-ID: %s\n' "${MACHINE_ID:-UNASSIGNED}"
    printf 'Collector scope: hardware, Linux, desktop, portal, graphics, audio, MIDI and required tools.\n'
    printf 'Collector exclusion: no network addresses, browser state, Wine authorisation, Ableton projects or licence files.\n'

    report_file "Operating system" /etc/os-release
    report_command "Kernel" uname -srmo
    report_command "C library" getconf GNU_LIBC_VERSION
    report_command "CPU" lscpu
    report_command "Memory" free -h

    subsection "Platform, board and firmware types"
    local field path value
    for field in \
        bios_vendor bios_version bios_date \
        sys_vendor product_name product_version product_sku \
        board_vendor board_name board_version \
        chassis_vendor chassis_type chassis_version
    do
        path="/sys/class/dmi/id/$field"
        if [[ -r "$path" ]]; then
            value="$(tr -d '\000\n' < "$path")"
            [[ -n "$value" ]] && printf '%s=%s\n' "$field" "$value"
        fi
    done
    subsection "Storage devices"
    if have lsblk; then
        lsblk -o NAME,TYPE,SIZE,FSTYPE,FSVER,MODEL,VENDOR,REV,TRAN 2>&1 ||
            lsblk -o NAME,TYPE,SIZE,FSTYPE,MODEL,VENDOR,REV,TRAN 2>&1 || true
    else
        printf '(not installed: lsblk)\n'
    fi
    report_command "Free space for home" df -hT "$HOME"

    report_command "PCI devices and drivers" lspci -nnk

    subsection "Graphics controller and driver"
    if have lspci; then
        lspci -nnk 2>&1 | grep -A4 -Ei 'VGA|3D|Display' || true
    else
        printf '(not installed: lspci)\n'
    fi
    report_command "OpenGL renderer" glxinfo -B
    report_command "Vulkan summary" vulkaninfo --summary

    section "DESKTOP AND DISPLAY"
    printf 'XDG_CURRENT_DESKTOP=%s\n' "${XDG_CURRENT_DESKTOP:-}"
    printf 'XDG_SESSION_DESKTOP=%s\n' "${XDG_SESSION_DESKTOP:-}"
    printf 'XDG_SESSION_TYPE=%s\n' "${XDG_SESSION_TYPE:-}"
    printf 'WAYLAND_DISPLAY=%s\n' "${WAYLAND_DISPLAY:-}"
    printf 'DISPLAY=%s\n' "${DISPLAY:-}"
    printf 'GDK_SCALE=%s\n' "${GDK_SCALE:-}"
    printf 'QT_SCALE_FACTOR=%s\n' "${QT_SCALE_FACTOR:-}"
    if [[ -n "$session_id" ]] && have loginctl; then
        report_command "Desktop session" loginctl show-session "$session_id" -p Type -p Class -p Desktop -p Remote -p State
    fi
    report_command "X/XWayland displays" xrandr --current
    report_command "KDE display state" kscreen-doctor -o
    if have gsettings; then
        report_command "GNOME scale" gsettings get org.gnome.desktop.interface scaling-factor
        report_command "GNOME text scale" gsettings get org.gnome.desktop.interface text-scaling-factor
        report_command "Mutter experimental features" gsettings get org.gnome.mutter experimental-features
    fi

    section "DESKTOP PORTALS"
    report_command "Portal service" systemctl --user --no-pager show xdg-desktop-portal.service -p LoadState -p ActiveState -p SubState -p FragmentPath -p MainPID
    report_command "GTK portal backend" systemctl --user --no-pager show xdg-desktop-portal-gtk.service -p LoadState -p ActiveState -p SubState -p FragmentPath -p MainPID
    report_command "KDE portal backend" systemctl --user --no-pager show xdg-desktop-portal-kde.service -p LoadState -p ActiveState -p SubState -p FragmentPath -p MainPID
    report_command "GNOME portal backend" systemctl --user --no-pager show xdg-desktop-portal-gnome.service -p LoadState -p ActiveState -p SubState -p FragmentPath -p MainPID
    collect_portal_config

    section "AUDIO"
    report_command "PipeWire version" pipewire --version
    report_command "WirePlumber version" wireplumber --version
    report_command "PipeWire and WirePlumber services" systemctl --user --no-pager show pipewire.service pipewire-pulse.service wireplumber.service -p Id -p LoadState -p ActiveState -p SubState -p FragmentPath -p MainPID -p ExecMainStatus
    report_command "PipeWire graph summary" wpctl status
    report_command "Pulse compatibility information" pactl info
    report_command "Pulse cards" pactl list cards
    report_command "Pulse sinks" pactl list sinks
    report_command "Pulse sources" pactl list sources
    report_command "PipeWire rate and quantum" pw-metadata -n settings
    report_command "PipeWire processing and xruns" pw-top -b -n 1
    report_command "JACK ports and connections" jack_lsp -A -c
    report_command "ALSA playback devices" aplay -l
    report_command "ALSA capture devices" arecord -l
    subsection "Realtime limits"
    printf 'rtprio=%s\n' "$(ulimit -r 2>&1 || true)"
    printf 'memlock-kbytes=%s\n' "$(ulimit -l 2>&1 || true)"

    section "MIDI"
    report_command "ALSA sequencer clients" aconnect -l
    report_command "Raw MIDI devices" amidi -l
    if [[ -e /dev/snd/seq ]]; then
        printf '/dev/snd/seq: present\n'
    else
        printf '/dev/snd/seq: absent\n'
    fi

    section "USB AND RELEVANT KERNEL MODULES"
    report_command "USB devices" lsusb
    report_command "USB topology" lsusb -t
    subsection "USB device types"
    local device
    for device in /sys/bus/usb/devices/*; do
        [[ -r "$device/idVendor" ]] || continue
        printf '[USB device]\n'
        for field in manufacturer product idVendor idProduct bcdDevice speed; do
            path="$device/$field"
            if [[ -r "$path" ]]; then
                value="$(tr -d '\000\n' < "$path")"
                [[ -n "$value" ]] && printf '%s=%s\n' "$field" "$value"
            fi
        done
    done
    report_command "Input devices" libinput list-devices
    subsection "Audio and graphics modules"
    if have lsmod; then
        lsmod 2>&1 | grep -Ei '^(amdgpu|i915|nouveau|nvidia|snd|sound|usb_audio)' || true
    else
        printf '(not installed: lsmod)\n'
    fi

    section "TOOLS AND PACKAGES"
    for tool in bash curl wget sha256sum timeout tar zstd cabextract cc pkg-config; do
        if have "$tool"; then
            printf '%-14s %s\n' "$tool" "$(command -v "$tool")"
        else
            printf '%-14s MISSING\n' "$tool"
        fi
    done
    collect_package_versions
}
