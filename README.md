# cdl5Device

CDL5 采集卡官邸：桌面只使用 UDP；Linux 板端由 DMA Handle 接入 FPGA，BridgeCore 只转发字节。一个设备产生 ADC0、ADC1、ADC2 三路独立功率谱。

页面按 ADC0、ADC1、ADC2 顺序平铺九张同级图窗，每通道依次为 `ADCn·功率谱`、`ADCn·径向风`、`ADCn·风廓线`，不再嵌套通道标签。径向风页继续包含风速和 CNR；图窗常驻和数据接线保持。设备副标题仅显示 `网口·UDP·4kHz/5kHz/10kHz` 中对应规格，不在标签重复尺寸和通道数。

2026-10-31：已实现 DMA 启动/等待/读取/取消、顺序 Collector、三路功率谱 QML 页面及独立落盘。后续风测量接入见下文。实测帧头及只读寄存器为 Profile 8、450×128，用户确认 Windows 按实际数据接收。已回放两帧真实抓包并核对三路数值；这不代替更新后主程序的现场操作验收。

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
| Cdl5TenKModule / Cdl5TenKDevice | 7 / 10 kHz | 240×240 | 6 | 1–6 s | 5.4 km |

P7 混合模式为前 200 门×15 m、后 40 门×60 m；P7/P8 还支持统一 60 m 模式，分别覆盖 14.4/27 km。图窗最大斜距覆盖该规格允许的模式，实际坐标仍读取帧头选择。累积上限按 16 位计数器/标称触发频率计算，DMA 端另按活动周期验证实际计数，并确认配置回读相符；10 kHz 已完成一次 1 秒实采，多秒累积仍待硬件验收。

完成一次采集，处理线程独立发出三路 spectrumReady；启用保存时通过现有 AcqSaver 写入：

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

## 三路 ADC 的风测量

每个 ADC 标签内有功率谱、径向风速/CNR、风廓线三个常驻图窗。距离以交付上位机 frame_decoder.py 的 gate_distance_m 为准，以每门起点为坐标。P8 混合模式前 200 门从 0 m 起每 15 m，接下来的 150 门从 3000 m 起每 60 m，最后 100 门从 12000 m 起每 150 m。帧头 word7 的 range_mode 置位时按 60 m 等间隔处理。P9 混合模式最后有 150 门。P7 只包含前 200 门×15 m 和后 40 门×60 m，统一模式为 240 门×60 m。

工作线程逐通道拟合谱峰。CNR 不依赖零风速频率。风速换算使用帧头 word4 的起始 bin、500 MSPS、512 点 FFT 和 1550 nm 波长。程序启动时从 exe 旁的 cdl5-wind.ini 读取 [wind] 节的 ADC0、ADC1、ADC2（MHz）。未填写或参考频率超出本帧频带时，该通道仅显示 CNR，风速缺测。每路独立使用四束实测朝向，在任务同步点求 ENU，按逐门斜距乘平均仰角的正弦生成高度。无朝向、未凑齐四束或几何退化时不产生风廓线。勾选保存后使用 AcqSaver 的非均匀接口保存已标定通道及真实 Range/High。
