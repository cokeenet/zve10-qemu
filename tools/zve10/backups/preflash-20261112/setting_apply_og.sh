#!/bin/sh
P=$(/bin/busybox pidof im.elf)
[ -z "$P" ] && echo NO_IM && exit 1
echo "PID=$P"
echo "=== RESTORE 06517ca0 ==="
/setting/mem_patch.elf $P 06517ca0 06d34868 2>&1 | /bin/busybox tail -1
echo "=== CHECK ==="
/setting/mem_patch.elf $P 06517ca0 00bf 2>&1 | /bin/busybox tail -1
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106003670 count=2 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "06d3" ]; then echo "OK   06517cd6 $C"; /setting/mem_patch.elf $P 06517cd6 00bf 2>&1 | /bin/busybox tail -1; else echo "SKIP 06517cd6 got=$C want=06d3"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106059700 count=2 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "06d3" ]; then echo "OK   065257b4 $C"; /setting/mem_patch.elf $P 065257b4 00bf 2>&1 | /bin/busybox tail -1; else echo "SKIP 065257b4 got=$C want=06d3"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106122656 count=2 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "08d3" ]; then echo "OK   06534da0 $C"; /setting/mem_patch.elf $P 06534da0 00bf 2>&1 | /bin/busybox tail -1; else echo "SKIP 06534da0 got=$C want=08d3"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106123338 count=2 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "03d3" ]; then echo "OK   0653504a $C"; /setting/mem_patch.elf $P 0653504a 00bf 2>&1 | /bin/busybox tail -1; else echo "SKIP 0653504a got=$C want=03d3"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106123498 count=2 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "06d3" ]; then echo "OK   065350ea $C"; /setting/mem_patch.elf $P 065350ea 00bf 2>&1 | /bin/busybox tail -1; else echo "SKIP 065350ea got=$C want=06d3"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106124210 count=2 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "03d3" ]; then echo "OK   065353b2 $C"; /setting/mem_patch.elf $P 065353b2 00bf 2>&1 | /bin/busybox tail -1; else echo "SKIP 065353b2 got=$C want=03d3"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106110474 count=4 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "97f8eb31" ]; then echo "OK   06531e0a $C"; /setting/mem_patch.elf $P 06531e0a 022300bf 2>&1 | /bin/busybox tail -1; else echo "SKIP 06531e0a got=$C want=97f8eb31"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106110520 count=4 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "97f8ea31" ]; then echo "OK   06531e38 $C"; /setting/mem_patch.elf $P 06531e38 022300bf 2>&1 | /bin/busybox tail -1; else echo "SKIP 06531e38 got=$C want=97f8ea31"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106113262 count=4 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "02f0cdfa" ]; then echo "OK   065328ee $C"; /setting/mem_patch.elf $P 065328ee 022000bf 2>&1 | /bin/busybox tail -1; else echo "SKIP 065328ee got=$C want=02f0cdfa"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106142560 count=4 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "fbf794f9" ]; then echo "OK   06539b60 $C"; /setting/mem_patch.elf $P 06539b60 022000bf 2>&1 | /bin/busybox tail -1; else echo "SKIP 06539b60 got=$C want=fbf794f9"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106155620 count=4 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "f8f7e0f9" ]; then echo "OK   0653ce64 $C"; /setting/mem_patch.elf $P 0653ce64 022000bf 2>&1 | /bin/busybox tail -1; else echo "SKIP 0653ce64 got=$C want=f8f7e0f9"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106158790 count=4 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "f7f7e1f9" ]; then echo "OK   0653dac6 $C"; /setting/mem_patch.elf $P 0653dac6 022000bf 2>&1 | /bin/busybox tail -1; else echo "SKIP 0653dac6 got=$C want=f7f7e1f9"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106132360 count=2 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "227e" ]; then echo "OK   06537388 $C"; /setting/mem_patch.elf $P 06537388 0222 2>&1 | /bin/busybox tail -1; else echo "SKIP 06537388 got=$C want=227e"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106132384 count=2 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "227e" ]; then echo "OK   065373a0 $C"; /setting/mem_patch.elf $P 065373a0 0222 2>&1 | /bin/busybox tail -1; else echo "SKIP 065373a0 got=$C want=227e"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106132420 count=2 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "237e" ]; then echo "OK   065373c4 $C"; /setting/mem_patch.elf $P 065373c4 0223 2>&1 | /bin/busybox tail -1; else echo "SKIP 065373c4 got=$C want=237e"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106109754 count=2 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "2b7e" ]; then echo "OK   06531b3a $C"; /setting/mem_patch.elf $P 06531b3a 0223 2>&1 | /bin/busybox tail -1; else echo "SKIP 06531b3a got=$C want=2b7e"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106109770 count=2 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "6b7e" ]; then echo "OK   06531b4a $C"; /setting/mem_patch.elf $P 06531b4a 0223 2>&1 | /bin/busybox tail -1; else echo "SKIP 06531b4a got=$C want=6b7e"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=113105898 count=2 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "0122" ]; then echo "OK   06bddbea $C"; /setting/mem_patch.elf $P 06bddbea 0222 2>&1 | /bin/busybox tail -1; else echo "SKIP 06bddbea got=$C want=0122"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106123296 count=4 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "4ff4a063" ]; then echo "OK   06535020 $C"; /setting/mem_patch.elf $P 06535020 4ff48763 2>&1 | /bin/busybox tail -1; else echo "SKIP 06535020 got=$C want=4ff4a063"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=106123308 count=2 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "a023" ]; then echo "OK   0653502c $C"; /setting/mem_patch.elf $P 0653502c b423 2>&1 | /bin/busybox tail -1; else echo "SKIP 0653502c got=$C want=a023"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=104692300 count=4 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "034b044a" ]; then echo "OK   063d7a4c $C"; /setting/mem_patch.elf $P 063d7a4c 01207047 2>&1 | /bin/busybox tail -1; else echo "SKIP 063d7a4c got=$C want=034b044a"; fi
/setting/mem_patch.elf $P 068ce672 b6f5406fc4f0d382 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068cecbc b6f5406f80f04081 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068cf136 b6f5406f80f08f81 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068cf65e b6f5406f80f05e81 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068cfb6e b6f5406fc3f05580 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068cff3e b6f5406f80f02681 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068d03ca b6f5406f80f02181 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068d0904 b6f5406f80f02981 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068d0ccc b6f5406f80f00481 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068d1008 b6f5406f80f00d81 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068d13b2 b6f5406f80f0df80 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068d174c b6f5406fc1f06682 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068d1a3c b6f5406fc1f0ee80 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068d1d50 b6f5406f80f01581 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068d2078 b6f5406fc0f0d085 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068d268a b6f5406f80f00481 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068d64da b6f5406f80f0b983 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 054a5fd6 00bf 2>&1 | /bin/busybox tail -1
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=130585318 count=4 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "12f02b0f" ]; then echo "OK   07c892e6 $C"; /setting/mem_patch.elf $P 07c892e6 12f02f0f 2>&1 | /bin/busybox tail -1; else echo "SKIP 07c892e6 got=$C want=12f02b0f"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=134132586 count=4 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "012804d1" ]; then echo "OK   07feb36a $C"; /setting/mem_patch.elf $P 07feb36a 032300bf 2>&1 | /bin/busybox tail -1; else echo "SKIP 07feb36a got=$C want=012804d1"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=129207832 count=4 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "4ff47062" ]; then echo "OK   07b38e18 $C"; /setting/mem_patch.elf $P 07b38e18 40f6a842 2>&1 | /bin/busybox tail -1; else echo "SKIP 07b38e18 got=$C want=4ff47062"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=129225772 count=4 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "4ff47063" ]; then echo "OK   07b3d42c $C"; /setting/mem_patch.elf $P 07b3d42c 40f6a843 2>&1 | /bin/busybox tail -1; else echo "SKIP 07b3d42c got=$C want=4ff47063"; fi
C=$(/bin/busybox dd if=/proc/$P/mem bs=1 skip=129226544 count=4 2>/dev/null | /bin/busybox xxd -p)
if [ "$C" = "4ff47062" ]; then echo "OK   07b3d730 $C"; /setting/mem_patch.elf $P 07b3d730 40f6a842 2>&1 | /bin/busybox tail -1; else echo "SKIP 07b3d730 got=$C want=4ff47062"; fi
/setting/mem_patch.elf $P 07feb6b4 40f6a843 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07eb1d48 40f6a843 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07eb1d94 40f6a843 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0802660e 40f6a843 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068c1d78 40f6a843 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068c1d9c 40f6a843 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 068c2896 40f6a843 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07d7f600 40f6a843 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 079ae1c4 40f6a843 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 079ae306 40f6a843 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 079ae31e 40f6a841 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 079c22ac 40f6a840 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07f43e18 13e000bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07f43eee 0ae000bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07f43f5a 00bf00bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07f46022 07e000bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07f46152 00bf00bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07f462ac 0ce000bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07f463f2 00bf00bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07f4735e 00bf00bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07f477a8 00bf00bf00bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07edf1d6 00bf00bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07cd0100 00bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 080aca0c 00bf00bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0806fec4 00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 079d4b30 00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08026784 00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 080476a6 00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 080477a4 00bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07f37b3e 00bf00bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 07f3f8cc b8e700bf 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 080f3c30 a80c700800000000 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 080f3c3c a80c700801000000 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 083918f6 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839198e a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08391a26 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08391abe a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08392736 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 083927ce a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08392866 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 083928fe a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08392c8e a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08392d26 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08392dbe a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08392e56 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839379a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08393832 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 083938ca a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08393962 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 083939fa a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08393a92 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08393b2a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08393bc2 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08393c5a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08393cf2 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08393d8a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08393e22 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08393eba a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08393f52 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08393fea a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08394082 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839411a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 083941b2 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839424a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 083942e2 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839437a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08394412 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 083944aa a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08394542 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 083977ba a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08397852 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 083978ea a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08397982 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08397a1a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08397ab2 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839898a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08398a22 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08398aba a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08398b52 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08398bea a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 08398c82 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839b93a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839b9d2 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839ba6a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839bb02 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839c18a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839c222 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839c2ba a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839c352 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839c64a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839c6e2 a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839c77a a80c0200 2>&1 | /bin/busybox tail -1
/setting/mem_patch.elf $P 0839c812 a80c0200 2>&1 | /bin/busybox tail -1
echo "=== DONE ==="
