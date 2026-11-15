# ZV-E10 移植用 API 表（从原厂 libObj.so cfdc59b9 精确提取）

## 一、18 个 API 的绝对偏移（libObj.so 内偏移）

```text
core::ID::genID(char const*)                      0x63cbbc
WidgetBase::setAnimate(bool)                      0x62cc08
WidgetSystem::createLayer(core::ID const&)        0x6171fc
WidgetSystem::sys_getInstance()                   0x616fe4
Layer::setClear(bool)                             0x61540c
Layer::setActive(bool)                            0x6153e2
Widget::setLayerID(core::ID const&)               0x6137c0
Widget::setLocalPos(core::Vector2 const&)         0x613710
Widget::Widget()                                  0x613860
LayerBase::moveLast()                             0x6275e8
GEN_Label::setTextDirect(char const*)             0x11573e
GEN_Label::setFont(unsigned)                      0x11581e
GEN_Label::setSize(float,float)                   0x1161f0
GEN_Label::GEN_Label()                            0x115bb0
GEN_Panel::setColorDirect(stella::CColor const&)  0x164388
GEN_Panel::setSize(float,float)                   0x164892
GEN_Panel::GEN_Panel()                            0x164450
WidgetSystem::getMouseState(u8,u8)                0x6172f8
WidgetSystem::getMousePosInFramebuffer(u8,short&,short&)  0x6172cc
```

## 二、InfraInput 注入点（我们比 A6400 多的优势）

```text
InfraInput::GetInstance()                         0x8c22a0
InfraInput::SetCallback(int*, void(*)(int,_InfraInputEvent*))  0x8c2824
InfraInput::RmCallback(int)                       0x8c189c
InfraInput::GetPhyKeyStatus(int, _InfraInputPhyKeyData*)       0x8c199c
InfraInput::GetEventStatus(int,int,_InfraInputEvent*)          0x8c2a00
InfraInput::SetEventKeyList(int,_InfraInputKeyList*,int)       0x8c3540
InfraInput::RmEventKeyList(int,_InfraInputKeyList*,int)        0x8c3454
InfraInput::ReleaseInstance()                     0x8c1810
InfraInput::SendEventInitData(int)                0x8c2b28
InfraInput::GetKeyInh(int)                        0x8c2d0e
InfraInput::SetKeyInh(int,bool)                   0x8c2e94
InfraInput::SetOsalId(int*,int)                   0x8c27b8
```

## 三、对象布局（反汇编实测，与 A6400 一致）

```text
GEN_Panel::GEN_Panel()  构造写到最大偏移 0x1f8
  · +0x000 vtable
  · +0x15c Sprite 构造（SpriteC1Ev）
  · +0x1ec / +0x1f0 / +0x1f4 / +0x1f8 四个清零
  · 之后 blx 0x10df80（Widget::initSprite，无 inline 写入）
  ⇒ 分配 0x200 字节足够（与 A6400 相同）

GEN_Label::GEN_Label()  构造写到最大偏移 0x374
  · +0x000 vtable
  · +0x160 core::ID 构造
  · +0x16c MultiLineTextSprite 构造
  · +0x2a8 LayoutAttributeWriter 构造
  · +0x2cc strb / +0x2d0 str.w / +0x2d4/+0x2d5/+0x2d6 strb / +0x2dc str.w
  · +0x374（另一个子对象）
  ⇒ 分配 0x400 字节足够（与 A6400 相同）
```

## 四、调用约定

```text
· 相机侧 libc/firmware 是 **AAPCS soft-float**（Flags 0x5000002）
  ⇒ 但 ARM EABI 的 float 参数在软浮点下走通用寄存器 r0/r1/r2/r3（不是 VFP 寄存器）
  ⇒ A6400 的 build 脚本用 `-mfloat-abi=hard` 编译但给 float 参数显式加
     __attribute__((pcs("aapcs")))，这一点我们必须照做
  · setSize 实测：r1→[r4+0x164]，r2→[r4+0x168]（两个 float 走 r1/r2）
    ⇒ **确认是整型寄存器传 float**，与 pcs("aapcs") 相符
```

## 五、还没确定、必须实测的三件事

```text
? WidgetSystem::sys_getInstance() 返回的单例 == createLayer 的第一个参数？
  （A6400 上两者相同，我们需确认）
? keys 单例（A6400 是 0x59150）在 ZV-E10 上是什么 —— 用 InfraInput::GetInstance 替代
? MENU / 放大镜 的 key 码是否仍是 0x1ab / 0x70dc
```
