# cdl5Device 维护入口

2026-11-09 用户定：DAQ 族去处理线程与通道归一，见 README 同日条目；落盘改为族统一 JSON，旧三份原谱 .bin 与规格 JSON 不再写出。

2026-11-09 用户定：零风速参考频率为型号常量 80 MHz（Configuration.h 的 ZeroWindReferenceMHz），4/5 kHz 与 10 kHz 所有通道共用；删除 cdl5-wind.ini 及其读取、QSettings 依赖。参考落在本帧频带内即换算风速，否则只出 CNR。qmlcache 生成代码按其他官邸的方式局部豁免 C4702。见 VALIDATION。

2026-11-06 用户定：桌面 10 kHz 装配明确选用 TenKAdc0 规格，对应 PL 0x26110401。三个输出槽属于同一 ADC0，按 6/15/30 m 独立计算斜距和高度、共用 ADC0 零风速参考。每帧保存说明 JSON 与三份原谱，保留 4/5 kHz 和传输链。见 README、VALIDATION。

2026-11-03 用户定：网口连接区仅保留网卡选择和连接/断开，删除 IP、端口等固定参数展示及对应 QML 参数字段；实际连接配置、校验、ARP 和收发关闭行为保持。

2026-11-03 图窗整理（用户定）：FourK/FiveK/TenK 全部平铺九个同级页，按 ADC0/1/2 各自的功率谱、径向风、风廓线排列，不再嵌套通道 TabPane。统一标题为 ADCn·图名，设备副标题缩为网口·UDP·对应频率。保持后端、历史、收帧和处理接线；仅本地提交，主程序通过源码覆盖验证，证据见 VALIDATION。

2026-11-03 FPGA 更新验收：用户授权备份、替换并重启。活动逻辑已从 P8/5 kHz/450×128 改为 P7/10 kHz/240×240；寄存器与一次 1 秒真实采集帧头均已核实。只替换原 BOOT.BIN 的 PL 分区及分区名称，其余启动分区、内核、设备树、驱动、系统库与 root 镜像保持；SD 与本机保留旧 BOOT.BIN。两桥接服务自动恢复，三路数据完整到达 Windows。详情、回退与验证边界见 VALIDATION；下方“尚未刷入”为此前阶段。

2026-11-03 用户定：同仓提供 FourK/FiveK/TenK 三个明确类型的 UDP 官邸，规格统一在 Configuration.h，保持三 ADC、FP32 小端与 32/1024 字节边界。DMA 按活动 Profile 核对 P9/P8 的 5 lane 或 P7 的 6 lane，不改驱动 ABI、触发/取消及关闭顺序。Windows 装配由应用选择具体页面，删除旧 Cdl5Module/Cdl5ActiveCollector；本轮准许推送及更新板端 root 镜像、重启，不烧写 FPGA、不动系统/内核和 eMMC。验证进度见 VALIDATION。

2026-11-02：三路 ADC 各自的原谱、非均匀风速/CNR、风羽已接入。门位与起始 bin 读实际帧头；风测量标定、验证边界详见 README 和 VALIDATION。


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
