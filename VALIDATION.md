# 验证记录

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
