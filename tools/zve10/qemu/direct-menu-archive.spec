dir /dev 0755 0 0
dir /proc 0755 0 0
dir /bin 0755 0 0
dir /sbin 0755 0 0
dir /etc 0755 0 0
nod /dev/console 0600 0 0 c 5 1
nod /dev/ttyAM0 0600 0 0 c 204 16
file /init init 0755 0 0
file /sbin/init deny 0755 0 0
file /etc/init deny 0755 0 0
file /bin/init deny 0755 0 0
file /bin/sh deny 0755 0 0
file /sbin/modprobe deny 0755 0 0
file /sbin/hotplug deny 0755 0 0
