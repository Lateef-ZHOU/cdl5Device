# cdl5Device

CDL5 采集卡官邸：桌面只使用 UDP；Linux 板端由 DMA Handle 接入 FPGA，BridgeCore 只转发字节。一个设备产生 ADC0、ADC1、ADC2 三路独立功率谱。

2026-10-31：已实现 DMA 启动/等待/读取/取消、顺序 Collector、三路功率谱 QML 页面及独立落盘。风速/CNR 和风廓线等待中频与距离门标定确认，未套用国实卡换算。实测帧头及只读寄存器为 Profile 8、450×128，用户确认 Windows 按实际数据接收。已回放两帧真实抓包并核对三路数值；这不代替更新后主程序的现场操作验收。

## 模块与构建

| 开关（默认 OFF） | 目标 | 用途 |
|---|---|---|
| CDL5_WITH_UDP | Lidar::Cdl5Udp | 复用 PcapHandle，Windows/Linux |
| CDL5_WITH_DMA | Lidar::Cdl5Dma | Linux Cdl5DmaHandle，Qt Core，无 QML |
| CDL5_WITH_DEVICE | Lidar::Cdl5Device | UDP 官邸、Collector、三张 SurfaceView，自动开启 UDP |

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

QML 导入 `Lidar.Cdl5` 并装配 `Cdl5Module`。页面独持精确 C++ 后端，向任务提供 DAQ 族引用，保留族级锁、Collector 原型/重试、消费线程及取消链；不提供 DMA 枚举或 DMA 桌面页面。

连接只选择本机网卡，其余型号参数在 C++：Windows 192.168.100.200:55200，板端 192.168.100.100:55200。三张图均用距离门/频点下标，不冒充物理距离或风速。Z 范围初始 0/0 自动全范围；按原谱显示，只有绘图器才钳制。

`Device.h` 的 Cdl5ActiveCollector 当前使用 Cdl5FiveKCollector，即 450 门 ×128 频点，与实测帧头一致；谱尺寸、收齐长度和图窗规格都来自该类型。Cdl5FourKCollector（500 门）仍保留，今后以实际帧头及用户确认切换，不仅凭交付包名称判断。两种 Collector 均已验证，切换不修改 Handle 或 Bridge。

完成一次采集，处理线程独立发出三路 spectrumReady；启用保存时通过现有 AcqSaver 写入：

```
Rawspec/<session>/ADC0_<frame-time>.bin
Rawspec/<session>/ADC1_<frame-time>.bin
Rawspec/<session>/ADC2_<frame-time>.bin
```

每份为无头、小端 IEEE-754 double，距离门为行、128 频点为列。沿用已有 detached 保存方式，不新增退出排空。原型为空数组；makeSelf 才 reserve 三路收集区，完成后只读共享给处理/绘图/保存。

## 字节协议与采集

详见 [采集协议](docs/acquisition.md)。要点：

- 下行统一 8 字节：小端 magic 0x354c4443，随后小端 uint32 累积秒数；1..13 开始，0 取消。13 秒在 4/5 kHz 两版均不会超过 16 位累积计数。单次触发，默认 1 秒。
- DMA 读取整帧所需的传输长度来自 REG_FRAME_WORDS，累积计数由当前活动触发周期计算；不改变触发频率、光源、通道映射或距离模式。
- 一帧一次 START，先准备 DMA 再触发 FPGA。WAIT_KEEP 短等待，完成后 rewind/read，每次交付最多 1024 字节自有 QByteArray，尾包交实际剩余字节。
- 第一包含 32 字节整帧头；随后每条 32 字节，前三个小端 FP32 分别转成 double，无符号整数仅用来读取位型。元信息不参与重排或去重。
- 1024 字节是 DMA 发送块长。Windows 抓包可见 7168 字节合并批次，接收按 32 字节对齐及整帧剩余容量校验，不要求一包最多 1024 字节，也不新增接收端拆包拷贝。
- 接收策略按本轮用户裁定：假定完整数据按顺序交付；缺包不判齐，由现有看门狗重试。不能检测 UDP 重复/乱序或重试旧包，不能宣称网络无损。
- DMA 突发发送必须先有对端 MAC；Bridge 的 cdl5Device 入口要求 --dstMac，避免已有 Pcap 的 ARP 等待单包槽覆盖前面的谱包。

## 验证与后续

[VALIDATION.md](VALIDATION.md) 记录 Windows、Linux、ARM64 结果和实测边界。测试替换系统调用/抓包接口，实际 Handle、Collector、Device 消费链保持生产实现。`tests/build-arm.sh` 使用现有板卡 SDK 构建测试，不安装应用、不重打镜像。

后续：使用当前实测 450×128 数据继续主程序现场联调；中频/距离门明确后接三路风速/CNR 和风廓线。两种规格的距离映射须分别确认，不能直接套用现有统一门距算法。

## 许可与发布

仍为本地仓库，未创建远端或推送。DMA 资源、采集与相关测试保留 GPL-2.0 来源标记；HandleCore 等依赖各自许可不变。整体发布许可仍待确认，不自动沿用其他官邸 MIT。仓库不包含厂商交付包、内核镜像或运行库。
