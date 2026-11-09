#!/bin/sh
P=$(/bin/busybox pidof im.elf)
echo "PID=$P"
echo "=== VERIFY ==="
printf "06517ca0 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106003616 count=2 2>/dev/null | /bin/busybox xxd -p
printf "06517cd6 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106003670 count=2 2>/dev/null | /bin/busybox xxd -p
printf "065257b4 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106059700 count=2 2>/dev/null | /bin/busybox xxd -p
printf "06534da0 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106122656 count=2 2>/dev/null | /bin/busybox xxd -p
printf "0653504a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106123338 count=2 2>/dev/null | /bin/busybox xxd -p
printf "065350ea "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106123498 count=2 2>/dev/null | /bin/busybox xxd -p
printf "065353b2 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106124210 count=2 2>/dev/null | /bin/busybox xxd -p
printf "06531e0a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106110474 count=4 2>/dev/null | /bin/busybox xxd -p
printf "06531e38 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106110520 count=4 2>/dev/null | /bin/busybox xxd -p
printf "065328ee "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106113262 count=4 2>/dev/null | /bin/busybox xxd -p
printf "06539b60 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106142560 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0653ce64 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106155620 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0653dac6 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106158790 count=4 2>/dev/null | /bin/busybox xxd -p
printf "06537388 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106132360 count=2 2>/dev/null | /bin/busybox xxd -p
printf "065373a0 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106132384 count=2 2>/dev/null | /bin/busybox xxd -p
printf "065373c4 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106132420 count=2 2>/dev/null | /bin/busybox xxd -p
printf "06531b3a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106109754 count=2 2>/dev/null | /bin/busybox xxd -p
printf "06531b4a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106109770 count=2 2>/dev/null | /bin/busybox xxd -p
printf "06bddbea "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=113105898 count=2 2>/dev/null | /bin/busybox xxd -p
printf "06535020 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106123296 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0653502c "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=106123308 count=2 2>/dev/null | /bin/busybox xxd -p
printf "063d7a4c "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=104692300 count=4 2>/dev/null | /bin/busybox xxd -p
printf "068ce672 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109897330 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068cecbc "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109898940 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068cf136 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109900086 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068cf65e "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109901406 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068cfb6e "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109902702 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068cff3e "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109903678 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068d03ca "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109904842 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068d0904 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109906180 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068d0ccc "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109907148 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068d1008 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109907976 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068d13b2 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109908914 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068d174c "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109909836 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068d1a3c "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109910588 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068d1d50 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109911376 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068d2078 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109912184 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068d268a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109913738 count=8 2>/dev/null | /bin/busybox xxd -p
printf "068d64da "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109929690 count=8 2>/dev/null | /bin/busybox xxd -p
printf "054a5fd6 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=88760278 count=2 2>/dev/null | /bin/busybox xxd -p
printf "07c892e6 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=130585318 count=4 2>/dev/null | /bin/busybox xxd -p
printf "07feb36a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=134132586 count=4 2>/dev/null | /bin/busybox xxd -p
printf "07b38e18 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=129207832 count=4 2>/dev/null | /bin/busybox xxd -p
printf "07b3d42c "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=129225772 count=4 2>/dev/null | /bin/busybox xxd -p
printf "07b3d730 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=129226544 count=4 2>/dev/null | /bin/busybox xxd -p
printf "07feb6b4 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=134133428 count=4 2>/dev/null | /bin/busybox xxd -p
printf "07eb1d48 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=132848968 count=4 2>/dev/null | /bin/busybox xxd -p
printf "07eb1d94 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=132849044 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0802660e "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=134374926 count=4 2>/dev/null | /bin/busybox xxd -p
printf "068c1d78 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109845880 count=4 2>/dev/null | /bin/busybox xxd -p
printf "068c1d9c "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109845916 count=4 2>/dev/null | /bin/busybox xxd -p
printf "068c2896 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=109848726 count=4 2>/dev/null | /bin/busybox xxd -p
printf "07d7f600 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=131593728 count=4 2>/dev/null | /bin/busybox xxd -p
printf "079ae1c4 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=127590852 count=4 2>/dev/null | /bin/busybox xxd -p
printf "079ae306 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=127591174 count=4 2>/dev/null | /bin/busybox xxd -p
printf "079ae31e "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=127591198 count=4 2>/dev/null | /bin/busybox xxd -p
printf "079c22ac "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=127673004 count=4 2>/dev/null | /bin/busybox xxd -p
printf "07f43e18 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=133447192 count=6 2>/dev/null | /bin/busybox xxd -p
printf "07f43eee "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=133447406 count=6 2>/dev/null | /bin/busybox xxd -p
printf "07f43f5a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=133447514 count=6 2>/dev/null | /bin/busybox xxd -p
printf "07f46022 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=133455906 count=6 2>/dev/null | /bin/busybox xxd -p
printf "07f46152 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=133456210 count=6 2>/dev/null | /bin/busybox xxd -p
printf "07f462ac "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=133456556 count=6 2>/dev/null | /bin/busybox xxd -p
printf "07f463f2 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=133456882 count=6 2>/dev/null | /bin/busybox xxd -p
printf "07f4735e "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=133460830 count=6 2>/dev/null | /bin/busybox xxd -p
printf "07f477a8 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=133461928 count=8 2>/dev/null | /bin/busybox xxd -p
printf "07edf1d6 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=133034454 count=6 2>/dev/null | /bin/busybox xxd -p
printf "07cd0100 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=130875648 count=4 2>/dev/null | /bin/busybox xxd -p
printf "080aca0c "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=134924812 count=6 2>/dev/null | /bin/busybox xxd -p
printf "0806fec4 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=134676164 count=2 2>/dev/null | /bin/busybox xxd -p
printf "079d4b30 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=127748912 count=2 2>/dev/null | /bin/busybox xxd -p
printf "08026784 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=134375300 count=2 2>/dev/null | /bin/busybox xxd -p
printf "080476a6 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=134510246 count=2 2>/dev/null | /bin/busybox xxd -p
printf "080477a4 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=134510500 count=4 2>/dev/null | /bin/busybox xxd -p
printf "07f37b3e "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=133397310 count=4 2>/dev/null | /bin/busybox xxd -p
printf "07f3f8cc "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=133429452 count=4 2>/dev/null | /bin/busybox xxd -p
printf "080f3c30 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=135216176 count=8 2>/dev/null | /bin/busybox xxd -p
printf "080f3c3c "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=135216188 count=8 2>/dev/null | /bin/busybox xxd -p
printf "083918f6 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137959670 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839198e "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137959822 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08391a26 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137959974 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08391abe "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137960126 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08392736 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137963318 count=4 2>/dev/null | /bin/busybox xxd -p
printf "083927ce "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137963470 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08392866 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137963622 count=4 2>/dev/null | /bin/busybox xxd -p
printf "083928fe "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137963774 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08392c8e "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137964686 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08392d26 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137964838 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08392dbe "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137964990 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08392e56 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137965142 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839379a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137967514 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08393832 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137967666 count=4 2>/dev/null | /bin/busybox xxd -p
printf "083938ca "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137967818 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08393962 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137967970 count=4 2>/dev/null | /bin/busybox xxd -p
printf "083939fa "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137968122 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08393a92 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137968274 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08393b2a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137968426 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08393bc2 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137968578 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08393c5a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137968730 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08393cf2 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137968882 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08393d8a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137969034 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08393e22 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137969186 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08393eba "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137969338 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08393f52 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137969490 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08393fea "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137969642 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08394082 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137969794 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839411a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137969946 count=4 2>/dev/null | /bin/busybox xxd -p
printf "083941b2 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137970098 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839424a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137970250 count=4 2>/dev/null | /bin/busybox xxd -p
printf "083942e2 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137970402 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839437a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137970554 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08394412 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137970706 count=4 2>/dev/null | /bin/busybox xxd -p
printf "083944aa "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137970858 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08394542 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137971010 count=4 2>/dev/null | /bin/busybox xxd -p
printf "083977ba "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137983930 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08397852 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137984082 count=4 2>/dev/null | /bin/busybox xxd -p
printf "083978ea "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137984234 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08397982 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137984386 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08397a1a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137984538 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08397ab2 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137984690 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839898a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137988490 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08398a22 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137988642 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08398aba "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137988794 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08398b52 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137988946 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08398bea "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137989098 count=4 2>/dev/null | /bin/busybox xxd -p
printf "08398c82 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=137989250 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839b93a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=138000698 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839b9d2 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=138000850 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839ba6a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=138001002 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839bb02 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=138001154 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839c18a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=138002826 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839c222 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=138002978 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839c2ba "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=138003130 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839c352 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=138003282 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839c64a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=138004042 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839c6e2 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=138004194 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839c77a "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=138004346 count=4 2>/dev/null | /bin/busybox xxd -p
printf "0839c812 "
/bin/busybox dd if=/proc/$P/mem bs=1 skip=138004498 count=4 2>/dev/null | /bin/busybox xxd -p
echo "=== END ==="
