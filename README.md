# cdl5Device

CDL5 采集卡官邸的独立本地仓库。一个设备对应三路 ADC，后续共用采集协议、Collector、处理和页面，按部署选择 UDP 或 DMA 接入。

**当前只完成两种接入的构建边界和 DMA 连接资源层迁移。尚无完整采集、Collector、三通道处理或 QML 页面，也未接入主程序或 Bridge。**

| 接入 | CMake 目标 | 实现 | 平台 |
|---|---|---|---|
| UDP | `Lidar::Cdl5Udp` | 直接使用 HandleCore `PcapHandle` | Windows / Linux |
| DMA | `Lidar::Cdl5Dma` | `Cdl5/DmaHandle.h`、`Cdl5/DmaConnection.h` | Linux；实际运行还需匹配的 DMA 驱动与 FPGA |

UDP 没有额外的型号传输行为，因此不增加包装类或复制 Pcap 实现。两条接入均沿用 HandleCore 的 `QByteArray` 发送及 `DataCallback(const QByteArray &, RxTime)` 接收接口。DMA 资源层仍是抽象类；发令、收帧、取消尚未实现。

## 目录与依赖

- `include/Cdl5/`：从 DmaProxy/userspace 迁入的 DMA 资源实现；运行逻辑未改。
- `tests/`：UDP 离线交付边界、原 DMA 100 项资源断言、单头编译和 ARM 构建入口。
- `cmake/Dependencies.cmake`：HandleCore 远端固定 `3470a1d9f3826f4a666b0e53c29c001f0901fbbd`。
- `DmaProxy/cdl5-dma-proxy.h`：外部共享 ioctl ABI。由 `CDL5_DMA_PROXY_SOURCE_DIR` 显式指定，不复制、不编译驱动 C 文件。DmaProxy 目前是相邻本地模块；未来远端发布需先落实其独立版本入口。

仅需要 C++20、CMake 3.28、Qt Core >= 6.8.3。UDP 额外使用 libpcap >= 1.10.0，Windows 对应 Npcap SDK / 运行时；DMA 单独构建不查找 pcap、串口、QML 或绘图库。

两个模块默认关闭。消费项目按需启用，Linux 可以同时启用。若外部已经创建 HandleCore，则应在它配置之前开启所需 Pcap 模块。

## 构建和验证

Windows UDP（Qt 和 Npcap 的位置按本机调整）：

```powershell
cmake -S . -B build-udp -A x64 -DCDL5_WITH_UDP=ON `
  -DCMAKE_PREFIX_PATH=C:/path/to/Qt/msvc2022_64 `
  -DHANDLECORE_PCAP_INCLUDE_DIR=C:/path/to/Npcap/Include
cmake --build build-udp --config Release
ctest --test-dir build-udp -C Release --output-on-failure
```

Linux 两种接入：

```sh
cmake -S . -B build-linux -DCDL5_WITH_UDP=ON -DCDL5_WITH_DMA=ON \
  -DCDL5_DMA_PROXY_SOURCE_DIR=/path/to/DmaProxy
cmake --build build-linux -j2
ctest --test-dir build-linux --output-on-failure
```

Linux 只检查 DMA 时省略 `-DCDL5_WITH_UDP=ON` 即可。开发时可通过标准 `FETCHCONTENT_SOURCE_DIR_LIDAR_HANDLE_CORE=/path/to/HandleCore` 使用已有检出；默认从远端取得固定版本。

Windows 显式开启 DMA 将在配置时拒绝。单独 UDP 构建不要求 DmaProxy 路径。测试均不打开真实网卡、DMA 或寄存器；测试通过不代表 FPGA 实际采集已验证。结果见 [VALIDATION.md](VALIDATION.md)，ARM 入口及资源契约见 [DMA 说明](docs/dma-resources.md)。

## 后续接入顺序

1. DMA 整帧启动、等待、分块读取及取消；不能按每个 1024 字节重新启动 DMA。
2. 型号 FPGA 配置与触发、停止顺序，以及 UDP 下行命令约定；Bridge 只转发协议字节。
3. 共用 Collector 根据帧号、门号、频点取出三个独立 double 数组，完整性校验后交付。
4. 三通道各自处理、绘图、落盘及独立风廓线缓存；一个设备共用采集时刻、朝向和任务上下文。
5. 先打通 4K，再验证 5K 的尺寸与配置差异。

用户已定的传输单位：32 字节记录对齐，每块最多 1024 字节，帧尾交实际剩余长度；拆分由 DMA Handle 负责，Bridge 不切包。未来实现不新增保存排空或第二套停止机制。

## 源码许可与发布

本仓库目前仅本地维护，未创建远端或推送。迁入的 DMA 文件和测试保留原 `GPL-2.0` 标记，不自动套用其他官邸的 MIT 许可。HandleCore 保持其 MIT 许可，DmaProxy 的驱动及共享头保持原标记；整体发布许可在发布前另行确认。不包含厂商交付包、内核构建产物或运行库。
