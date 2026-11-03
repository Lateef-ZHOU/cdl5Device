// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "cdl5-dma-proxy.h"

struct DmaParameters
{
    std::string dmaDevice = "/dev/cdl5_dma_proxy";
    std::string memoryDevice = "/dev/mem";
    std::uint64_t registerBase = 0;
    std::size_t registerBytes = 0;

    bool operator==(const DmaParameters &) const = default;
};

// 连接资源在 Handle 工作线程创建；最后一个临时借用退出后按逆序释放。
// 不启动 DMA、不写 FPGA 寄存器；型号及采集顺序由上层提供。
class DmaConnection final
{
public:
    DmaConnection(const DmaConnection &) = delete;
    DmaConnection &operator=(const DmaConnection &) = delete;
    DmaConnection(DmaConnection &&) = delete;
    DmaConnection &operator=(DmaConnection &&) = delete;

    static std::shared_ptr<DmaConnection> open(const DmaParameters &params)
    {
        const long page = ::sysconf(_SC_PAGESIZE);
        if (page <= 0)
            throw std::system_error(EINVAL, std::generic_category(), "sysconf(_SC_PAGESIZE)");
        const auto pageBytes = static_cast<std::uint64_t>(page);
        const auto aligned = params.registerBase - params.registerBase % pageBytes;
        const auto offset = params.registerBase - aligned;
        if (params.dmaDevice.empty() || params.memoryDevice.empty()
            || params.registerBytes == 0
            || aligned > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())
            || offset > std::numeric_limits<std::size_t>::max() - params.registerBytes
            || params.registerBytes - 1 > std::numeric_limits<std::uint64_t>::max() - params.registerBase)
            throw std::system_error(EINVAL, std::generic_category(), "invalid DMA mapping parameters");

        // 先交给 RAII；后面的 ioctl/open/mmap 任一步失败都释放已经取得的资源。
        auto conn = std::shared_ptr<DmaConnection>(new DmaConnection);
        conn->dmaFd_ = ::open(params.dmaDevice.c_str(), O_RDONLY | O_CLOEXEC);
        if (conn->dmaFd_ < 0)
        {
            const int code = errno;
            throw std::system_error(code, std::generic_category(), "open DMA " + params.dmaDevice);
        }
        __u32 caps = 0;
        if (::ioctl(conn->dmaFd_, CDL5_DMA_CAPS, &caps) < 0)
            fail("ioctl DMA CAPS");
        if ((caps & 1u) == 0)
            throw std::system_error(ENOTSUP, std::generic_category(), "DMA WAIT_KEEP is required");
        if (::ioctl(conn->dmaFd_, CDL5_DMA_CAPACITY, &conn->capacity_) < 0)
            fail("ioctl DMA CAPACITY");
        if (conn->capacity_ == 0)
            throw std::system_error(EINVAL, std::generic_category(), "DMA capacity is zero");

        const int memoryFd = ::open(params.memoryDevice.c_str(), O_RDWR | O_SYNC | O_CLOEXEC);
        if (memoryFd < 0)
        {
            const int code = errno;
            throw std::system_error(code, std::generic_category(), "open register memory " + params.memoryDevice);
        }
        conn->registerOffset_ = static_cast<std::size_t>(offset);
        conn->registerBytes_ = params.registerBytes;
        conn->mapping_ = ::mmap(nullptr, conn->registerOffset_ + conn->registerBytes_, PROT_READ | PROT_WRITE,
                                MAP_SHARED, memoryFd, static_cast<off_t>(aligned));
        const int mappingError = errno;
        // mmap 持有自己的映射引用，不需要让 /dev/mem 的文件描述符常驻。
        ::close(memoryFd);
        if (conn->mapping_ == MAP_FAILED)
            throw std::system_error(mappingError, std::generic_category(), "mmap FPGA registers");
        return conn;
    }

    ~DmaConnection()
    {
        // 将来增加 DMA 执行后，所属 Handle 必须先停止传输及回调，再释放连接。
        if (mapping_ != MAP_FAILED)
            ::munmap(mapping_, registerOffset_ + registerBytes_);
        if (dmaFd_ >= 0)
            ::close(dmaFd_);
    }

    // 借用口：调用方不得 close/munmap，不得在连接寿命之外保存地址或 fd。
    int dmaDescriptor() const noexcept { return dmaFd_; }
    std::uint32_t capacity() const noexcept { return capacity_; }
    volatile std::byte *registers() const noexcept
    {
        return static_cast<std::byte *>(mapping_) + registerOffset_;
    }
    std::size_t registerBytes() const noexcept { return registerBytes_; }

private:
    DmaConnection() = default;
    [[noreturn]] static void fail(const char *operation)
    {
        const int code = errno;
        throw std::system_error(code, std::generic_category(), operation);
    }

    int dmaFd_ = -1;
    void *mapping_ = MAP_FAILED;
    std::size_t registerOffset_ = 0;
    std::size_t registerBytes_ = 0;
    __u32 capacity_ = 0;
};
