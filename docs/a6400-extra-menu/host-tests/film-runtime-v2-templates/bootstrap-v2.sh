#!/bin/sh
# Cold-boot constructor only. Never invoke this from a running shooting camera.
set -uf
umask 077
export PATH=/usr/bin:/bin:/sbin:/usr/sbin
BB=/bin/busybox
RAM=/tmp_bt
MOUNT=/mnt
MOUNTS=/proc/mounts
UPTIME=/proc/uptime
CAMERA_FDS=/proc/149/fd
AELOCK=$RAM/ae.lock
PREPARE=$MOUNT/A6400/RUNTIME/prepare.sh
RUNTIME=$RAM/a6400-film-runtime-start.sh
mounted=0
fail() { echo "FILM_BOOT_FAIL stage=$1"; exit 1; }
hash_is() {
    test -f "$1" && test ! -L "$1" || return 1
    hash_line=$("$BB" sha256sum "$1") || return 1
    test "${hash_line%% *}" = "$2"
}
disabled() {
    test -e /tmp/disable_hook || test -e "$RAM/a6400-film.DISABLE" ||
        test -e "$RAM/a6400-film-capture.stop"
}
early_boot() {
    test -e "$AELOCK" && test ! -L "$AELOCK" || return 1
    read -r uptime rest < "$UPTIME" || return 1
    seconds=${uptime%%.*}
    case "$seconds" in ''|*[!0-9]*) return 1;; esac
    test "${#seconds}" -le 3 && test "$seconds" -le 30 || return 1
    # The author's userspace FS owns mmca once open. Refuse to mount it then.
    # Expand this fixed proc glob deliberately; all other shell globbing stays off.
    if test -d "$CAMERA_FDS"; then test -r "$CAMERA_FDS" && test -x "$CAMERA_FDS" || return 1; fi
    set +f
    set -- "$CAMERA_FDS"/*
    set -f
    for fd do
        test -L "$fd" || continue
        fd_target=$("$BB" readlink "$fd") || return 1
        case "$fd_target" in /dev/mmca*|/dev/mmcblk*) return 1;; esac
    done
    return 0
}
clear_mounts() {
    test -r "$MOUNTS" || return 1
    while read -r source target type options rest; do
        test "$target" != "$MOUNT" || return 1
        case "$source" in /dev/mmca*|/dev/mmcblk*) return 1;; esac
    done < "$MOUNTS"
    return 0
}
own_ro_mount() {
    owned_count=0
    while read -r source target type options rest; do
        if test "$target" = "$MOUNT"; then
            test "$source" = /dev/mmca1 && test "$type" = vfat || return 1
            case ",$options," in *,ro,*) owned_count=$((owned_count+1));; *) return 1;; esac
        fi
    done < "$MOUNTS"
    test "$owned_count" = 1
}
cleanup() {
    if test "$mounted" = 1; then
        if own_ro_mount; then "$BB" umount "$MOUNT" || echo FILM_BOOT_RETAINED_MOUNT; fi
    fi
}
trap cleanup 0
trap 'exit 10' HUP INT TERM
if disabled; then echo FILM_BOOT_DISABLED;exit 0;fi
early_boot && clear_mounts || fail not_early_exclusive_boot
test -d "$MOUNT" && test ! -L "$MOUNT" || fail mountpoint
attempt=0
while test "$attempt" -lt 10; do
    ! disabled && early_boot && clear_mounts || fail mount_gate
    if "$BB" mount -t vfat -o ro /dev/mmca1 "$MOUNT"; then mounted=1;break;fi
    attempt=$((attempt+1))
    "$BB" sleep 1
done
test "$mounted" = 1 && own_ro_mount || fail sd_readonly_mount
if test -e "$MOUNT/disable_hook" || test -e "$MOUNT/disable.txt" || test -e "$MOUNT/A6400/DISABLE"; then
    echo FILM_BOOT_DISABLED
    exit 0
fi
hash_is "$PREPARE" @@PREPARE_SHA@@ || fail prepare_pin
/bin/sh "$PREPARE"
prepare_rc=$?
if test "$prepare_rc" = 10; then echo FILM_BOOT_DISABLED;exit 0;fi
test "$prepare_rc" = 0 || fail prepare
own_ro_mount || fail mount_owner_drift
"$BB" umount "$MOUNT" || fail unmount
mounted=0
clear_mounts || fail unmount_unconfirmed
! disabled && hash_is "$RUNTIME" @@RUNTIME_SHA@@ || fail runtime_pin
echo FILM_BOOT_STAGED_AND_UNMOUNTED
exec /bin/sh "$RUNTIME"
