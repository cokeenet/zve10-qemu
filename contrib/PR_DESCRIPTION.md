来自 ZV-1F 供体树的独立验证。这个 PR 只放工具、原始观测和补丁，**不对卡点原因下结论**。

## 交付内容

- `contrib/osal-uipc-analysis/` —— `osal_uipc.ko` cmd 0–16 语义表、LD_PRELOAD 探针 v1/v2（v2 带 TID / 时间戳 / qid）、诊断与重建脚本
- `contrib/gpe-ioctl-package/` —— `grm_gles.ko` 全 17 条 ioctl 逐条注解表，以及 GPE 设备模型补丁（含可直接合入的 `bionz.c` 代码）

## 观测记录

- guest 内核只激活 1 个 CPU（`Brought up 1 CPUs`）
- 探针一轮记录到 1447 行 uipc 调用日志
- 诊断快照里 `InfraOnDmdLdr` 与 `ACCESSLOG_MAIN` 停在 `osal_rcv_msg_tmo`，主线程 `sleep_on_page`
- 该轮观察窗口内 `appFw.so` 未被加载，UI 从未绘制

以上只陈述读数，不推断原因。

## 状态

QEMU 里 App 能启动、观察窗口内未见崩溃；显示链路没打通，原因未闭环。我们不再就卡点给结论。

## 说明

- 早期版本里的推断性结论已作废，本版不保留，也不再引用上游尚未定稿的结论。
- 本 PR 只含分析文本与我们自己写的代码，不含 Sony 固件、二进制或任何 dump。
- 感谢 upstream 在 DMM 协议、grm_ma 分派表和真机取证上的工作。
