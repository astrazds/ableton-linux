# Sourceable display-scale detection. ableton_detect_scale prints the primary monitor's scale
# ("1", "1.25", ...) or returns 1 when no probe answers (probes: GNOME, KDE, sway, Hyprland, Xft.dpi).

_ads_gnome() {
    local state rows all prim
    state="$(timeout 5 gdbus call --session \
        --dest org.gnome.Mutter.DisplayConfig \
        --object-path /org/gnome/Mutter/DisplayConfig \
        --method org.gnome.Mutter.DisplayConfig.GetCurrentState 2>/dev/null)" || return 1
    # logical monitors serialize as "(x, y, scale, uint32 transform, primary, ..."
    rows="$(printf '%s\n' "$state" \
        | grep -oE '\(-?[0-9]+, -?[0-9]+, [0-9]+(\.[0-9]+)?, uint32 [0-9]+, (true|false)')"
    [ -n "$rows" ] || return 1
    all="$(printf '%s\n' "$rows" | awk -F', ' '{print $3}' | sort -u)"
    prim="$(printf '%s\n' "$rows" | awk -F', ' '$5=="true"{print $3; exit}')"
    [ -n "$prim" ] || prim="$(printf '%s\n' "$rows" | awk -F', ' 'NR==1{print $3}')"
    if [ "$(printf '%s\n' "$all" | wc -l)" -gt 1 ]; then
        echo "note: monitors run mixed scales ($(printf '%s' "$all" | tr '\n' ' ' )) — using the primary monitor's $prim" >&2
    fi
    printf '%s\n' "$prim"
}

_ads_kde() {
    command -v kscreen-doctor >/dev/null 2>&1 || return 1
    local out prim
    out="$(timeout 5 kscreen-doctor -o 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g')"
    [ -n "$out" ] || return 1
    # Plasma 5: one "Output: ..." line per screen with "primary"; Plasma 6 splits blocks, marks "priority 1".
    prim="$(printf '%s\n' "$out" | awk '
        /^Output:/ { blk++ }
        blk {
            if (match($0, /Scale: [0-9.]+/)) s[blk] = substr($0, RSTART+7, RLENGTH-7)
            if ($0 ~ / primary/ || $0 ~ /priority 1([^0-9]|$)/) p[blk] = 1
        }
        END {
            for (i = 1; i <= blk; i++) if (p[i] && s[i] != "") { print s[i]; exit }
            for (i = 1; i <= blk; i++) if (s[i] != "")          { print s[i]; exit }
        }')"
    [ -n "$prim" ] || return 1
    printf '%s\n' "$prim"
}

_ads_sway() {
    command -v swaymsg >/dev/null 2>&1 || return 1
    [ -n "${SWAYSOCK:-}" ] || return 1
    local s
    s="$(timeout 5 swaymsg -t get_outputs 2>/dev/null \
        | grep -oE '"scale": *[0-9.]+' | head -1 | grep -oE '[0-9.]+')"
    [ -n "$s" ] || return 1
    printf '%s\n' "$s"
}

_ads_hyprland() {
    command -v hyprctl >/dev/null 2>&1 || return 1
    [ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ] || return 1
    local s
    s="$(timeout 5 hyprctl monitors 2>/dev/null \
        | grep -oE 'scale: [0-9.]+' | head -1 | grep -oE '[0-9.]+')"
    [ -n "$s" ] || return 1
    printf '%s\n' "$s"
}

_ads_xftdpi() {
    [ -n "${DISPLAY:-}" ] || return 1
    command -v xrdb >/dev/null 2>&1 || return 1
    local dpi
    dpi="$(timeout 5 xrdb -query 2>/dev/null | awk '$1=="Xft.dpi:"{print $2; exit}')"
    [ -n "$dpi" ] || return 1
    awk -v d="$dpi" 'BEGIN{ printf "%g\n", d/96 }'
}

ableton_detect_scale() {
    local scale
    for probe in _ads_gnome _ads_kde _ads_sway _ads_hyprland _ads_xftdpi; do
        if scale="$($probe)"; then
            # normalize: 1.0 -> 1, 1.250 -> 1.25
            printf '%s\n' "$scale" | awk '{ printf "%g\n", $1 }'
            return 0
        fi
    done
    return 1
}
