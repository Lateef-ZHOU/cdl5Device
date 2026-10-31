# cdl5Device 维护入口

2026-10-31 用户定：新建本地采集卡官邸仓库，本轮仅迁入 DMA 连接资源层并整理 UDP / DMA 接入。完整采集、三通道处理和 QML 页面后续逐项接入；不推送、不接入主程序、不改板卡服务、镜像或 eMMC。

- 开始先核对 Git 状态，再读 README 与 VALIDATION。中文沟通和提交，不用 friend，不加状态缓存或薄转发。
- UDP 直接用 HandleCore 的 PcapHandle；CMake 提供 Lidar::Cdl5Udp，不新写重复 Handle 类。
- DMA 为 Linux 可选目标 Lidar::Cdl5Dma；目前 DmaHandle 仍为抽象连接资源层，不使用空实现冒充完整设备。
- HandleCore 固定远端完整提交；共享 ioctl 头从 DmaProxy 显式引用。驱动和 ABI 只有一份，不复制进本仓。
- 两个模块默认关闭，按需启用。Windows 不查找 DMA 头、驱动或 /dev/mem；显式请求 DMA 应在配置时失败。
- 保持只读打开 DMA、寄存器映射与失败回收、HandleCore 连接/重连/同步停止行为。迁入文件原有 GPL-2.0 标记保留，发布及整体许可后续确定。
- 后续已定：一台设备、三 ADC 独立谱/径向风/CNR/风廓线/保存；32 字节记录对齐、每块最多 1024 字节、尾块实际长度；切块归 DMA Handle，Bridge 原样转发。当前尚未实现这些采集步骤。
- 未来采集需保持 DMA 准备先于 FPGA 触发；取消需要结合 FPGA 的帧边界停止行为，不能把 RESET 当作 FPGA 复位。
- 验证：Windows UDP、Linux UDP+DMA 与 DMA 单独构建；DMA 系统调用包装测试及 ARM 测试都不触碰真实硬件。没有 QML 页面时不伪造 UI 冒烟结果。
