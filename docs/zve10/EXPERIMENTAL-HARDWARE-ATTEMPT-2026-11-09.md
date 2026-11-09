# Authorized native presentation hardware deployment attempt

Owner explicitly authorized experimental-library replacement and one startup
verification. This attempt did not reach UI activation.

## Artifact and deployment

- Experimental artifact: 140152 bytes, MD5 6831935c8517f310fd636c9062a2aa54,
  SHA256 bc039e2e73432c33321be4ff46795f0f2b94955f9b32de5928ac8a13f3847b41.
- Uploaded to an absent staging path, read back exactly, then copied into a
  previously absent /usr/lib/libnewmenu_exp.so.
- Original /usr/lib/libnewmenu_usr.so remained intact, 135332 bytes, MD5
  40cc0e2f271dbbb13d3e6961017dd852.
- /usr returned to read-only after the copy. Whole-file host readback matched.
- An ordinary newly exec'd BusyBox with explicit LD_PRELOAD mapped the
  experimental file in four segments and returned status zero. This is a
  loader/constructor compatibility result for a non-UI process only.
- Preload list was temporarily switched to the experimental path, retaining
  /etc/zve10_agent.so. Previous list saved as /etc/ld.so.preload.before-exp.

## Exactly one reboot

The command `/bin/busybox sync; /sbin/reboot` was sent once. Its response timed
out; it was not resent. Subsequent bounded queries established a boot-ID change:

```
before 6d5414d1-9c5a-4277-a841-f9ad2fab71f3
after  ce0db914-14a2-4e0a-8d02-a4cb428ccda9
```

Post-boot observations:

- /proc/ssboot/state still reports snapshot.
- Experimental preload configuration and library checksum persisted.
- /usr remained read-only.
- PIDs 149/157 identified themselves as bootin.elf/im.elf.
- Their maps contained neither menu-library name nor the agent-library name.
- /log/newmenu.log remained zero bytes; no fallback log file was reported.

The result is **UI activation not achieved**. It neither validates nor falsifies
native presentation behavior because that code never ran in the observed UI
process. Snapshot restoration remains a supported explanation, not a per-task
provenance proof.

## Cleanup and final state

The preload file was restored atomically from the verified before-exp backup:

```
/etc/zve10_agent.so
/usr/lib/libnewmenu_usr.so
```

Host PULL verified these exact bytes. The stable library's checksum was checked
before rollback. The experimental file was left in place rather than overwriting
or deleting a file that might be mapped in a surviving ordinary process.

No second reboot, UI kill/restart, ptrace injection, guardian installation,
image_drop write or image regeneration was performed.

Complete private backups, preflight/post-boot responses, reboot request and result
manifest are under tools/zve10/backups/experimental-deploy-rf03ssb2/.

Next useful work must qualify a real fresh-exec lifecycle or a safe late-load
mechanism; repeating the same ordinary reboot is not justified by this result.
The hardware acceptance (row, ENTER, MENU) remains unfulfilled.
