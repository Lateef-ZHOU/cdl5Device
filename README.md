## 2026-11-20：用 15 m 近段拼接 60 m 远段（用户定）

PL 仍传输 6、15、30、60 m 四个槽。Windows 图页依次显示 6m、15m（混合）、30m。混合通道的前 240 门来自 15 m 槽，后 180 门来自 60 m 槽下标 60～239。覆盖 `[0, 14400)` m，拼接点为 3600 m。功率谱、径向风和 CNR 使用实际斜距，风廓线使用各门投影高度。功率谱曲面在 3600 m 起按 60 m 门距绘制。采集和风廓线保存支持对应的均匀或非均匀坐标。

## 2026-11-20：页面测试轴配置改用 SurfaceGrid

smoke 测试中的功率谱初始轴检查从 `LinearArray2D<double>` 改为 ChartCore 的 `SurfaceGrid`，与 Algorithm 删除 `LinearArray.h` 后的类型迁移一致。断言值与测试行为不变。

验证：build-1109 Release 零警告，CTest 3/3。

## 2026-11-17：Collector 命令集合使用 vector（用户定）

Collector::buildStart/buildStop 及设备 stopCommands 按值返回 std::vector<std::vector<unsigned char>>，与 DeviceCore 直接发送的类型一致。沿用 Cdl5::command 编码，在集合工厂出口取得自有字节；累积秒数和停止命令 0 的内容、UDP/DMA 协议、收包与取消逻辑保持。未改底层单条编码器或增加缓存。验证见 VALIDATION.md，本轮不推送。

## 2026-11-17：onTX 接入 vector 只读引用（用户定）

onTX 输入统一为 const std::vector<unsigned char>&，同步传参不复制调用方数组；RX 仍用只读 span。DMA 命令校验与 TXRuling 自有字节保持，UDP 继续使用 PcapHandle；适配 DMA 测试替身。 不新增成员状态；使用相邻本地 HandleCore 源码覆盖，远端锁定版本暂不变，不推送。Windows Release CTest 3/3，Linux UDP+DMA 配置 CTest 3/3 通过。 证据见主程序 docs/maintenance/validation.md 本轮记录。

## 2026-11-14：connSend 引用传输验证

DMA 发送覆盖及测试替身适配 connSend 的 vector 只读引用。Linux Release 与 DMA/协议测试 3/3、Bridge 全端点集成 4/4 通过；Windows 主程序构建与离屏冒烟通过。证据主程序 build/connsend-linux.log 与 ArchHFNL:~/connsend-20261114。未操作板卡，不推送。

## 2026-11-14：connSend 使用自有数组的只读引用（用户定）

connSend 覆盖改为 const std::vector<unsigned char>&，与 HandleCore 保持一致；仅同步读取最终报文字节，不增加成员缓存或兼容转发。接收视图、协议、重连及关闭行为不变。

## 2026-11-14：Handle 收发边界

DMA Handle 的收包 sink、onRX/onTX、connSend 改用只读 span；发送裁决拥有字节，命令同步解析后仍按整数请求交给 worker。采集块活到同步派发结束，Bridge 跨线程前复制取得所有权。 发送顺序、重连和关闭不变；本轮不部署硬件、不推送。统一契约见 HandleCore/docs/reference.md。

## 2026-11-14：适配只读发送视图

DMA/UDP 测试适配 Handle 的只读 span 单条发送入口，临时 QByteArray 只在完整发送表达式内借用；生产采集、裁决、重连和关闭保持。

Linux 独立快照构建及 DMA/UDP 测试 3/3 通过；日志 ArchHFNL:~/sendspan-20261114/cdl5Device-*.log。Windows 官邸随主程序增量构建及冒烟通过；未部署 ARM。 本轮不推送。

## 2026-11-14：停止门依赖接入

HandleCore 停止门改为标准二元信号量后，DMA 采集测试显式包含自己的 QSemaphore 屏障头，不再借用传输基类的间接包含。CDL5 生产代码和 DMA 控制不变；本轮 Linux x86_64 构建/CTest 3/3 通过，不代表 ARM 标准库已经核查。未推送。

## 2026-11-14：UTF-8 日志接口（用户定）

采集 Handle、DMA 异常和官邸日志统一为 UTF-8 std::string；DMA 错误码与累积范围使用 std::format。异常 what() 直接进入拥有字节的日志值；QML 使用 UICore 转换。采集、DMA、重试、取消和关闭保持。

需要支持 C++20 std::format 的标准库。本轮仅本地修改与验证，不推送。

验证：Linux/GCC/Qt 6.8.3 独立构建和 CTest 3/3 通过（ArchHFNL:~/logstring-20261114/cdl5Device-*.log）；Windows 官邸随主程序构建与 8 秒离屏冒烟通过。未更新板卡或做 ARM/实物验证。

## 历史：2026-11-14 TXRuling 只读 span（已被上方拥有型裁决取代）

onTX 返回命令字节的只读 span，DMA connSend 仍同步解析请求后交工作线程，不留存视图。 重连和关闭保持，与本地 HandleCore 同批使用，暂不推送。

Linux DMA/UDP 构建及 CTest 3/3、Bridge DMA 转发通过；仅替身验证，证据 ArchHFNL:~/txspan-20261114/。

## 2026-11-14：只读 span 接收裁决（用户定）

2026-11-14 接收裁决适配：onRX 返回 `std::span<const unsigned char>` 借用接收数据，onData 同步借用只读 span，现有 Qt 解析器在消费处零拷贝适配。留存或异步投递前必须复制取得字节所有权，不能仅浅拷贝视图。协议、重连和关闭逻辑不变；与本地 HandleCore 同批使用，尚未推送。

Linux/GCC/Qt 6.8.3：独立 DMA/UDP 构建及 CTest 3/3 通过；Bridge DMA 转发亦通过。证据在 ArchHFNL:~/rxspan-20261114/cdl5Device-{config,build,test}.log；均使用硬件替身，未更新板端。验证日志为相应构建目录下 rxspan-build.log / rxspan-test.log（Linux 使用上述命名）。

## 2026-11-14：静态功率谱、径向风与 CNR（用户定）

6/15/30 m 三路直接填写各自 Spectrum<Cdl5ChannelSpec<I>> 的最终数组，保留 FP32 小端解释和每条 32 字节记录中 0/4/8 偏移。功率谱、径向风及独立 CNR 按各路 Spec 计算和传递；channelsReady 的只读共享元组接入九个图页。收包、请求、重试、取消、四束历史和 JSON 保存口径保持。

图窗配置通过 root 明确引用对应通道，避免 QML 未限定访问。公共图窗保持具体型号提供的类型化 setData 入口。

本轮仅本地联调，不推送；与配套 FamilyCore、ChartCore 及官邸源码一起使用。

验证：Windows/MSVC Release 构建及 CTest 3/3 通过，覆盖 UDP 边界、FP32 小端解码、三路静态数组、距离轴、四束结果与九张图装配。Linux DMA pipeline 测试已适配新类型，本轮未运行 Linux 或板端验证。证据：build-1109/static-frames*.log 与 Testing/Temporary/LastTest.log。

## 2026-11-14：接入 FamilyCore（用户定）

依赖声明与开关改用 FamilyCore / FAMILYCORE_WITH_*，协程头由 DeviceCore 提供。当前仅本地整理，GitHub 仍为 TaskCore，旧固定提交暂不更新；联调使用 `FETCHCONTENT_SOURCE_DIR_LIDAR_FAMILYCORE` 指向相邻 FamilyCore，并覆盖配套 DeviceCore/Algorithm 等源码。Windows 主程序或 simonly 的实际消费目标构建通过，业务与数据格式不变；未推送、未更新交付包。

## 2026-11-14：删除旧风场测试依赖

Windows/MSVC Release 构建通过，CTest 3/3。

配合 Algorithm 删除 WindField.h，测试改用标准风廓线帧或删除无用引用；本库生产、保存与取消逻辑不变。不推送。

## 2026-11-14 风廓线原帧直接传输（用户定）

三路风廓线经 Cdl5Profiles 直接传递具体 VerticalWindProfileUnit 的只读份额。6 m、15 m、30 m 风羽图只取对应项，保留已有通道规格、四束反演、坐标和保存格式；旧 profileReady 接线删除。Windows/MSVC Release 构建、公开头编译及 CTest 3/3 通过。 本轮不推送。

# cdl5Device

2026-11-13 用户定：通道规格的距离门数量与频点数量分别命名为 `rangeGateCount`、`frequencyBinCount`，取代 `rows`、`columns`。同步规格使用方，数值与数据布局不变；通用二维传输对象和 SpectrumLike 的行列接口保持。

2026-11-10 用户定（凑帧归官邸）：官邸持有原样帧缓冲（含帧头记录）与已收字节，`assemble` 校验记录对齐、首包帧头与剩余容量后放置字节，凑满即解析帧头起始 bin 并拆三路，解析后释放缓冲。

2026-11-10 用户定（收包与解析分离）：收包只校验帧头与 32 字节记录对齐并按序 `store`，帧头记录作为帧内元素原样保留；`decode` 解析帧头起始 bin 并拆开三路交错的 FP32。删除收包期间的帧头字段与 `profile()`/`firstBin()`。

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

## 规格：10 kHz ADC0 四门宽

`Cdl5Module` / `Cdl5Device` 对应 PL `0x26111901` 和配套 PS。输出槽 0–3 分别是同一个 ADC0 的 6/15/30/60 m 距离门。每路 240 门 × 240 频点，6 lane，帧长仍为 1,843,232 字节。门起点为 0 m，各路末门起点为 1434/3585/7170/14340 m，覆盖长度为 1440/3600/7200/14400 m。帧头要求 Profile 7、有效槽掩码 `0x0f`、尺寸 240×240 和起始 bin 15。原三路掩码 `0x07` 的旧帧会被拒收。

每路依次显示功率谱、径向风、风廓线，共十二个子页。绘图不显示镜面（480 m）之前的门：6/15/30 m 混合通道分别从第 80/32/16 门开始显示（遮蔽是图页配置 `rangeMask`，帧与落盘仍是全量）。绘图标签只写门宽与图名，例如 `60m·风廓线`。功率谱用斜距（米）和频率（MHz），径向风速和 CNR 用本路斜距。四束反演按本路斜距乘平均实测高度角的正弦计算高度。四路共用 `Configuration.h` 的 80 MHz 零风速参考。

通道名为 `ADC0_6m`、`ADC0_15m`、`ADC0_30m`、`ADC0_60m`，按此顺序写入 FamilyCore 的 JSON。

## 模块与构建

| 开关（默认 OFF） | 目标 | 用途 |
|---|---|---|
| CDL5_WITH_UDP | Lidar::Cdl5Udp | 复用 PcapHandle，Windows/Linux |
| CDL5_WITH_DMA | Lidar::Cdl5Dma | Linux Cdl5DmaHandle，Qt Core，无 QML |
| CDL5_WITH_DEVICE | Lidar::Cdl5Device | UDP 官邸、Collector、四路原谱与径向风/风廓线图，自动开启 UDP |

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

三路分别提供功率谱、径向风速/CNR、风廓线，距离以每门起点为坐标。收齐末包后先按各槽第一个距离门扣本底，再用去噪谱逐通道拟合谱峰；保存的功率谱仍是未去噪原谱，风速/CNR 是去噪谱的结果。CNR 不依赖零风速频率。风速按验收口径分段换算：0 ≤ f ≤ 200 MHz 时 v=(f−80)/1.29，200 < f ≤ 250 MHz 时 v=(f−330)/1.29（f 为拟合频率）。每路独立使用四束实测朝向，在任务同步点求 ENU 并生成高度；无朝向、未凑齐四束或几何退化时不产生风廓线。

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


## 2026-11-13：不可变墙钟风廓线（用户定）

风廓线验证按 6m、15m、30m 各自的 Cdl5ChannelSpec 构造不可变静态帧，再通过只读视图交给页面。距离到高度的换算、三个通道的协议及采集行为保持。

本轮使用本地源码覆盖验证，尚未推送；具体集成验证见主程序维护操作。

## 2026-11-13：大数组固定对象与只读传输

4/5/10 kHz 官邸的功率谱、径向风和 CNR 信号与 DAQ 族统一改传只读共享数组份额，页面原样转交图表。原通道规格、FP32 解析、距离轴、保存及重试取消语义不变。

本轮使用本地源码依赖验证；上游及本库暂不推送，远端固定版本未更新。

2026-11-13 本轮验证：Windows/MSVC Release 构建及公开头独立编译通过；build-1109 中 CTest 3/3 通过。使用本地源码依赖，未验证 Linux 或远端拉取；暂不推送。

