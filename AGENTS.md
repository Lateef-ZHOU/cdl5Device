# cdl5Device 维护入口

2026-10-31 发布用户定：创建公开仓库 https://github.com/Lateef-ZHOU/cdl5Device 并推送现有修改；保留原有 GPL-2.0 等许可标记，整体许可后续整理，不自动改为 MIT。此前各阶段“不推送”只描述当时范围。运行逻辑与实机配置不因发布改变。

2026-10-31 最新用户定：UDP 业务端点改为板端 192.168.100.100、Windows 192.168.100.200，端口不变；不配置系统 IP、不改卸载设置，用于双端抓包对照。下方旧地址为历史记录。验证见 VALIDATION，仅本地提交。

2026-10-31 实测后用户定：Windows 按实际帧 450×128 接收，使用 Cdl5FiveKCollector。抓包帧头与只读寄存器均为 Profile 8、周期 50000（250 MHz 下为 5 kHz）；这说明当前活动数据规格，不代表已查明此前烧录历史。两规格均保留，板卡未修改。接收侧允许网卡合并后的 32 字节整记录批次，不把 DMA 的 1024 字节发送块长当作接收上限；仍核对帧头尺寸和剩余容量。

已实现 Linux DMA 采集端和 UDP 桌面官邸，Bridge 负责原样转发。Collector 顺序追加三路 FP32→double，不看逐元素 meta。当前先接三路原谱/保存；风速、CNR、风廓线待标定确认。Windows 主程序已接入该 DAQ 官邸。板端桥接服务此前已部署，本轮只更新 Windows 依赖与入口，不改板卡、镜像或 eMMC；不推送。

- 开始先核对 Git 状态，再读 README 与 VALIDATION。中文沟通和提交，不用 friend，不加状态缓存或薄转发。
- UDP 直接用 HandleCore 的 PcapHandle；CMake 提供 Lidar::Cdl5Udp，不新写重复 Handle 类。
- DMA 为 Linux 可选目标 Lidar::Cdl5Dma；DmaHandle 保留抽象连接资源层，Cdl5DmaHandle 为具体采集端；桌面不提供 DMA 官邸。
- HandleCore 固定远端完整提交；共享 ioctl 头从 DmaProxy 显式引用。驱动和 ABI 只有一份，不复制进本仓。
- 三个模块默认关闭，按需启用。Windows 不查找 DMA 头、驱动或 /dev/mem；显式请求 DMA 应在配置时失败。
- 保持只读打开 DMA、寄存器映射与失败回收、HandleCore 连接/重连/同步停止行为。迁入文件原有 GPL-2.0 标记保留，发布及整体许可后续确定。
- 后续已定：一台设备、三 ADC 独立谱/径向风/CNR/风廓线/保存；32 字节记录对齐、每块最多 1024 字节、尾块实际长度；切块归 DMA Handle，Bridge 原样转发。DMA/三路谱已实现，物理风速与风廓线后续接入。
- 未来采集需保持 DMA 准备先于 FPGA 触发；取消需要结合 FPGA 的帧边界停止行为，不能把 RESET 当作 FPGA 复位。
- 验证：Windows UDP、Linux UDP+DMA 与 DMA 单独构建；DMA 系统调用包装测试及 ARM 测试都不触碰真实硬件。独立页面需构建、单头、qmllint 和离屏冒烟；测试替身不等于真实 FPGA 验证。
