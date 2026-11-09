# Camera /usr relocation — 2026-11-09

Owner requested moving the library to /usr and continuing. No reboot performed during this relocation.

## Observed changes

- Copied existing camera /etc/libnewmenu.so into a previously absent /usr/lib/libnewmenu_usr.so.
- Used the deployed 135332-byte build, MD5 40cc0e2f271dbbb13d3e6961017dd852; did not deploy the newer local logging build.
- Remounted /usr read-write only for the copy, then restored read-only. Final /proc/mounts: /dev/nflasha15 /usr ext2 ro,relatime,errors=continue.
- Camera cmp passed; host PULL of the complete destination was byte-identical to camera-libnewmenu.so in this backup directory.
- SHA256: 58360091d415bc68f0748aa31cf718cac286e108fe511e322de296e5d5392123.
- Backed up the immediately preceding preload configuration to /etc/ld.so.preload.before-usr-20261109.
- Replaced only the menu-library entry, preserving /etc/zve10_agent.so.

Final /etc/ld.so.preload bytes:

```
/etc/zve10_agent.so
/usr/lib/libnewmenu_usr.so
```

Host readback matched exactly. A newly exec'd BusyBox grep showed four mappings of /usr/lib/libnewmenu_usr.so on device fc:0f, inode 48777. Current /proc/157/maps still showed zero menu-library mappings.

Boot ID before and after deployment: 6d5414d1-9c5a-4277-a841-f9ad2fab71f3. No reboot occurred.

## Limits and next check

This establishes storage and loading for a newly exec'd utility on hardware. It does not establish stock-menu insertion, ENTER behavior, or MENU return in im.elf. A reboot or UI-process restart is not authorized by the relocation instruction alone; obtain the owner's explicit reboot approval before rebooting.

Earlier claims that late-process mappings prove late /etc mounting, or that PID 1 being launch_shell.elf proves failed bootin launch, are not established. Process starttime is not exec time; PID 1 can exec normally, and absence of a mapped library does not measure mount timing. Startup-image restore is an untested candidate given the recorded ssboot/warm boot parameters.

Rollback configuration is preserved on the camera and preload.before.txt stores its exact original bytes locally. Do not overwrite or delete a library while processes map it.
