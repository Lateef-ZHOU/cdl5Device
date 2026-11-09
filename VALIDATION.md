# 验证记录

## 2026-11-09：零风速参考硬编码与 qmlcache 警告豁免（用户定）

- 零风速参考频率改为 `Configuration.h` 的 `ZeroWindReferenceMHz = 80.0`，所有规格与通道共用；删除 `readReferenceMHz`、`ReferenceMHz` 与 QSettings/QCoreApplication 依赖，`radial` 直接收参考频率，越出本帧频带仍只出 CNR。三个页面未换算时的日志改为“零风速参考频率不在本帧频带内，风速缺测”。首 bin 15 时两种频点数的频带都覆盖 80 MHz，因此现行三种规格风速均参与换算。
- `cmake/Device.cmake` 对 `.rcc/qmlcache/` 生成文件加 `/wd4702`，与其他官邸一致；原 Release 整编的 12 条 Qt 头 C4702 即来自三个 Module.qml。
- 验证：主程序原 build 源码覆盖 Release 整编零警告，三个 Module 的 qmlcache 均重编。本仓新建 build-1109（UDP+Device，相邻公共库源码覆盖）Release 零警告，CTest 3/3（udp、collector、page，7.69 秒）；collector 删除 ini 读取用例，改为常量断言及频带外参考仍出相同 CNR。旧 build-win 缓存指向迁移前 Documents 路径，未复用。未连接实物或改动板卡。

## 2026-11-03：九图平铺与标签精简

- 三规格页面的 ChannelPlots 只持有三张 Page 及原有接线，主页面直接展开三个通道的列表；无内层 TabPane，不增加后端或数据缓存。副标题按配置频率生成，尺寸和通道数不再重复显示于设备标签。
- Windows Qt 6.11.2/MSVC：由相邻 LiDAR_DataACQ 原 build 源码覆盖接入，Release 主程序与 laser_assembly 增量构建通过，无 C++ 编译警告；CMake 生成的本模块 qmllint 参数检查通过。
- 主程序 D3D11 装配验证运行 8 秒自然退出 0、stderr 为空：TenK 九个页面、三 ADC 独立数据接收及历史保留通过，截图确认单行九标签。测试未连接设备。FourK/FiveK 本轮编译及 lint，未单独运行三规格 smoke；旧 smoke 的页面数量断言同步为九。
- 证据在 LiDAR_DataACQ/build：configure-flat-charts.log、build-flat-charts.log、lint-flat-charts.log、assembly-flat-charts.stdout.log / stderr.log、flat-charts.png。仅本地更新原入口，不推送、不发 Release，不改板端或采集逻辑。

## 2026-11-03：10 kHz FPGA 逻辑替换、重启与真实采集

- 用户授权备份、改写 BOOT.BIN、重启并验证。SD 备份目录 `/media/sd-mmcblk1p1/codex_fpga10k_backup_20261103_161849` 保留旧 BOOT.BIN、SHA256 和恢复说明；本机副本及证据位于 `out/fpga-10k-20261103/`。旧镜像 SHA256 为 `079bcca7d0297f133184f1d7fd568c61ef7f17b7ea87ad89ec0ff151b34ca75b`。
- 使用 capture-10k/20_runtime 的 `ad9695_prod3_10k240_dualrange_unscaled_dcrem_acdc_trigio_u64_0x26090301.bit.bin`，SHA256 `46f410266ad6b6b3d222378d72b591ee11edaf9effb5b47a7242352ec47640e9`。仅替换原镜像等长 PL 分区（偏移 251136、长度 26510780）并更新分区名称；FSBL/PMUFW、BL31、内嵌设备树、U-Boot 分区逐字节不变。此镜像无加密/签名/载荷校验字段，头部校验、各分区长度和属性保持；官方 Bootgen 2020.1 读取新旧镜像通过。这是对本次特定镜像的检查，不作为任意 BOOT.BIN 均可原位修改的规则。
- 新 BOOT.BIN 大小仍为 27756036 字节，SHA256 `918e0450766b826a942265179e5676ede68acc7ecafccc78e1322d107b3c3a57`。先停止两桥接服务，上传临时文件、校验并替换、sync 后正常重启；boot_id 由 `9cb921bd-df21-4cae-bb86-d77231ab5239` 变为 `65a5b230-ab68-4c17-8563-84156b6fa890`。未直接覆盖交付包中的完整 BOOT.BIN，保留板卡既有启动链。
- 重启后实际寄存器：Version=`0x26090301`，Geometry=`0x7600f0f0`（P7、6 lane、240×240），Period=25000（250 MHz 下为 10 kHz），FrameWords=460808，ADC mask=7，Accum=10000，JESD=`0x64ff8`。旧逻辑更新前同一只读探针读到 P8/450×128/Period=50000。代理驱动自动加载并注册 `/dev/cdl5_dma_proxy`；laser-bridge PID848、cdl5-bridge PID890 自动启动，Device/UDP 连接成功，硬件 watchdog 仍在运行。
- 通过 Windows 原物理网卡发送一次既有 8 字节、1 秒采集命令，经过生产 Pcap→Bridge→DMA→Bridge→UDP 路径。Windows 捕获完整一帧 1843232 字节，帧头为 `324c4443 05040607 00f000f0 000061a8 2710700f 00000002 00000000 00000000`；三 ADC 各 57600 点，小端 FP32 独立解码全部有限、非负、非零。帧 SHA256 `1ecedf7f02be92cbf27209e2bece1d2b09658c2d6fdcc0e91ce3561cf347293e`。接收仍为 257×7168+1024+32 字节批次，未改变既有合并处理。首次离线检查仅因 dumpcap 输出纳秒 PCAP 魔数而拒绝，兼容该文件头后重读同一捕获通过，未因此重新发起采集。
- 采后 Enable/Busy/RunState 全为 0，两服务仍正常。boot.scr、image.ub、root-home.tar.gz、代理模块、bridge 与旧 V52 初始化程序的 SHA256 均与更新前相同；没有操作 eMMC 科学数据，没有发送激光器命令。此次验证真实采集链路与完整帧，不包括 Windows UI 绘图验收、激光功能、多秒累积、风速标定或长期无丢包保证。
- 启动日志出现 SD FAT 分区“Volume was not properly unmounted”提示，来源尚未确定；镜像和保护文件校验均通过。本轮未对已挂载分区执行 fsck，也未扩展为文件系统修复。只读寄存器探针经 `/dev/mem` 的 O_RDONLY/PROT_READ 映射；板上没有 devmem/lsmod 命令，对应首轮命令失败不计验证通过，驱动状态由字符设备、内核注册日志及真实 DMA 采集确认。
- 证据：`backup.json`、`image-verification.json`、`boot-before.txt`/`boot-10k.txt`、`install.log`、`deployment.json`、`registers-before.txt`/`registers-after.txt`、`ten-k-real-frame.pcap`/`.bin`、`capture-result.json`、`final-check.log`。仅同步维护记录，不改变生产代码，不推送或新发 Release。

回退：关闭板卡后通过读卡器，将上述 SD 备份目录内的 BOOT.BIN 复制到 SD 启动分区根目录替换现文件，核对旧 SHA256，安全弹出并重启。其他镜像和 eMMC 不需要恢复。回退后桌面须选用 FiveK 规格；本轮保留备份但未实际执行回退。

## 2026-11-03：4/5/10 kHz 三规格官邸

- Windows/MSVC/Qt 6.11.2 Release 构建、五个公开头独立编译、qmllint 通过。最终编译和 lint 无新增警告；首次发现 Profile 比较的 signed/unsigned 诊断，显式使用协议字段类型后通过。CTest 3/3：UDP、三种 Collector、三个独立页面各自创建/接线/销毁，确认唯一后端、DAQ 族引用和具体原型类型。
- Collector 合成完整帧逐点核对三路小端 FP32→double；覆盖 1024 字节块、32 字节尾、7168 字节合并批次、缺尾、超量、错误尺寸/lane/Profile、轻量只读原型及重试独立收集区。P7 混合门起点 0..5340 m、统一模式 0..14340 m；P9 末门 34350 m。型号累积上限 16/13/6 秒按 16 位计数器计算并检查。
- ArchHFNL/Linux x86_64/GCC 16.2.1/Qt Core 6.8.3：DMA 模块 Release / Werror、单头、CTest 2/2 通过，新增 P7 六 lane 成功和错误五 lane 拒收；4/5 kHz、短读/EINTR、取消及 DMA 错误回收回归通过。该机当前没有桌面 Qt/Ninja，因此使用现有 Qt Core SDK 和 Make；未声称运行 Linux 图形页面。
- 现有 PetaLinux SDK 交叉编译 ARM64 成功；修正测试构建入口，补编译 ResourcePhase 的 moc（此前脚本未跟随资源信号改造）。两个 ARM 测试上传 /tmp 后在板上执行通过，连接测试 100 检查，采集测试覆盖三规格。全部系统调用使用替身，不触发真实 DMA/FPGA，不修改板上程序或镜像。
- 证据：本地 out/{build-profiles-final,lint-profiles,test-profiles,test-arm-board}.log；远端 ~/cdl5-profiles-20261103/out/。此前真实帧仍是 5 kHz/P8；10 kHz 的实采、触发和多秒累积必须等待用户刷入新逻辑验证。三规格均保留原始谱幅值，不提前实施归一化待办。


## 2026-11-02 三路非均匀风图

Windows Release 构建、公开头与 LidarCdl5Device_qmllint 通过。CTest 3/3，页面探针实例化 ADC0/1/2 各自的谱、风速/CNR 与风羽，接收合成剖面和整列。已知谱峰按真实起始 bin 和参考频率得到预期径向风速，未标定时风速缺测、CNR 相同。两份既有实机捕获帧离线回放均为 3×450 门、起始 bin 15、斜距 0..26850 m。未采集新的硬件数据。首次独立测试暴露间接旧 ChartCore 不含非均匀组件及测试对 CNR 正负的不当预期，固定实际提供该组件的版本并按可计算的理论 CNR 校验后通过。


## 2026-11-02：资源阶段通知（用户定）

- DMA AcquisitionHandle 采用 ResourcePhase 信号；连接资源层/FPGA 配置/分块和接收逻辑不变，页面状态按完成事件读取已有 DeviceBase。
- Linux ArchHFNL Qt Core 6.8.3 / GCC 16.2.1 独立 DMA 构建、公开头与 CTest 2/2 通过（系统调用替身：连接/采集）。证据 ~/resource-phase-20261102/cdl-{configure,build,test}.log。Windows 页面随主程序 Release 编译与装配验证，未操作板卡或重启服务。依赖仅本地固定，未推送。

## 2026-10-31：独立业务地址对照（用户定）

- 默认 Windows 192.168.100.200 ↔ 板端 192.168.100.100，端口不变；仅修改型号构参，不改 Handle、Collector、分块或协议。无需将这两个地址配置到系统网卡。
- Windows Release 增量构建及 CTest 3/3 通过，含 8 秒页面验证；证据 build-win/build-ip100.log、test-ip100.log。
- 双端真实抓包结果记录于 BridgeCore 的 build-ip100 与部署验证，未据地址调整本身宣称消除合并。仅本地提交，不推送或发 Release。


## 2026-10-31：真实抓包诊断与接收修复（用户定）

Windows 无谱时，在用户发起的采集中被动抓包，并只读核对板卡寄存器。捕获到两帧、518 个上行数据报：514 个 7168 字节、2 个 1024 字节、2 个 32 字节。每帧 1,843,232 字节，帧头为 450×128/Profile 8/周期 50000；寄存器 FRAME_WORDS=0x70808、Geometry=0x8501C280、Period=0xC350 一致。此前“尚未换 FPGA”的描述不能替代活动数据规格；用户据此确认 Windows 按实际 450×128 接收，不修改板卡。

两个拒收点分别是 Collector 的 1024 字节上限及 500×128 形状检查。去掉接收块长上限，保留非空、32 字节对齐、帧头及剩余容量检查；默认类型改为 Cdl5FiveKCollector。DMA 仍每次最多交付 1024 字节、Bridge 仍原样转发。Windows 网卡开启接收合并，7168=7×1024，与接收卸载的行为一致；没有修改网卡配置，也未在板端另抓线上包。

- Windows Release 增量构建及单头编译通过，无 C++ 警告；CTest 3/3、qmllint 通过。新增两规格的 7168 字节合并批次、未对齐拒收、超出剩余容量拒收、缺尾不完成及最终完整数值一致性验证。
- 真实以太帧离线回放经过生产 PcapProtocol::kindOf / UDPDatagram::bodyView / Collector，准确完成两帧、三路各 57,600 点。六份结果逐点与 Python 独立小端 FP32 解码后转 double 的值比较，全部一致且非零；没有拆分合并批次再喂入。
- ArchHFNL 增量构建及 CTest 6/6 通过，含生产消费/三路保存链和 DMA 模拟系统调用回归。仍不代表实际 DMA/网络永久无损，也不代替更新主程序后的用户现场验收。
- 本地证据：out/cdl5-live-windows.pcapng、out/board-live-diagnosis.txt、out/capture-replay/replay.log、out/build-coalesced.log、out/test-coalesced.log、out/lint-coalesced.log。Linux：~/.cache/cdl5Device-20261031/out/build-coalesced.log、test-coalesced.log。现场数据和一次性回放程序留在被忽略的 out，不进入仓库。

未操作 FPGA、板卡服务、SD 镜像或 eMMC；未改变 HandleCore、BridgeCore、重试和取消链。仅本地提交，不推送。

## 2026-10-31：按实物确认切回 4 kHz（用户定）

用户确认尚未更换 FPGA 逻辑，板卡仍为 4 kHz。只将 Cdl5ActiveCollector 改为 Cdl5FourKCollector（500×128）；三路收集长度、页面尺寸和保存点数由同一类型导出，不修改协议、Handle、DMA 或 Bridge。之前的 5 kHz 实物判断撤回，两个规格类型均保留。

Windows Release 增量构建（含独立头文件）通过，无新增编译诊断；CTest 3/3（UDP、两规格 Collector、8 秒 QML 页面）通过。证据 out/build-four-k.log、out/test-four-k.log。本轮未重跑 Linux、未做实卡采集，不改板卡服务或镜像、不推送。

## 2026-10-31：Windows 主程序接入准备

此阶段曾将默认桌面 Collector 改为 Cdl5FiveKCollector（450×128）。随后用户确认未更换 FPGA、实物仍为 4 kHz，此处规格判断已纠正；下列测试结果保留为当时的 450×128 软件验证，不能据此判定实物版本。Handle、DMA、协议和重试逻辑未改。

- Windows Release 增量构建与单头编译通过，未见 C++ 编译警告；CTest 3/3，QML 冒烟运行 8 秒；LidarCdl5Device_qmllint 通过。
- 页面验证新增 DAQ 族归属、唯一后端/页面所有权、只读具体 Collector 原型、离线启动拒绝和页面销毁释放检查。
- ArchHFNL 同步本轮源代码后构建通过，CTest 6/6。生产 Device → Collector → consumeFrames 测试按当前 450×128 规格收齐三路数据、发出三个信号，并逐项核对三个落盘文件；每份 57,600 个小端 double（460,800 字节）。两个 Collector 规格的独立测试继续通过。
- 证据：本机 out/build-windows-integration.log、out/lint-windows-integration.log、build-win/Testing/Temporary/LastTest.log；ArchHFNL ~/.cache/cdl5Device-20261031/out/build-windows-integration.log 与 build-linux/Testing/Temporary/LastTest.log。

以上使用模拟抓包和系统调用，没有触发实际 FPGA 采集。主程序完整装配结果记在 LiDAR_DataACQ 的维护验证记录；本轮不更新板卡桥接器、不推送或发布 Release。

## 2026-10-31：三路原谱、UDP 官邸与 DMA 采集

本轮新实现已经构建验证，尚未真实 FPGA 联调。未接入主程序，未安装新板端服务、改驱动/镜像或 eMMC；没有推送。

| 环境 / 入口 | 实际结果 |
|---|---|
| Windows x64 / MSVC 19.51 / Qt 6.11.2 / Npcap | UDP+桌面 Release 构建无 C++ 警告，单头编译通过；CTest 3/3；CMake qmllint 通过 |
| ArchHFNL x86_64 / GCC 16.2.1 / Qt 6.12.0 / libpcap 1.11 | DMA+UDP+桌面 Release 构建通过；CTest 6/6 |
| Linux 只启用 DMA | 严格警告构建、CTest 2/2 通过；依赖只有 Qt Core，不引入 Pcap、串口、QML |
| Linux 所有可选模块默认关闭 | 重新配置和构建通过，无 Qt/传输依赖查询 |
| ARM64 / GCC12 / Qt Core6.8.3 / 板卡 glibc sysroot | 资源及采集测试交叉构建通过；所需 glibc 基线 GLIBC_2.17 |
| 板卡 /run/cdl5_dma_capture_probe-arm64 | 模拟系统调用/寄存器运行通过，退出0；未打开真实 DMA 或 /dev/mem |

覆盖内容：

- 两种 Collector（500×128、450×128）：三路小端 FP32 位解释后转 double，未对齐读取与小数保留；逐元素 meta 填任意字节仍按顺序追加；原型不 reserve、makeSelf 独立 reserve；缺尾包不完成、错误形状/非32字节整包拒绝、完成后不再写入。
- 生产 Device/Collector/consumeFrames 链：模拟 Pcap 收包后仅一次完成回调，三路谱信号在主线程收到；三份 ADC 文件各为64000个小端 double，逐项读取核对正确。没有在测试中替换 Device 或处理逻辑。
- DMA 实际 Handle 工作线程：只读打开，寄存器取得两种帧长/周期，START 在 FPGA 触发前；短 read/EINTR 补满块、2001/1801包及32字节尾包逐字节匹配；关闭在途采集丢弃并同步退出；START、WAIT_KEEP、read 失败和长度不完整均诊断、RESET 并关闭，无上传。
- 资源层原100项断言保持通过。QML冒烟运行8秒，核对三个图页、唯一后端归属及页面析构后的释放，无QML警告；这是离屏页面装配验证，不等于真实GPU画面验收。
- BridgeCore 同时验证生产 Cdl5DmaHandle → 主线程排队 → Pcap send，两个尺寸字节无改变，Linux与板卡ARM64模拟验证均通过。详细记录在 BridgeCore/VALIDATION.md。

证据：本机 out/build-final.log、out/build-final-headers.log、out/arm-board-final.txt 和 build-win/Testing/Temporary/LastTest.log；ArchHFNL 的 ~/.cache/cdl5Device-20261031/{build-linux,build-dma-only,out}。Qt桌面配置存在依赖包可选TaskTree/Vulkan查找提示，不是本轮C++编译警告。HandleCore 与 TaskCore 使用固定远端提交；板端 SDK 测试使用这些版本的本地源码挂载。

物理风速/CNR、距离门映射及风廓线本轮未实现。真实 FPGA 的停止/恢复、UDP 丢包及吞吐仍待部署联调；模拟数据通过不作实采结论。

## 2026-10-31：初始资源层迁移（历史阶段）

2026-10-31：创建本地仓库并迁入连接资源层，本轮验证已完成。

| 环境 / 入口 | 实际结果 |
|---|---|
| Windows x64，MSVC 19.51，Qt Core 6.11.2，Npcap 内含 libpcap 1.10.6 | UDP Release 构建通过；测试使用 /W4 /WX，构建无警告；CTest 1/1 |
| Windows 显式启用 CDL5_WITH_DMA | 配置按预期失败，明确提示 DMA requires Linux；没有进入 Linux 头或驱动依赖查询 |
| ArchHFNL x86_64，GCC 16.2.1，Qt Core 6.12.0，libpcap 1.11.0 | UDP + DMA 构建通过，测试使用 -Wall -Wextra -Wpedantic -Werror；CTest 2/2；DMA 100 项断言通过 |
| 同一 Linux，单独启用 DMA | 构建与 CTest 1/1 通过；没有启用 Pcap / libserialport / QML / GUI；两个 DMA 公开头分别独立编译 |
| 同一 Linux，两个模块均保持默认关闭 | 配置和构建通过，不查找 Qt 或传输依赖 |
| ARM64，已有 SDK 容器，GCC 12、Qt Core 6.8.3、板卡 glibc sysroot | 严格警告交叉构建通过；测试程序所需 glibc 符号版本为 GLIBC_2.17 |
| PetaLinux 板卡 192.168.1.11，/run/cdl5_dma_connection_probe-arm64 | 实际执行 100 项模拟系统调用断言通过，退出码 0 |

Windows 和 Linux 双接入构建均由 FetchContent 从远端获取 HandleCore，并核对检出为 `3470a1d9f3826f4a666b0e53c29c001f0901fbbd`；未使用源码覆盖。Linux DMA 单独构建也使用相同固定依赖。

UDP 测试实际链接所选 Pcap 模块，检查带零字节的 32 / 1024 字节载荷经过 UDP 封装后原样取回、离线发送失败仅报告一次以及重复停止。没有打开真实网卡，没有发送数据，不是网络吞吐量或丢包验证。

DMA 测试沿用原 100 项资源断言，仅调整包含路径；原生及 ARM 均包装 open/ioctl/mmap/close 等系统调用，实际使用 Qt 工作线程。板卡测试没有打开真实 DMA 或 /dev/mem，没有提交传输、触发 FPGA 或改动正在运行的服务。仅将测试程序写入 /run；没有替换驱动、镜像、root 家目录或 eMMC 数据。

## 迁移一致性

与 ArchHFNL 上前一轮已验证的源码快照核对，两份实现及共享 ABI 的 SHA256 完全一致：

| 文件 | SHA256 |
|---|---|
| DmaConnection.h | `3400d3fe356bdf79cd76851800e3daa177dd63e86fedea8408ab2ed222da31d3` |
| DmaHandle.h | `ee751fe4f5abc032cab248f29c7d36b9e17850352301825f592934273b1552e4` |
| 外部 cdl5-dma-proxy.h | `1c80cda01225f1cd528c76ec5b79a37fc9c246a63c7b575fdaf039207ec5629a` |

旧用户态实现、测试及构建入口从 DmaProxy 移出；其 README 指向本仓，历史资源验证仍在原 validation 目录。驱动与 ABI 不迁移、不复制。

## 初始资源层阶段的范围（后续以上方新记录为准）

没有完整 DMA 开采、等待、分块读取、取消或 FPGA 配置；没有 UDP 下行采集命令、Collector、三路处理、保存或 QML 页面。因此未执行主程序 UI 冒烟或真实采集测试，也未接入 Bridge、主程序、simonly 或旧 sim-ui。未创建远端、推送、发布 Release 或修改交付包。

## 2026-11-03 回调类型声明整理

DMA capture/connLoop 与测试直接声明 std::function，删除 Sink/DataCallback 别名依赖。Linux/GCC/Qt Core 6.8.3 增量构建及 CTest 3/3 通过（virtual-upload-20261103/build-cdl5Device/callback-alias-{build,test}.log），覆盖连接资源、采集和三规格收集；桥接器构建及 CTest 4/4 通过。分块、ioctl、取消、线程及关闭顺序不变；未更新板卡或驱动。

## 2026-11-03 网口连接显示精简

2026-11-03 用户定：网口连接区仅保留网卡选择和连接/断开，删除 IP、端口等固定参数展示及对应 QML 参数字段；实际连接配置、校验、ARP 和收发关闭行为保持。

Windows 验证：通过主程序本地源码覆盖增量构建，五个网口官邸 qmllint 目标成功；仍有 QObject 属性类型的既有静态提示。八秒离屏冒烟通过，仅既有字体提示；未进行硬件操作或 Linux 验证。证据：LiDAR_DataACQ/build/build-network-ui.log、smoke-network-ui.json。


2026-11-05（用户定）：4/5/10 kHz各ADC风廓线页删除等待提示、占位布局及profileNote显隐接线。主程序Release增量构建与八秒离屏冒烟通过；保留既有Qt C4702及字体提示，无新增运行诊断。证据在LiDAR_DataACQ/build/build-profile-note-library.log、build-profile-note.log、smoke-profile-note.json。未做硬件采集；原启动入口更新，未推送。

## 2026-11-06 Windows ADC0 三门宽适配（用户定）

Release 构建及五份公开头单独编译通过，CTest 3/3。Collector 验证含 4/5/10 kHz 收集、分块与合并批次、三门宽坐标和四束高度、ADC0 共用标定及帧规格 JSON。QML 验证覆盖三规格页面、新版九个标题、逐路斜距及风廓线范围与功率谱轴参数。交付包三帧使用 `cdl5_collector_probe --adc0 <frame...>` 回放，每帧三路完整，首 bin 15，斜距末值 1434/3585/7170 m。

本轮使用本地 ChartCore b882fb1 和相邻依赖源码覆盖。qmllint 保留 QObject 属性静态类型提示，Release 保留 Qt 生成代码 C4702 基线。实机数据来自交付包，当前未操作板卡或进行在线采集。主程序的构建和启动证据见其维护验证。
