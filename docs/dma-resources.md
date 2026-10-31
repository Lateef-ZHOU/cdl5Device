# DMA 连接资源边界

本层来自 DmaProxy/userspace；只迁移维护归属、包含路径与构建入口。驱动 ABI、打开/失败清理/关闭逻辑未改。

`DmaParameters` 由具体采集 Handle 的构参函数交入 DMA 路径、寄存器内存设备路径、物理起址和有效长度，不在连接层写死 4K/5K 地址或尺寸。

打开顺序：DMA 字符设备使用 `O_RDONLY | O_CLOEXEC`；查询 CAPS（必须有 WAIT_KEEP）和非零 CAPACITY；再用 `O_RDWR | O_SYNC | O_CLOEXEC` 打开 `/dev/mem`，按页对齐映射指定寄存器范围。映射成功后立即关闭 `/dev/mem` 描述符。释放时先 munmap，再关闭 DMA 描述符；每个失败路径回收已取得的资源，保存原始 errno。

映射对象是 FPGA 控制寄存器，不是 DMA 缓冲区。资源层只调用 CAPS/CAPACITY；具体 Cdl5DmaHandle 才提交 START/RESET、写寄存器和读取采集数据。只读打开不禁止具有副作用的 ioctl。

资源在 Handle 工作线程创建；临时共享借用的最后释放者可能在主线程或工作线程。保证同步 stop 返回前结束回调并释放连接，不承诺资源析构所在的线程。借用描述符和地址不得自行关闭或跨越连接寿命保存。

必须先停止传输及回调，再释放资源。驱动 WAIT_KEEP 持有互斥锁，RESET 不能越过正在等待的 ioctl；FPGA 停止命令也不等于立即停止在途帧。具体采集与取消顺序见 [采集协议](acquisition.md)。

## ARM 资源测试

已有板卡 SDK 容器 `lidar-arm-runtime-sdk:20261030` 内执行 `sh /src/tests/build-arm.sh`，挂载：

- `/src`：本仓源码（只读即可）。
- `/proxy`：DmaProxy 共享 ABI 头所在目录。
- `/handle`：固定版本 HandleCore 源码。
- `/work`：已有 board-runtime-20261030 SDK / glibc sysroot。
- `/out`：可写测试产物目录。

脚本使用 Qt Core 6.8.3、GCC 12 与匹配旧 glibc 的 CRT，不用宿主新 glibc 启动对象。输出 `cdl5_dma_connection_probe-arm64` 和 `cdl5_dma_capture_probe-arm64`。测试包装 open/ioctl/mmap/close/read 等系统调用，并模拟寄存器；不打开真实 DMA 或 /dev/mem，不干扰真实采集服务。

测试保留原 100 项断言：只读权限、跨页映射、各阶段失败回收、EACCES/ENOMEM 在 close 改写 errno 后仍保留、重开/重复停止/析构。计数器使用原子变量。历史验证保留在 DmaProxy/validation，迁移后的重新验证见本仓 VALIDATION.md。
