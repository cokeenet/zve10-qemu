#!/usr/bin/env python3
"""盘点相机内部存储，标注每一项的归属（原厂 / 我们 / 未知）。

用法:
    python3 tools/zve10/inspect/inventory.py            # 全部
    python3 tools/zve10/inspect/inventory.py --garbage  # 只看可删的
    python3 tools/zve10/inspect/inventory.py --json out.json
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'fat'))
sys.path.insert(0, str(HERE.parent / 'dev'))

import fatio  # noqa: E402

# 原厂文件清单（厂商固件里就有，绝不能删）
FACTORY_SETTING = {
    'Backup.bin', 'Backup.bak', 'DmmConfig.bin', 'psd.bin', 'facerec0.bin',
    'facedmy.bin', 'ulogio.bin', 'kemco.txt', '.hasp', '.etools', 'zsd',
    'updater', 'appfw_infra', 'mode', 'gps', 'zvegisk',
}
FACTORY_LOG = {
    'AccessLog.dat', 'AccessLog.bak', 'ThreadMonitor.bin', 'camfs.log',
    'cleanup_log.txt', 'p1', 'part.bin',
}

# 我们自己建的目录（整棵树都算我们的）
OURS_DIRS_LOG = {'_bt', 'zv1f_ui'}

# 我们自己的产物
OURS_LOG = {
    'zvegisk.so', 'zvegisk.so.new', 'zvegisk.so.new.orig', 'zvegisk.log',
    'net_agent4.so', 'agent_v14.so', 'zve10_agent.log', 'autonet.so',
    'pl.hex', 'touchlog.so', 'boot_audit.txt', 'wifi_diag.txt', 'step5.sh',
    'hooks.txt', 'preload_hex.txt', 'probe_io.log', 'ldtest.txt', 'patch.log',
    'unbind_result.txt', 'dump1.log', 'dump2.log', 'dump3.log',
    '.zve10_patch_result', 'libfix_mark.txt', 'im_manual.log',
    'libjit_mark.txt', 'ev.err', 'zve10_install.log', 'ld.so.preload.bak',
    '.zve10_out', 'zvping.log', 'dat4.bak', 'zve10_net.on', 'cc_byte.bak',
    'CautionConfig.bak', 't.txt', 'wpa.out', 'gotdump.txt', 'zvloop.log',
    'zve10_net.log', 'bss.conf.bak', 'bss.conf.new', 'agent_diag.txt',
    'wpa_probe.so', 'wpa_probe.log', 'wpa_probe2.log', 'wpactl', 'wpactl3',
    'wpactl.orig', 'wpa_auto.conf', 'gotdump.so', 'zvegisk_loader',
    'zvegisk_loader2', 'zvegisk_devprobe', 'zvegisk_sdprobe',
    'zvegisk_sdwrite', 'zvegisk_rwtest', 'zvegisk_memprobe',
    'zvegisk_memwrite', 'zvegisk_patch', 'zvegisk_pcsample',
    'zvegisk_hooktool', 'zvegisk_stream', 'zvegisk_stream2', 'zvegisk_faces',
    'zvegisk_dumpface', 'zvegisk_ringdiff', 'zvegisk_rd2', 'zvegisk_rd3',
    'zvegisk_hook2tool', 'zvegisk_count', 'zvegisk_downdiff',
    'zvegisk_testload', 'zvegisk_gpumap', 'vcd.bin', 'r.bin', 'r.bin.orig',
    'part.bin.orig', 't13.img', 'zsnap.bin', 'face_a.bin', 'face_b.bin',
    'face_c.bin', 'libObj.fix', 'libObj.so.orig', 'wpactl.orig',
    'zvegisk_sdwrite.orig', 'zvegisk_gpumap.orig',
    'zvegisk_pcsample.orig', 'zvegisk_loader.orig', 'credscan.log',
}
OURS_SETTING = {
    'agent_diag.txt', 'unbind_result.txt', 'preload_probe.txt',
    'libfix_mark.txt', 'libjit_mark.txt', 'zvping.log', 'zvegisk_fix.log',
    'probe_io.log',
}


def classify(part, name, attr, size):
    is_dir = bool(attr & fatio.DIRENT)
    if part == 'nflasha2':
        if name in FACTORY_SETTING:
            return 'factory'
        if name in OURS_SETTING:
            return 'ours'
        return 'unknown'
    if part == 'nflasha11':
        if name in OURS_DIRS_LOG and is_dir:
            return 'ours'
        if name in OURS_LOG:
            return 'ours'
        if name in FACTORY_LOG:
            return 'factory'
        return 'unknown'
    return 'unknown'


def scan(part):
    v = fatio.Volume(part)
    rows = []
    for e in v.listdir():
        kind = classify(part, e['name'], e['attr'], e['size'])
        rows.append(dict(
            part=part, name=e['name'], dir=bool(e['attr'] & fatio.DIRENT),
            size=e['size'], cluster=e['cluster'], kind=kind,
        ))
    return v, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--garbage', action='store_true', help='只列出可删项')
    ap.add_argument('--json')
    args = ap.parse_args()

    out = {}
    for part in ('nflasha2', 'nflasha11'):
        v, rows = scan(part)
        out[part] = dict(fstype=v.fstype, cluster_bytes=v.cluster_bytes, items=rows)

        if args.garbage:
            rows = [r for r in rows if r['kind'] == 'ours']

        label = {'nflasha2': '/setting', 'nflasha11': '/log'}[part]
        total = sum(r['size'] for r in rows if not r['dir'])
        print('\n=== %s (%s, 簇 %d B) ===' % (label, v.fstype, v.cluster_bytes))
        print('  %-40s %10s  %s' % ('名称', '大小', '归属'))
        print('  ' + '-' * 66)
        for r in sorted(rows, key=lambda x: (x['kind'] != 'ours', -x['size'])):
            mark = {'factory': '原厂', 'ours': '★我们', 'unknown': '?'}[r['kind']]
            print('  %-40s %10d  %s' % (r['name'] + ('/' if r['dir'] else ''), r['size'], mark))
        print('  ' + '-' * 66)
        print('  合计 %d 项，%d B (%.2f MB)' % (len(rows), total, total / 1048576))

    if args.json:
        Path(args.json).write_text(json.dumps(out, ensure_ascii=False, indent=1))
        print('\n已写入 %s' % args.json)

    # 汇总
    print('\n=== 可删除汇总 ===')
    grand = 0
    for part, d in out.items():
        ours = [i for i in d['items'] if i['kind'] == 'ours' and not i['dir']]
        s = sum(i['size'] for i in ours)
        grand += s
        print('  %s: %d 项, %d B (%.2f MB)' % (part, len(ours), s, s / 1048576))
    print('  总计: %.2f MB' % (grand / 1048576))


if __name__ == '__main__':
    main()
