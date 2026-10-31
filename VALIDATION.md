# 验证记录

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

## 当前未验证 / 未实现

没有完整 DMA 开采、等待、分块读取、取消或 FPGA 配置；没有 UDP 下行采集命令、Collector、三路处理、保存或 QML 页面。因此未执行主程序 UI 冒烟或真实采集测试，也未接入 Bridge、主程序、simonly 或旧 sim-ui。未创建远端、推送、发布 Release 或修改交付包。
