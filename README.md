# cdl5Device

2026-11-03 用户定：网口连接区仅保留网卡选择和连接/断开，删除 IP、端口等固定参数展示及对应 QML 参数字段；实际连接配置、校验、ARP 和收发关闭行为保持。

Windows 验证：通过主程序本地源码覆盖增量构建，五个网口官邸 qmllint 目标成功；仍有 QObject 属性类型的既有静态提示。八秒离屏冒烟通过，仅既有字体提示；未进行硬件操作或 Linux 验证。证据：LiDAR_DataACQ/build/build-network-ui.log、smoke-network-ui.json。

2026-11-03 上传接口：官邸仍拥有唯一具体 Core；Core 继承 `Device<HandleT, CollectorT>`，Device 直接继承 Handle 并重写 onData。Core 只提供 Qt 属性、锁信号与末端预析构；不增设上传回调、状态缓存或逐包信号转发。协议、Collector 与 UI 逻辑保持。

CDL5 采集卡官邸的桌面版本使用 UDP，Linux 板端由 DMA Handle 接入 FPGA，BridgeCore 转发帧字节。4/5 kHz 规格各自输出 ADC0、ADC1、ADC2 的三路独立功率谱。

4/5 kHz 页面按 ADC0、ADC1、ADC2 顺序平铺九张同级图窗，每通道依次为 `ADCn·功率谱`、`ADCn·径向风`、`ADCn·风廓线`，不再嵌套通道标签。径向风页继续包含风速和 CNR；图窗常驻和数据接线保持。设备副标题仅显示 `网口·UDP·4kHz/5kHz/10kHz` 中对应规格，不在标签重复尺寸和通道数。

2026-10-31：已实现 DMA 启动/等待/读取/取消、顺序 Collector、三路功率谱 QML 页面及独立落盘。后续风测量接入见下文。实测帧头及只读寄存器为 Profile 8、450×128，用户确认 Windows 按实际数据接收。已回放两帧真实抓包并核对三路数值；这不代替更新后主程序的现场操作验收。

## 10 kHz ADC0 三门宽版

桌面 `Cdl5TenKModule` 对应配置的 PL `0x26110401`。输出槽 0/1/2 分别是同一个 ADC0 的 6/15/30 m 距离门，三路各 240 门 × 240 频点。门起点为 0 m，三路末门起点分别是 1434/3585/7170 m，覆盖长度为 1440/3600/7200 m。帧头的 Profile 7 与旧版相同，桌面由明确的规格选择距离网格，新版门距固定。

每路依次显示功率谱、径向风、风廓线，共九个子页。绘图标签只写门宽与图名，例如 `6m·功率谱`、`15m·径向风`、`30m·风廓线`。功率谱用距离门下标，径向风速和 CNR 用实际斜距（米）。四束反演按本路斜距乘平均实测高度角的正弦计算高度。零风速参考频率固定为 `Configuration.h` 的 `ZeroWindReferenceMHz`（80 MHz），三路及 4/5 kHz 各 ADC 共用，不再读取外部配置文件。

新原谱文件标签为 `ADC0_6m`、`ADC0_15m`、`ADC0_30m`。每帧同目录的 `<frame-time>.json` 描述对应原谱文件、行列、字节序和排列、门起点/门距、每门有效样本数、采样率、FFT 长度及帧头首 bin、触发周期、累积数。`configuredPlVersion` 是所选规格的配置值。原谱继续写无头小端 double。径向风使用 `Rangeadc0_6m` 等分通道字段，风廓线文件各自写出实际 `High`。描述文件记录帧规格和对应文件名，与原谱文件分别写入。

## 模块与构建

| 开关（默认 OFF） | 目标 | 用途 |
|---|---|---|
| CDL5_WITH_UDP | Lidar::Cdl5Udp | 复用 PcapHandle，Windows/Linux |
| CDL5_WITH_DMA | Lidar::Cdl5Dma | Linux Cdl5DmaHandle，Qt Core，无 QML |
| CDL5_WITH_DEVICE | Lidar::Cdl5Device | UDP 官邸、Collector、三路原谱和非均匀风图，自动开启 UDP |

C++20、CMake >=3.28。传输层 Qt Core >=6.8.3；桌面模块 Qt >=6.11.2、Quick/Qml/GuiPrivate/ShaderTools。UDP 需要 libpcap >=1.10.0，Windows 使用 Npcap。DMA 共享 ioctl 头通过 CDL5_DMA_PROXY_SOURCE_DIR 指向 DmaProxy，不复制或修改驱动 ABI。

Windows 官邸（Qt/Npcap 路径按本机配置）：

```powershell
cmake -S . -B build-win -A x64 -DCDL5_WITH_DEVICE=ON `
  -DCMAKE_PREFIX_PATH=C:/path/to/Qt/msvc2022_64 `
  -DHANDLECORE_PCAP_INCLUDE_DIR=C:/path/to/Npcap/Include
cmake --build build-win --config Release
ctest --test-dir build-win -C Release --output-on-failure
```

Linux DMA 单独构建，不要求 QML、绘图库或串口：

```sh
cmake -S . -B build-dma -DCDL5_WITH_DMA=ON \
  -DCDL5_DMA_PROXY_SOURCE_DIR=/path/to/DmaProxy
cmake --build build-dma -j
ctest --test-dir build-dma --output-on-failure
```

Linux 桌面验证可以同时启用 CDL5_WITH_DEVICE。Windows 请求 DMA 时配置直接拒绝。外部先配置 HandleCore 时，应事先开启其 Pcap 模块。依赖均为完整提交固定；开发可用标准 FETCHCONTENT_SOURCE_DIR_* 覆盖，不改变默认远端地址。

## 桌面官邸

QML 导入 `Lidar.Cdl5`，按实际 FPGA 规格装配下表对应的官邸。页面独持精确 C++ 后端，向任务提供 DAQ 族引用，保留族级锁、Collector 原型/重试、消费线程及取消链；不提供 DMA 枚举或 DMA 桌面页面。

连接只选择本机网卡，其余型号参数在 C++：Windows 192.168.100.200:55200，板端 192.168.100.100:55200。三张原谱均用距离门/频点下标，不冒充物理距离或风速。Z 范围初始 0/0 自动全范围；按原谱显示，只有绘图器才钳制。

三个末端分别持有精确型号后端，Collector、图窗尺寸、累积上限由 `Configuration.h` 的同一编译期规格导出。没有全局“活动型号”别名或运行时规格状态；应用选择页面即选择该型号。帧头 Profile、lane 格式和尺寸必须同时匹配，不会把等字节长度的 P7/P8 误当成同一种帧。

| QML 官邸 / C++ 后端 | Profile | 三路原谱尺寸 | lane | 累积输入 | 混合距离覆盖 |
|---|---|---|---|---|---|
| Cdl5FourKModule / Cdl5FourKDevice | 9 / 4 kHz | 500×128 | 5 | 1–16 s | 34.5 km |
| Cdl5FiveKModule / Cdl5FiveKDevice | 8 / 5 kHz | 450×128 | 5 | 1–13 s | 27 km |
| Cdl5TenKModule / Cdl5TenKDevice | 7 / 10 kHz | 240×240 | 6 | 1–6 s | 1.44 / 3.6 / 7.2 km |

旧 P7 混合模式为前 200 门×15 m、后 40 门×60 m，旧 P7 和 P8 的统一 60 m 模式分别覆盖 14.4/27 km。4/5 kHz 图窗仍按各自规格和帧头确定距离网格。累积上限按 16 位计数器/标称触发频率计算，DMA 端另按活动周期验证实际计数，并确认配置回读相符；10 kHz 已完成一次 1 秒实采，多秒累积仍待硬件验收。

完成一次采集，处理线程独立发出三路 spectrumReady。4/5 kHz 启用保存时通过现有 AcqSaver 写入：

```
Rawspec/<session>/ADC0_<frame-time>.bin
Rawspec/<session>/ADC1_<frame-time>.bin
Rawspec/<session>/ADC2_<frame-time>.bin
```

每份为无头、小端 IEEE-754 double，距离门为行、型号规定的 128 或 240 频点为列。沿用已有 detached 保存方式，不新增退出排空。原型为空数组；makeSelf 才 reserve 三路收集区，完成后只读共享给处理/绘图/保存。

## 字节协议与采集

详见 [采集协议](docs/acquisition.md)。要点：

- 下行统一 8 字节：小端 magic 0x354c4443，随后小端 uint32 累积秒数；传输格式接受 1..16 秒开始、0 取消；具体官邸进一步钳制到本型号上限，DMA 按活动周期拒绝超过 65535 次的请求。单次触发，默认 1 秒。
- DMA 读取整帧所需的传输长度来自 REG_FRAME_WORDS，累积计数由当前活动触发周期计算；不改变触发频率、光源、通道映射或距离模式。
- 一帧一次 START，先准备 DMA 再触发 FPGA。WAIT_KEEP 短等待，完成后 rewind/read，每次交付最多 1024 字节自有 QByteArray，尾包交实际剩余字节。
- 第一包含 32 字节整帧头；随后每条 32 字节，前三个小端 FP32 分别转成 double，无符号整数仅用来读取位型。元信息不参与重排或去重。
- 1024 字节是 DMA 发送块长。Windows 抓包可见 7168 字节合并批次，接收按 32 字节对齐及整帧剩余容量校验，不要求一包最多 1024 字节，也不新增接收端拆包拷贝。
- 接收策略按本轮用户裁定：假定完整数据按顺序交付；缺包不判齐，由现有看门狗重试。不能检测 UDP 重复/乱序或重试旧包，不能宣称网络无损。
- DMA 突发发送必须先有对端 MAC；Bridge 的 cdl5Device 入口要求 --dstMac，避免已有 Pcap 的 ARP 等待单包槽覆盖前面的谱包。

## 验证与后续

[VALIDATION.md](VALIDATION.md) 记录 Windows、Linux、ARM64 结果和实测边界。测试替换系统调用/抓包接口，实际 Handle、Collector、Device 消费链保持生产实现。`tests/build-arm.sh` 使用现有板卡 SDK 构建测试，不安装应用、不重打镜像。

2026-11-03 已将现场板卡更新为 10 kHz/P7，寄存器及一次 1 秒实采帧头均为三通道 240×240，使用 Cdl5TenKModule。此前 5 kHz/P8 记录保留历史适用范围；其他板卡仍须核对活动寄存器和实际帧头。物理风速的现场验收需要逐 ADC 确认零风速参考频率，多秒累积与长期稳定性另行验证。

## 许可与发布

公开源码仓库：https://github.com/Lateef-ZHOU/cdl5Device 。DMA 资源、采集与相关测试保留 GPL-2.0 来源标记；HandleCore 等依赖各自许可不变。整体许可后续整理，不自动沿用其他官邸 MIT。仓库不包含厂商交付包、内核镜像或运行库；DMA 构建仍须显式提供 DmaProxy 共享接口头的目录。

## 资源阶段通知（2026-11-02）

连接采用 HandleCore 的 `connectionChanged(ResourcePhase)`；主动关闭前在所属线程同步发 `AboutToRelease`，完成事件用于回读现状。页面借出已有 `deviceState` 机制对象，不再保存 isOpen/isLocked/holder 的 QML 转交属性；标签通过连接/锁完成阶段回读，不恢复通用 statusChanged。固定依赖已同步；本轮提交尚未推送，验证使用显式本地源码覆盖。

## 4/5 kHz 与旧 10 kHz 逻辑的距离口径

三路分别提供功率谱、径向风速/CNR、风廓线。距离以交付上位机 frame_decoder.py 的 gate_distance_m 为准，以每门起点为坐标。P8 混合模式前 200 门从 0 m 起每 15 m，接下来的 150 门从 3000 m 起每 60 m，最后 100 门从 12000 m 起每 150 m。帧头 word7 的 range_mode 置位时按 60 m 等间隔处理。P9 混合模式最后有 150 门。P7 只包含前 200 门×15 m 和后 40 门×60 m，统一模式为 240 门×60 m。

工作线程逐通道拟合谱峰。CNR 不依赖零风速频率。风速换算使用帧头 word4 的起始 bin、500 MSPS、512 点 FFT 和 1550 nm 波长。零风速参考频率为型号常量 80 MHz（`Configuration.h`）。参考频率超出本帧频带时，该通道仅显示 CNR，风速缺测。每路独立使用四束实测朝向，在任务同步点求 ENU，按逐门斜距乘平均仰角的正弦生成高度。无朝向、未凑齐四束或几何退化时不产生风廓线。勾选保存后使用 AcqSaver 的非均匀接口保存已标定通道及真实 Range/High。


2026-11-03 验证：Windows 主程序 Release/页面装配通过；Linux Qt Core 6.8.3 UDP、DMA 连接与采集测试 3/3 通过。接口联调使用相邻源码覆盖，新依赖固定为本地提交，尚未推送；未连接实物设备或更新板卡。

## 源码目录（2026-11-03）

公开文件直接放在 `include/`，引用时使用文件名，不再携带原单仓库的业务目录前缀。CMake 目标名称、C++ 类型和 QML 模块名保持不变；使用方须同步更新头文件路径及固定依赖版本。
协议头使用 `Cdl5Protocol.h`，避免与其他设备的同名协议头冲突。

目录整理验证（2026-11-03）：Windows：公开头独立编译、Collector 与页面测试 2/2 通过；Linux Qt Core 6.8.3：DMA 独立构建、CTest 2/2，BridgeCore 4/4 通过。本轮通过本地源码覆盖验证，固定依赖同步更新到本地提交；尚未推送，不代表已验证远端拉取。

## 2026-11-04：设备日志直接接线（用户定）

删除具体设备的 logSource 自引用属性，QML 直接监听现有设备对象的 logMessage/connectionChanged；设备失败日志直接调用继承自 Handle 的信号。协议、收发、重连和关闭顺序不变。 本轮仅本地整理，未推送；Windows 集成验证记录见主程序维护操作。

## 2026-11-04：设备身份与占用查询（用户定）

设备名称/副标题由构造传入 DeviceBase 的公开 const 成员，Qt 通过 MEMBER CONSTANT 直接暴露；占用查询只保留 holder，Qt 用无效 QVariant 表示空闲、字符串表示占用，删除 isLocked 属性。既有连接、重连、收发和取消逻辑保持。

本轮仅本地整理；集成验证记录见主程序 docs/maintenance/validation.md，不推送。


## 2026-11-04：Handle 连接入口统一（用户定）

DMA/UDP Handle 测试同步使用 open/close 连接入口；DMA 资源、触发/取消、分块和关闭顺序不变。本轮不更新板卡。

Windows 主程序增量构建和八秒离屏冒烟通过；本仓测试调用已同步，本轮未单独重跑本仓全套测试，不含硬件或 Linux/ARM 验证。 依赖固定到相邻本地提交，尚未推送，联调使用源码覆盖。

## 2026-11-04：进料在消费端取值（用户定）

Collector 的 onFeed 接收 optional<QByteArray> 的 const 引用，由本型号解析端取出字节。空输入由 DeviceCore 公共 feed 拦截；三通道解析、帧格式和尺寸校验不变。

本轮仅本地整理，不推送；集成验证见主程序维护记录。


2026-11-05 风廓线页面（用户定）：4/5/10 kHz三规格、各ADC风廓线页统一只显示图窗，删除等待四束结果的提示及其布局、显隐接线；收帧、标定、反演和历史追加不变。与其他采集卡风廓线页保持一致。


### 2026-11-05 公共视觉与连接布局

三种规格的连接区使用 UICore ConnectionSection；规格、枚举、命令及日志不变。 本轮仅本地整理，未推送；Windows 集成验证记录见主程序维护操作。

### 2026-11-05 径向风说明文字

三个规格的径向风提示统一使用 UICore HintText，按可用宽度换行并限制自然宽度。原提示内容、标定可见条件和收数接线保持。主程序 Windows 增量构建及八秒冒烟通过；仅本地提交，未推送。
## 径向风初始门位（2026-11-05）

三个规格的 chartParameters 提供 ranges，复用已有 gateRanges(profile, false, rows) 生成各规格混合距离门，供图表初始零值与逐门悬停使用；不再以两端点代替整个剖面。初始混合模式只是连接前显示默认值，真实回报仍依据帧头的 range_mode 整帧替换，支持切换到均匀60 m模式。Collector、处理、保存及风廓线保持。

验证：2026-11-05 Windows Qt 6.11.2 / MSVC 增量构建通过；UICore CTest 3/3、ChartCore nonuniform 1/1、主程序八秒桌面冒烟及四窗装配通过。装配检查覆盖三规格、三通道的18张初始风速/CNR图的内部及分段门位悬停。未连接硬件、未推送。


### 2026-11-05 连接间距、累积默认值与径向风页面整理

三个规格删除径向风页的“等待接收功率谱”提示和外层 ColumnLayout，直接放置非均匀径向风图。未标定时仍由官邸通过 logMessage 报告缺少零风速参考频率，图中不显示诊断条；算法、默认逐门零值、数据及风廓线逻辑保持。

主程序 Windows Release 增量构建、八秒桌面冒烟及四窗口装配检查通过；运行 stderr 为空，保留既有 CDL5/Qt 生成代码 C4702 编译警告。仅本地验证，未连接硬件、未推送。
