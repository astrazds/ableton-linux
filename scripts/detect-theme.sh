# Sourceable host light/dark-scheme detection. ableton_detect_theme prints "dark" or "light"
# or returns 1 when no probe answers (probes: XDG settings portal, GNOME gsettings).

_adt_portal() {
    local out val
    out="$(timeout 5 gdbus call --session \
        --dest org.freedesktop.portal.Desktop \
        --object-path /org/freedesktop/portal/desktop \
        --method org.freedesktop.portal.Settings.Read \
        org.freedesktop.appearance color-scheme 2>/dev/null)" || return 1
    # serializes as "(<<uint32 1>>,)": 0 = no preference, 1 = prefer dark, 2 = prefer light
    val="$(printf '%s\n' "$out" | grep -oE 'uint32 [0-9]+' | awk '{print $2; exit}')"
    [ -n "$val" ] || return 1
    case "$val" in
        1) echo dark ;;
        *) echo light ;;
    esac
}

_adt_gsettings() {
    command -v gsettings >/dev/null 2>&1 || return 1
    local scheme
    scheme="$(timeout 5 gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null)" || return 1
    case "$scheme" in
        *prefer-dark*)            echo dark ;;
        *prefer-light*|*default*) echo light ;;
        *)                        return 1 ;;
    esac
}

ableton_detect_theme() {
    local theme
    for probe in _adt_portal _adt_gsettings; do
        if theme="$($probe)"; then
            printf '%s\n' "$theme"
            return 0
        fi
    done
    return 1
}
