#!/bin/sh
# RAM-only native-loader entry; never mount raw SD from the running camera.
set -uf
umask 077
export PATH=/usr/bin:/bin:/sbin:/usr/sbin
BB=/bin/busybox
RAM=/tmp_bt
MOUNTS=/proc/mounts
COMM=/proc/149/comm
MAPS=/proc/149/maps
LOCK=$RAM/a6400-film-runtime.lock
REQUEST=$RAM/a6400-ui-load.req
GUARD=$RAM/a6400-ui-load-guard.elf
SETTINGS=$RAM/a6400-film-menu-job.elf
DAEMON=$RAM/a6400-film-capture-daemon.elf
RUNTIME_LOG=$RAM/a6400-film-capture.runtime.log
request_ours=0
last_request_hash=
fail() { echo "FILM_RUNTIME_FAIL stage=$1"; exit 1; }
disabled() {
    test -e /tmp/disable_hook || test -e "$RAM/a6400-film.DISABLE" ||
        test -e "$RAM/a6400-film-capture.stop"
}
no_sd_mount() {
    test -r "$MOUNTS" || return 1
    while read -r source target type options rest; do
        case "$source" in /dev/mmca*|/dev/mmcblk*) return 1;; esac
    done < "$MOUNTS"
    return 0
}
hash_is() {
    test -f "$1" && test ! -L "$1" || return 1
    hash_line=$("$BB" sha256sum "$1") || return 1
    test "${hash_line%% *}" = "$2"
}
gate() { ! disabled && no_sd_mount; }
ready() {
    test -r "$COMM" && test -r "$MAPS" || return 1
    IFS= read -r comm < "$COMM" || return 1
    test "$comm" = im.elf &&
        "$BB" grep -q '/usr/bin/im\.elf$' "$MAPS" &&
        "$BB" grep -q '/usr/lib/libObj\.so$' "$MAPS" &&
        "$BB" grep -q '/usr/lib/viewUnified2\.so$' "$MAPS"
}
guard_report_ok() {
    end_count=0
    end_bad=0
    while IFS= read -r line; do
        case "$line" in
            END\ *)
                end_count=$((end_count+1))
                set -- $line
                if test "$#" != 4; then end_bad=1;continue; fi
                number=${4#errno=}
                test "$1" = END && test "$2" = result=0 &&
                    test "$3" = "op=$guard_op" && test "$4" = "errno=$number" || end_bad=1
                case "$number" in ''|*[!0-9]*) end_bad=1;; esac;;
        esac
    done < "$guard_log"
    test "$end_count" = 1 && test "$end_bad" = 0
}
guard_step() {
    guard_name=$1
    guard_op=$2
    guard_hash=$3
    guard_log=$RAM/a6400-film-runtime.$guard_name.log
    gate && ready || fail "guard_gate-$guard_name"
    hash_is "$GUARD" @@GUARD_SHA@@ && hash_is "$RAM/a6400-$guard_name-persist.req" "$guard_hash" || fail guard_pin
    if test "$request_ours" = 1; then
        hash_is "$REQUEST" "$last_request_hash" || fail request_drift
    else
        test ! -e "$REQUEST" && test ! -L "$REQUEST" || fail existing_request
    fi
    "$BB" cp "$RAM/a6400-$guard_name-persist.req" "$REQUEST" || fail request_copy
    hash_is "$REQUEST" "$guard_hash" || fail request_copy_hash
    request_ours=1
    last_request_hash=$guard_hash
    "$GUARD" > "$guard_log" 2>&1
    guard_rc=$?
    "$BB" cat "$guard_log"
    test "$guard_rc" = 0 && guard_report_ok || fail "guard_result-$guard_name"
}
if disabled; then echo FILM_RUNTIME_DISABLED;exit 0;fi
no_sd_mount || fail linux_sd_mounted
test -f "$RAM/a6400-film-staged" && test ! -L "$RAM/a6400-film-staged" || fail missing_stage
while read -r name dest expected mode; do
    hash_is "$RAM/$dest" "$expected" || fail "artifact_hash-$dest"
done <<'FILM_ARTIFACTS'
@@ARTIFACT_ROWS@@
FILM_ARTIFACTS
# Existing native-operation locks require human inspection, never deletion.
for path in "$RAM/a6400-capture-v1.lock" "$RAM/a6400-film-capture.inflight" "$RAM/a6400-film-job.lock"; do
    test ! -e "$path" && test ! -L "$path" || fail outstanding_native_operation
done
# The daemon owns O_EXCL creation; preserve prior evidence before loading the UI.
test ! -e "$RUNTIME_LOG" && test ! -L "$RUNTIME_LOG" || fail existing_runtime_log
wait_count=0
while ! ready; do
    gate || fail waiting_gate
    test "$wait_count" -lt 30 || fail im_not_ready
    "$BB" sleep 1
    wait_count=$((wait_count+1))
done
gate || fail pre_install_gate
"$BB" mkdir "$LOCK" || fail existing_runtime
printf 'phase=guard_check\npid=%s\npackage=@@PACKAGE_ID@@\n' "$$" > "$LOCK/manifest" || fail manifest
guard_step check 0 @@CHECK_SHA@@
guard_step install 1 @@INSTALL_SHA@@
# The one-shot loader still waits for a natural touch release. Unloaded UI is
# unavailable and cannot authorize photos; its compiled initial setting is OFF.
line=$("$SETTINGS")
settings_rc=$?
if test "$settings_rc" = 10 && test "$line" = FILM_OFF; then
    :
elif test "$settings_rc" = 11; then
    case "$line" in
        'FILM_UNAVAILABLE source='*) number=${line#FILM_UNAVAILABLE source=};;
        *) fail initial_settings;;
    esac
    case "$number" in ''|*[!0-9]*) fail initial_settings;; esac
else
    fail initial_settings_not_off
fi
gate && ready || fail pre_daemon_gate
printf 'phase=daemon\npid=%s\npackage=@@PACKAGE_ID@@\ninitial_enabled=0\n' "$$" > "$LOCK/manifest" || fail manifest
echo FILM_RUNTIME_READY initial_enabled=0
exec "$DAEMON" watch
