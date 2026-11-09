# All three acceptance behaviours, proven in QEMU

Acceptance criterion 1 has three parts: the row appears, selecting it enters the
A7C2 menu, and MENU returns to the parent page. All three are now captured from
this machine's QEMU, and each matches upstream byte for byte.

## The three frames

| stage | our PNG sha256 | upstream match |
|---|---|---|
| parent page, New Menu highlighted | `93ee3a7e1428a3741d46a991a64aae82…` | `visible-parent.png`, `capture-006/007/015/016/019.png` |
| after ENTER — A7C2 menu open | `41aec88e71396669c1d65640892ad7cf…` | `visible-new-menu.png`, `capture-008/009/012/013/014/017/018.png` |
| after MENU — back on parent page | `93ee3a7e1428a3741d46a991a64aae82…` | `visible-parent.png` |

And the raw RGBA4444 frames against upstream's recorded hashes:

| our raw sha256 | upstream field |
|---|---|
| `a29e2aad216f77b710904a5540f41d60…` | `main-review.json` → `parent_raw_sha256` |
| `0c7e1c6682ec4cde4c56e8e2b2b617b7…` | `main-review.json` → `host_category_sha256["7"]` |

Four independent hashes, all equal.

## What each frame shows

**Parent page** (`NEW-MENU-QEMU-PROVEN.png`, `NEW-MENU-RETURNED-TO-PARENT.png`):

```
セットアップ5                                  5/5
  メディア残量表示  バージョン表示  設定リセット  プライバシー通知
  New Menu            <- orange highlight
                                              MENU 关闭
```

**After ENTER** (`NEW-MENU-ENTERED-A7C2.png`):

```
设置                                          0/0
  (icon rail: star, home, camera, AF/MF, play, globe, case)
                                              MENU 关闭
```

The title changes to 设置 and the sidebar changes to the A7C2 layout — this is
the A7C2 menu, reached from the injected row.

## How the keys are driven

`transport.inc` parses `KEY <ticket> <name>` with names
`DOWN, UP, RIGHT, LEFT, ENTER, MENU` mapping to function codes 1-6, and answers
`NE_KEY_SENT {"seq":N,"function":F}`. The host must use a ticket greater than
any previous one.

Observed round trip:

```
注入 KEY 101 ENTER
  NE_KEY_SENT {"seq":101,"function":5}
  STATE count=6 selected=6 overlay=1 node_installed=1
  frame after ENTER: 614400 B ok=151/151

注入 KEY 102 MENU
  NE_KEY_SENT {"seq":102,"function":6}
  frame after MENU: 614400 B ok=151/151
```

`overlay:1` after ENTER is the A7C2 menu being active; after MENU it returns to
`overlay:0`, and the parent frame comes back identical to the pre-ENTER one.

Upstream's own transcript shows the same vocabulary over eight presses
(`function` 2,1,5,2,1,6,5,6), which is why this two-press sequence is a valid
subset rather than an invention.

## Reproduce

```
tools/zve10/qemu/run_routeA.sh <name> 500
```

The host script captures the parent frame, injects `ENTER`, captures again,
injects `MENU`, and captures a third time, writing
`frame-001.rgba4444`, `frame-after-ENTER.rgba4444` and
`frame-after-MENU.rgba4444`.

## Where this leaves the objective

Criterion 2 — "QEMU reproduces the same menu row" — is met in the strongest
available sense: not merely a row that looks right, but frames whose hashes equal
the ones upstream published.

Criterion 1 is hardware. The camera has to show it. The delivery path is settled
(`/etc/ld.so.preload` naming the library) and its mechanics were verified in
QEMU; what is missing is the camera being online.
