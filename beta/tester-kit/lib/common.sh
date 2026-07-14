#!/usr/bin/env bash

if [[ ${ABLETON_BETA_COMMON_LOADED:-0} == 1 ]]; then
    return 0
fi
ABLETON_BETA_COMMON_LOADED=1

section() {
    printf '\n===== %s =====\n' "$1"
}

subsection() {
    printf '\n--- %s ---\n' "$1"
}

have() {
    command -v "$1" >/dev/null 2>&1
}

escape_ere() {
    sed 's/[][(){}.^$*+?|\\]/\\&/g'
}

redact_stream() {
    local home_re

    home_re="$(printf '%s' "${HOME:-}" | escape_ere)"

    local -a expressions=( -E )

    if [[ ${HOME:-} == /* ]]; then
        expressions+=( -e "s|$home_re|<HOME>|g" )
    fi
    expressions+=(
        -e 's#(/home/)[^/[:space:]]+#\1<USER>#g'
        -e 's#luks-[0-9a-fA-F-]{16,}#luks-<REDACTED>#g'
        -e 's#([[:xdigit:]]{2}:){5}[[:xdigit:]]{2}#<MAC>#g'
        -e 's#[[:alnum:]._%+-]+@[[:alnum:].-]+\.[[:alpha:]]{2,}#<EMAIL>#g'
        -e 's#title="[^"]*"#title="<WINDOW-TITLE>"#g'
        -e "s#(cls='[^']*') '[^']*'#\1 '<WINDOW-TITLE>'#g"
        -e '/^[[:space:]]*([^:=]*([Ss]erial|UUID|UDID|WWN|GUID|[Uu]nique [Ii][Dd]|[Aa]sset[ _-]?[Tt]ag|[Pp]rocessor[Ii][Dd]|[Ii]dentifying[Nn]umber|[Ii]nstance[Ii][Dd]|PNPDeviceID|[Aa]ddress|Location ID|Mount Point|Device Identifier)[^:=]*)[:=]/d'
        -e '/(MachineGuid|Unlock[.]json|AUZ|password|passwd|access[_ -]?token|refresh[_ -]?token|api[_ -]?key|licen[cs]e[_ -]?key|cookie)/I c\<REDACTED: credential or licence line>'
    )

    sed "${expressions[@]}"
}

random_machine_id() {
    if [[ -r /proc/sys/kernel/random/uuid ]]; then
        head -n 1 /proc/sys/kernel/random/uuid
    elif have uuidgen; then
        uuidgen
    elif have openssl; then
        openssl rand -hex 16
    else
        printf '%s-%s-%s\n' "$(date +%s)" "$$" "${RANDOM:-0}" | sha256sum | awk '{print substr($1,1,32)}'
    fi
}

load_tester_id() {
    local id_dir="${XDG_CONFIG_HOME:-$HOME/.config}/ableton-beta"
    local id_file="$id_dir/tester-id-v2"
    local token tester_id

    if [[ -s "$id_file" ]]; then
        head -n 1 "$id_file"
        return
    fi

    token="$(random_machine_id | tr -cd '[:xdigit:]' | tr '[:lower:]' '[:upper:]')"
    [[ ${#token} -ge 20 ]] || return 1
    tester_id="BETA-${token:0:20}"

    mkdir -p "$id_dir"
    chmod 700 "$id_dir" 2>/dev/null || true
    printf '%s\n' "$tester_id" > "$id_file"
    chmod 600 "$id_file" 2>/dev/null || true
    printf '%s\n' "$tester_id"
}

load_machine_id() {
    local id_dir="${XDG_CONFIG_HOME:-$HOME/.config}/ableton-beta"
    local id_file="$id_dir/machine-id"

    if [[ -s "$id_file" ]]; then
        head -n 1 "$id_file"
        return
    fi

    mkdir -p "$id_dir"
    chmod 700 "$id_dir" 2>/dev/null || true
    random_machine_id > "$id_file"
    chmod 600 "$id_file" 2>/dev/null || true
    head -n 1 "$id_file"
}

print_command() {
    printf '$'
    printf ' %q' "$@"
    printf '\n'
}

ask_result() {
    local prompt="$1"
    local answer

    if [[ ${NON_INTERACTIVE:-0} == 1 || ! -t 0 ]]; then
        printf 'REVIEW'
        return
    fi

    while true; do
        printf '%s [y/n/u]: ' "$prompt" >/dev/tty
        IFS= read -r answer </dev/tty || answer=u
        case "${answer,,}" in
            y|yes) printf 'PASS'; return ;;
            n|no) printf 'FAIL'; return ;;
            u|unknown|unsure|'') printf 'REVIEW'; return ;;
        esac
    done
}

record_result() {
    local id="$1"
    local status="$2"
    local label="$3"
    local detail="$4"

    printf '%s\t%s\t%s\t%s\n' "$id" "$status" "$label" "$detail" >> "$RESULTS_FILE"
    printf '[%s] %s: %s' "$status" "$id" "$label"
    if [[ -n "$detail" ]]; then
        printf ' (%s)' "$detail"
    fi
    printf '\n'
}

append_redacted_file() {
    local label="$1"
    local file="$2"

    subsection "$label"
    if [[ -s "$file" ]]; then
        redact_stream < "$file"
    else
        printf '(no output)\n'
    fi
}

status_count() {
    local wanted="$1"
    awk -F '\t' -v wanted="$wanted" '$2 == wanted {count++} END {print count + 0}' "$RESULTS_FILE"
}
