# cdl5Device

2026-11-10 用户定（二维线性数组）：通道规格给出功率谱网格（行轴为本路门宽，列轴步长 500/512 MHz，列轴起点在首包按帧头起始 bin 写入）；`FirstBinDefault = 15` 只用于首帧前的初始频率轴。页面功率谱显示真实斜距与频率。

2026-11-10 用户定（波长与抽稀）：型号常量 `WavelengthM = 1550 nm` 交族基，删除 `MetersPerSecondPerMHz`。

2026-11-10 用户定（线性数组）：通道规格以 `radialWind = cnr = LinearArray<double>(起点, 门距)` 给出距离轴；`radialReady` 发线性数组，页面原样交给径向风图。

2026-11-10 用户定：通道规格改为起点 + 步长（第 0 门在 0 m），`radialReady` 不带斜距、`profileReady` 发均匀风廓线；页面统一用 `Wind2DView`（配置含 `rangeStart`）与 `WindBarbItem`。页面测试补充族信号经页面交给图窗不出 QML 类型警告的检查。

2026-11-10 用户定：课题组只使用 PL `0x26110401`（Profile 7、10 kHz、ADC0 的 6/15/30 m 三路均匀门距），删除 FourK/FiveK 与三路 ADC 的旧 TenK 规格，连同分段门距、帧头统一门距标志和两个旧页面。型号模板拍平为单一具体类：`Cdl5Device`（C++ 后端）、`Cdl5Module`（QML 页面）、`Cdl5Collector`；规格是 `Configuration.h` 里的一组常量。累积上限随之为 1–6 s（16 位计数 / 10 kHz），传输层命令解码同样拒绝超过 6 s 的请求；Linux DMA 端只接受该 Profile、lane 与尺寸。会话目录标识沿用 `Cdl510K`。下文带日期的旧条目只作追溯。

2026-11-09 用户定（DAQ 第二步）：Collector 的 createPrototype（只读原型）改为 `create(const DAQParameters&)`，按请求参数造完整实例并备齐收集区；makeSelf 只供看门狗重试，复用 create。

2026-11-09 用户定：删除处理线程、中间 Core 与型号处理 lambda；末端直接继承 `DAQDevice<HandleT, CollectorT>`，唯一 QObject 来自 Handle。Collector 继承族基的通道数组，型号只给通道规格并按协议整理功率谱，收齐末包调用 complete() 由族基在收包线程做标准处理；显示、落盘（每帧一个 JSON）与风廓线由族层完成。页面改接族信号 spectrumReady/radialReady/profileReady（按通道下标），family 改为 QtObject，删除 device.device 自引用。通道名 ADC0_6m/15m/30m（多门宽）或 ADCn；频率轴取帧头起始 bin，非多门宽规格的距离轴按帧头距离模式。删除 WindProcessing.h（门位/频率辅助并入 Collector.h）与 Rawspec 规格 JSON；帧头只保留 profile/firstBin/uniformRange，累积计数与触发周期不再保存。验证：独立构建零警告，CTest 3/3。

2026-11-03 用户定：网口连接区仅保留网卡选择和连接/断开，删除 IP、端口等固定参数展示及对应 QML 参数字段；实际连接配置、校验、ARP 和收发关闭行为保持。

Windows 验证：通过主程序本地源码覆盖增量构建，五个网口官邸 qmllint 目标成功；仍有 QObject 属性类型的既有静态提示。八秒离屏冒烟通过，仅既有字体提示；未进行硬件操作或 Linux 验证。证据：LiDAR_DataACQ/build/build-network-ui.log、smoke-network-ui.json。

2026-11-03 上传接口：官邸仍拥有唯一具体 Core；Core 继承 `Device<HandleT, CollectorT>`，Device 直接继承 Handle 并重写 onData。Core 只提供 Qt 属性、锁信号与末端预析构；不增设上传回调、状态缓存或逐包信号转发。协议、Collector 与 UI 逻辑保持。

CDL5 采集卡官邸的桌面版本使用 UDP，Linux 板端由 DMA Handle 接入 FPGA，BridgeCore 转发帧字节。

## 规格：10 kHz ADC0 三门宽

`Cdl5Module` / `Cdl5Device` 对应 PL `0x26110401`。输出槽 0/1/2 分别是同一个 ADC0 的 6/15/30 m 距离门，三路各 240 门 × 240 频点、6 lane。门起点为 0 m，三路末门起点分别是 1434/3585/7170 m，覆盖长度为 1440/3600/7200 m。帧头 Profile、lane 格式和尺寸必须同时匹配。

每路依次显示功率谱、径向风、风廓线，共九个子页。绘图标签只写门宽与图名，例如 `6m·功率谱`、`15m·径向风`、`30m·风廓线`。功率谱用距离门下标，径向风速和 CNR 用实际斜距（米）。四束反演按本路斜距乘平均实测高度角的正弦计算高度。零风速参考频率固定为 `Configuration.h` 的 `ZeroWindReferenceMHz`（80 MHz），三路共用，不读取外部配置文件。

通道名为 `ADC0_6m`、`ADC0_15m`、`ADC0_30m`；落盘由 TaskCore 的族统一 JSON 完成（每帧一个文件，按通道写功率谱、径向风与 CNR）。

## 模块与构建

| 开关（默认 OFF） | 目标 | 用途 |
|---|---|---|
| CDL5_WITH_UDP | Lidar::Cdl5Udp | 复用 PcapHandle，Windows/Linux |
| CDL5_WITH_DMA | Lidar::Cdl5Dma | Linux Cdl5DmaHandle，Qt Core，无 QML |
| CDL5_WITH_DEVICE | Lidar::Cdl5Device | UDP 官邸、Collector、三路原谱与径向风/风廓线图，自动开启 UDP |

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

QML 导入 `Lidar.Cdl5` 并装配 `Cdl5Module`。页面独持 `Cdl5Device` 后端，向任务提供 DAQ 族引用；不提供 DMA 枚举或 DMA 桌面页面。

连接只选择本机网卡，其余型号参数在 C++：Windows 192.168.100.200:55200，板端 192.168.100.100:55200。Z 范围初始 0/0 自动全范围；按原谱显示，只有绘图器才钳制。

累积时间由 `paramsFor` 校验为 1–6 s（16 位计数器 / 标称 10 kHz），越界拒绝；DMA 端另按活动周期验证实际计数，并确认配置回读相符。10 kHz 已完成一次 1 秒实采，多秒累积仍待硬件验收。

## 字节协议与采集

详见 [采集协议](docs/acquisition.md)。要点：

- 下行统一 8 字节：小端 magic 0x354c4443，随后小端 uint32 累积秒数；传输格式接受 1..6 秒开始、0 取消；DMA 按活动周期拒绝超过 65535 次的请求。单次触发，默认 1 秒。
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

## 风测量

三路分别提供功率谱、径向风速/CNR、风廓线，距离以每门起点为坐标。收齐末包后在收包线程逐通道拟合谱峰。CNR 不依赖零风速频率。风速换算使用帧头 word4 的起始 bin、500 MSPS、512 点 FFT 和 1550 nm 波长。参考频率超出本帧频带时，该通道仅显示 CNR，风速缺测。每路独立使用四束实测朝向，在任务同步点求 ENU 并生成高度；无朝向、未凑齐四束或几何退化时不产生风廓线。

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
