#!/bin/sh
# One bounded native-read -> author-film RAM job; apply saves a separate JPEG.
# Successful render retains its private mount for verified host backup/release.
# An uncertain native operation must retain all scratch even after client exit.
set -u
set -f
umask 077
RAM=/tmp_bt/a6400-film-job
LOCK=/tmp_bt/a6400-film-job.lock
BB=/bin/busybox
NATIVE=/tmp_bt/a6400-film-native-command.elf
WORKER=/tmp_bt/a6400-film-job.elf
SETTINGS=/tmp_bt/a6400-film-menu-job.elf
MEMINFO=/proc/meminfo
MOUNTS=/proc/mounts
BOOTFILE=/proc/sys/kernel/random/boot_id
NATIVE_SHA=__NATIVE_SHA__
WORKER_SHA=__WORKER_SHA__
SETTINGS_SHA=__SETTINGS_SHA__
INPUT=$RAM/input.jpg
OUTPUT=$RAM/output.jpg
MODE=${1:-}
SOURCE=${2:-}
EXPECTED_BYTES=${3:-}
TARGET=
PRESET=
GRAIN=
HALATION=
INITIAL_PLAN=
BOOT=
PHASE=arguments
INPUT_SHA=
OUTPUT_SHA=
OUTPUT_BYTES=0
TARGET_ATTEMPTED=0
TARGET_VERIFIED=0
ORIGINAL_VERIFIED=0
READBACK_BYTES=0
READBACK_SHA=
NATIVE_UNCERTAIN=0
NATIVE_LAST_STATUS=none
applied=0
retain=0
ram_created=0
native_inflight=0
worker_inflight=0
native_pid=0
worker_pid=0

fail() { echo "RAM_JOB_FAIL stage=$1"; exit "${2:-1}"; }
hash_file() {
    test -f "$1" && test ! -L "$1" || return 1
    hash_line=$("$BB" sha256sum "$1") || return 1
    HASH=${hash_line%% *}
    test "${#HASH}" = 64 || return 1
    case "$HASH" in *[!0-9a-f]*) return 1;; esac
}
hash_is() { hash_file "$1" && test "$HASH" = "$2"; }
file_bytes() {
    test -f "$1" && test ! -L "$1" || return 1
    size_line=$("$BB" wc -c < "$1") || return 1
    set -- $size_line
    test "$#" = 1 || return 1
    case "$1" in ''|*[!0-9]*) return 1;; esac
    test "${#1}" -le 8 || return 1
    FILE_BYTES=$1
}
no_linux_sd() {
    test -r "$MOUNTS" || return 1
    while read -r mount_source mount_target mount_type mount_options mount_rest; do
        case "$mount_source" in /dev/mmca*|/dev/mmcblk*) return 1;; esac
    done < "$MOUNTS"
}
# 0=one owned RAM mount;1=no mount at RAM;2=unreadable/other/ambiguous.
ram_mount_state() {
    test -r "$MOUNTS" || return 2
    mount_count=0
    mount_owned=0
    while read -r mount_source mount_target mount_type mount_options mount_rest; do
        if test "$mount_target" = "$RAM"; then
            mount_count=$((mount_count+1))
            if test "$mount_source" = a6400-film-job && test "$mount_type" = tmpfs; then
                mount_owned=$((mount_owned+1))
            fi
        fi
    done < "$MOUNTS"
    test "$mount_count" = 0 && return 1
    test "$mount_count" = 1 && test "$mount_owned" = 1 && return 0
    return 2
}
manifest() {
    printf 'boot_id=%s\npid=%s\nmode=%s\nphase=%s\nsource=%s\nexpected_bytes=%s\ntarget=%s\npreset=%s\ngrain=%s\nhalation=%s\ninput_sha256=%s\noutput_sha256=%s\noutput_bytes=%s\nnative_pid=%s\nworker_pid=%s\nnative_inflight=%s\nworker_inflight=%s\n' \
        "$BOOT" "$$" "$MODE" "$PHASE" "$SOURCE" "$EXPECTED_BYTES" "$TARGET" \
        "$PRESET" "$GRAIN" "$HALATION" "$INPUT_SHA" "$OUTPUT_SHA" "$OUTPUT_BYTES" \
        "$native_pid" "$worker_pid" "$native_inflight" "$worker_inflight" > "$LOCK/manifest" || return 1
    printf 'ram=%s\ninput=%s\noutput=%s\nnative_sha256=%s\nworker_sha256=%s\nsettings_sha256=%s\n' \
        "$RAM" "$INPUT" "$OUTPUT" "$NATIVE_SHA" "$WORKER_SHA" "$SETTINGS_SHA" >> "$LOCK/manifest" || return 1
    printf 'target_attempted=%s\ntarget_verified=%s\noriginal_verified=%s\nreadback_bytes=%s\nreadback_sha256=%s\n' \
        "$TARGET_ATTEMPTED" "$TARGET_VERIFIED" "$ORIGINAL_VERIFIED" "$READBACK_BYTES" "$READBACK_SHA" >> "$LOCK/manifest" || return 1
    printf 'native_uncertain=%s\nnative_last_status=%s\n' "$NATIVE_UNCERTAIN" "$NATIVE_LAST_STATUS" >> "$LOCK/manifest"
}
cleanup() {
    test "$native_inflight" = 0 && test "$worker_inflight" = 0 || return 1
    if test "$ram_created" = 1; then
        ram_mount_state
        cleanup_mount=$?
        if test "$cleanup_mount" = 0; then
            # Only these fixed files belong to this job; never delete by glob.
            "$BB" rm -f "$INPUT" "$OUTPUT" || return 1
            "$BB" umount "$RAM" || return 1
            ram_mount_state
            test "$?" = 1 || return 1
        elif test "$cleanup_mount" != 1; then
            return 1
        fi
        "$BB" rmdir "$RAM" || return 1
    fi
    "$BB" rm -f "$LOCK/native.log" "$LOCK/native.current.log" "$LOCK/verify.log" "$LOCK/render.log" "$LOCK/manifest" || return 1
    "$BB" rmdir "$LOCK"
}
finish() {
    job_rc=$?
    trap - 0
    if test "$native_inflight" != 0 || test "$worker_inflight" != 0; then
        if test "$native_inflight" != 0; then NATIVE_UNCERTAIN=1; fi
        retain=1
        PHASE=operation_unconfirmed
        job_rc=21
    fi
    if test "$retain" = 0; then
        if ! cleanup; then
            retain=1
            PHASE=cleanup_unconfirmed
            job_rc=21
        fi
    fi
    if test "$retain" = 1; then
        manifest || echo 'RAM_JOB_MANIFEST_WRITE_FAILED'
        echo "RAM_JOB_RETAINED phase=$PHASE ram=$RAM lock=$LOCK result=$job_rc"
    elif test "$job_rc" = 0 && test "$applied" = 1; then
        echo "RAM_JOB_APPLIED source=$SOURCE target=$TARGET preset=$PRESET grain=$GRAIN halation=$HALATION source_bytes=$EXPECTED_BYTES source_sha256=$INPUT_SHA output_bytes=$OUTPUT_BYTES output_sha256=$OUTPUT_SHA original_verified=$ORIGINAL_VERIFIED"
    fi
    exit "$job_rc"
}
interrupted() { fail interrupted 21; }
guard() {
    IFS= read -r current_boot < "$BOOTFILE" || return 1
    test "$current_boot" = "$BOOT" && no_linux_sd && ram_mount_state
}
decimal() {
    test -n "$1" && test "${#1}" -le 10 || return 1
    case "$1" in *[!0-9]*) return 1;; esac
}
# Accept the pinned command's legacy report and its extended history counters.
# A prompt is transport completion only; each copy is independently verified.
native_report_ok() {
    report_count=0
    report_bad=0
    while IFS= read -r report_line; do
        case "$report_line" in
            NATIVE_RETURN\ *|NATIVE_ABSENT\ *|NATIVE_FAIL\ *)
                report_count=$((report_count+1))
                set -- $report_line
                report_fields=$#
                test "$#" = 6 || test "$#" = 9 || { report_bad=1;continue; }
                test "$1" = "$report_kind" && test "$2" = "operation=$native_op" &&
                    test "$3" = "reason=$report_reason" && test "$5" = uncertain=0 &&
                    test "$6" = copy_verified=0 || report_bad=1
                report_number=${4#response_bytes=}
                report_response_bytes=$report_number
                test "$4" = "response_bytes=$report_number" && decimal "$report_number" || report_bad=1
                if test "$#" = 9; then
                    report_number=${7#startup_history_bytes=}
                    report_startup_bytes=$report_number
                    test "$7" = "startup_history_bytes=$report_number" && decimal "$report_number" || report_bad=1
                    report_number=${8#pre_echo_bytes=}
                    report_pre_echo_bytes=$report_number
                    test "$8" = "pre_echo_bytes=$report_number" && decimal "$report_number" || report_bad=1
                    report_number=${9#pre_echo_overflows=}
                    report_pre_echo_overflows=$report_number
                    test "$9" = "pre_echo_overflows=$report_number" && decimal "$report_number" || report_bad=1
                fi;;
        esac
    done < "$LOCK/native.current.log"
    test "$report_count" = 1 && test "$report_bad" = 0
}
native_safe_presend_failure() {
    # A child exit alone says nothing about a Sony operation it already sent.
    # Only the pinned helper's complete, exact pre-send failure receipt is safe.
    case "$native_rc" in
        3) set -- monitor_pin;;
        5) set -- startup startup_busy history_capacity;;
        6) set -- before_send write_command;;
        20) set -- signal;;
        *) return 1;;
    esac
    test ! -e "$INPUT" && test ! -L "$INPUT" || return 1
    "$BB" grep -q '^NATIVE_SENT ' "$LOCK/native.current.log"
    sent_status=$?
    test "$sent_status" = 1 || return 1
    report_kind=NATIVE_FAIL
    for report_reason do
        if native_report_ok && test "$report_fields" = 9 &&
           test "$report_response_bytes" = 0 && test "$report_pre_echo_bytes" = 0 &&
           test "$report_pre_echo_overflows" = 0; then
            if test "$native_rc" != 3 || test "$report_startup_bytes" = 0; then return 0; fi
        fi
    done
    return 1
}
native_run() {
    native_op=$1
    native_path=$2
    report_kind=NATIVE_RETURN
    report_reason=fresh_prompt
    if test "$native_op" = absent; then report_kind=NATIVE_ABSENT;report_reason=source_open_failed; fi
    PHASE=native_$native_op
    guard && hash_is "$NATIVE" "$NATIVE_SHA" || fail pre_native_guard
    native_inflight=1
    "$NATIVE" "$native_op" "$native_path" > "$LOCK/native.current.log" 2>&1 &
    native_pid=$!
    manifest || fail manifest_native
    wait "$native_pid"
    native_rc=$?
    NATIVE_LAST_STATUS=$native_rc
    native_inflight=0
    "$BB" cat "$LOCK/native.current.log"
    "$BB" cat "$LOCK/native.current.log" >> "$LOCK/native.log" || { NATIVE_UNCERTAIN=1;retain=1;fail native_log 21; }
    if test "$native_rc" = 21 || "$BB" grep -q 'uncertain=1' "$LOCK/native.current.log"; then
        NATIVE_UNCERTAIN=1;retain=1;fail native_uncertain 21
    fi
    if test "$native_rc" != 0; then
        if native_safe_presend_failure; then fail native_presend "$native_rc"; fi
        NATIVE_UNCERTAIN=1;retain=1;fail native_completion_unconfirmed 21
    fi
    if ! native_report_ok; then NATIVE_UNCERTAIN=1;retain=1;fail native_return_unconfirmed 21; fi
    guard || fail post_native_guard
}
decode_input() {
    PHASE=$1
    decode_failure=$2
    guard && hash_is "$WORKER" "$WORKER_SHA" || fail verify_worker_pin
    worker_inflight=1
    "$WORKER" --verify "$INPUT" > "$LOCK/verify.log" 2>&1 &
    worker_pid=$!
    manifest || fail manifest_verify
    wait "$worker_pid"
    worker_rc=$?
    worker_inflight=0
    "$BB" cat "$LOCK/verify.log"
    test "$worker_rc" = 0 && "$BB" grep -q '^FILM_DECODED ' "$LOCK/verify.log" || fail "$decode_failure"
}
release_input() {
    guard && test -f "$INPUT" && test ! -L "$INPUT" || fail release_input_guard
    "$BB" rm -f "$INPUT" || fail release_input
    test ! -e "$INPUT" && test ! -L "$INPUT" || fail release_input
}
read_settings() {
    hash_is "$SETTINGS" "$SETTINGS_SHA" || return 11
    SETTING_LINE=$("$SETTINGS")
    setting_rc=$?
    if test "$setting_rc" = 10 && test "$SETTING_LINE" = FILM_OFF; then return 10; fi
    test "$setting_rc" = 0 || return 11
    set -- $SETTING_LINE
    test "$#" = 4 && test "$1" = FILM_JOB || return 11
    setting_preset=${2#preset=}
    setting_grain=${3#grain=}
    setting_halation=${4#halation=}
    test "$SETTING_LINE" = "FILM_JOB preset=$setting_preset grain=$setting_grain halation=$setting_halation" || return 11
    case "$setting_preset" in portra_400|cinestill_800t|fuji_provia|kodak_gold|tri_x_400) ;; *) return 11;; esac
    case "$setting_grain:$setting_halation" in [0-3]:[01]) ;; *) return 11;; esac
    return 0
}
confirm_settings() {
    read_settings
    confirm_rc=$?
    if test "$confirm_rc" != 0; then fail "$1" "$confirm_rc"; fi
    test "$SETTING_LINE" = "$INITIAL_PLAN" || fail "$1-changed" 12
}
memory_budget() {
    test -r "$MEMINFO" || return 1
    mem_seen=0
    mem_bad=0
    mem_free=0
    mem_buffers=0
    mem_cached=0
    mem_shmem=0
    while read -r mem_key mem_value mem_unit mem_rest; do
        case "$mem_key" in MemFree:) mem_bit=1;; Buffers:) mem_bit=2;; Cached:) mem_bit=4;; Shmem:) mem_bit=8;; *) continue;; esac
        case "$mem_value" in ''|*[!0-9]*) mem_bad=1; continue;; esac
        if test "${#mem_value}" -gt 9 || test "$mem_unit" != kB || test "$((mem_seen & mem_bit))" != 0; then
            mem_bad=1;continue
        fi
        mem_seen=$((mem_seen | mem_bit))
        case "$mem_key" in MemFree:) mem_free=$mem_value;; Buffers:) mem_buffers=$mem_value;; Cached:) mem_cached=$mem_value;; Shmem:) mem_shmem=$mem_value;; esac
    done < "$MEMINFO"
    test "$mem_bad" = 0 && test "$mem_seen" = 15 || return 1
    mem_available=$((mem_free+mem_buffers+mem_cached-mem_shmem))
    mem_required=$(((EXPECTED_BYTES+1023)/1024+${1:-16384}+16384))
    echo "RAM_JOB_MEMORY available_kib=$mem_available required_kib=$mem_required private_mount_bytes=33554432"
    test "$mem_available" -ge "$mem_required"
}

case "$MODE:$#" in
    render:6) PRESET=$4;GRAIN=$5;HALATION=$6;;
    apply:4) TARGET=$4;;
    *) fail arguments;;
esac
case "$EXPECTED_BYTES" in ''|0|0*|*[!0-9]*) fail expected_size;; esac
test "${#EXPECTED_BYTES}" -le 8 || fail expected_size
test "$((EXPECTED_BYTES+16777216+65536))" -le 33554432 || fail scratch_capacity
source_tail=${SOURCE#A:/DCIM/}
source_dir=${source_tail%%/*}
source_file=${source_tail#*/}
source_name=${source_file%.JPG}
test "${#source_dir}" = 8 && test "${#source_name}" = 8 &&
    test "$SOURCE" = "A:/DCIM/$source_dir/$source_name.JPG" || fail source_path
case "$source_dir$source_name" in *[!A-Za-z0-9_-]*) fail source_path;; esac
if test "$MODE" = render; then
    case "$PRESET" in portra_400|cinestill_800t|fuji_provia|kodak_gold|tri_x_400) ;; *) fail preset;; esac
    case "$GRAIN:$HALATION" in [0-3]:[01]) ;; *) fail effect_arguments;; esac
else
    target_digits=${TARGET#A:/A6400/TEST/F}
    target_digits=${target_digits%.JPG}
    test "${#target_digits}" = 7 && test "$TARGET" = "A:/A6400/TEST/F$target_digits.JPG" || fail target_path
    case "$target_digits" in *[!0-9]*) fail target_path;; esac
fi
test -x "$NATIVE" && hash_is "$NATIVE" "$NATIVE_SHA" || fail native_pin
test -x "$WORKER" && hash_is "$WORKER" "$WORKER_SHA" || fail worker_pin
if test "$MODE" = apply; then
    read_settings
    initial_rc=$?
    if test "$initial_rc" != 0; then fail settings_initial "$initial_rc"; fi
    INITIAL_PLAN=$SETTING_LINE
    PRESET=$setting_preset;GRAIN=$setting_grain;HALATION=$setting_halation
fi
memory_budget || fail memory_budget
no_linux_sd || fail linux_sd_mounted
IFS= read -r BOOT < "$BOOTFILE" || fail boot_id
test "${#BOOT}" = 36 || fail boot_id
case "$BOOT" in *[!0-9a-f-]*) fail boot_id;; esac
test ! -e "$RAM" && test ! -L "$RAM" || fail existing_ram_directory
test ! -e "$LOCK" && test ! -L "$LOCK" || fail existing_lock
"$BB" mkdir "$LOCK" || fail lock
trap finish 0
trap '' HUP
trap interrupted INT TERM
PHASE=mount
manifest || fail manifest
"$BB" mkdir "$RAM" || fail ram_directory
ram_created=1
"$BB" mount -t tmpfs -o size=33554432,nr_inodes=16,mode=700,nosuid,nodev,noexec a6400-film-job "$RAM"
mount_rc=$?
ram_mount_state
mount_state=$?
if test "$mount_state" = 2; then retain=1;fail mount_unconfirmed 21; fi
test "$mount_rc" = 0 && test "$mount_state" = 0 || fail mount

native_run read "$SOURCE"
file_bytes "$INPUT" && test "$FILE_BYTES" = "$EXPECTED_BYTES" || fail source_size
hash_file "$INPUT" || fail source_hash
INPUT_SHA=$HASH

decode_input verify_input input_decode
if test "$MODE" = apply; then confirm_settings settings_before_processing; fi

PHASE=render
guard && hash_is "$WORKER" "$WORKER_SHA" || fail pre_render_guard
manifest || fail manifest_render
echo "RAM_JOB_RENDER_START preset=$PRESET grain=$GRAIN halation=$HALATION"
worker_inflight=1
"$WORKER" "$PRESET" "$INPUT" "$OUTPUT" "$GRAIN" "$HALATION" > "$LOCK/render.log" 2>&1 &
worker_pid=$!
manifest || fail manifest_worker
wait "$worker_pid"
worker_rc=$?
worker_inflight=0
"$BB" cat "$LOCK/render.log"
test "$worker_rc" = 0 && "$BB" grep -q '^FILM_DONE ' "$LOCK/render.log" || fail render
hash_is "$INPUT" "$INPUT_SHA" || fail input_changed
file_bytes "$OUTPUT" && test "$FILE_BYTES" -gt 0 && test "$FILE_BYTES" -le 16777216 || fail output_size
OUTPUT_BYTES=$FILE_BYTES
hash_file "$OUTPUT" || fail output_hash
OUTPUT_SHA=$HASH
guard || fail post_render_guard
if test "$MODE" = apply; then
    confirm_settings settings_after_render
    PHASE=release_original_scratch
    manifest || fail manifest_rendered
    # Keep the original hash/size in the manifest, then free only our own input.
    hash_is "$INPUT" "$INPUT_SHA" || fail input_changed
    release_input
    native_run absent "$TARGET"
    test ! -e "$INPUT" && test ! -L "$INPUT" || { retain=1;fail absence_scratch_unconfirmed 21; }
    confirm_settings settings_before_write
    guard && file_bytes "$OUTPUT" && test "$FILE_BYTES" = "$OUTPUT_BYTES" &&
        hash_is "$OUTPUT" "$OUTPUT_SHA" || fail pre_write_output
    TARGET_ATTEMPTED=1
    retain=1
    # absent is a fresh nonexistence check, not atomic exclusive-create.
    # Only this locked coordinator may write this dedicated target namespace.
    native_run write "$TARGET"
    native_run flush "$TARGET"
    PHASE=release_output_scratch
    hash_is "$OUTPUT" "$OUTPUT_SHA" || fail output_changed
    manifest || fail manifest_flushed
    "$BB" rm -f "$OUTPUT" || fail release_output
    test ! -e "$OUTPUT" && test ! -L "$OUTPUT" || fail release_output
    native_run readback "$TARGET"
    file_bytes "$INPUT" && test "$FILE_BYTES" = "$OUTPUT_BYTES" || fail target_readback_size
    READBACK_BYTES=$FILE_BYTES
    hash_file "$INPUT" || fail target_readback_hash
    READBACK_SHA=$HASH
    test "$READBACK_SHA" = "$OUTPUT_SHA" || fail target_readback_hash
    decode_input verify_readback target_readback_decode
    TARGET_VERIFIED=1
    PHASE=verified_target
    manifest || fail manifest_verified_target
    release_input
    memory_budget 0 || fail original_readback_memory
    native_run read "$SOURCE"
    file_bytes "$INPUT" && test "$FILE_BYTES" = "$EXPECTED_BYTES" &&
        hash_is "$INPUT" "$INPUT_SHA" || fail original_readback
    ORIGINAL_VERIFIED=1
    PHASE=applied
    manifest || fail manifest_applied
    guard || fail final_guard
    retain=0
    applied=1
    exit 0
fi
PHASE=rendered
retain=1
echo "RAM_JOB_RENDERED input_sha256=$INPUT_SHA output_sha256=$OUTPUT_SHA output_bytes=$OUTPUT_BYTES"
exit 0
