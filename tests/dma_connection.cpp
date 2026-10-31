// SPDX-License-Identifier: GPL-2.0
#include <Cdl5/DmaHandle.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <vector>
#include <QCoreApplication>
#include <QSemaphore>

namespace {
enum class Failure { None, DmaOpen, Caps, Unsupported, Capacity, ZeroCapacity, MemoryOpen, Mapping };
Failure failure = Failure::None;
std::vector<std::string> events;
std::array<std::byte, 8192> memory;
QThread *openedOn = nullptr;
QThread *closedOn = nullptr;
int dmaFlags = 0;
int memoryFlags = 0;
std::size_t mappedBytes = 0;
off64_t mappedOffset = 0;
std::atomic<int> checks{0};
void check(bool value, const char *message)
{
    ++checks;
    if (!value) {
        std::cerr << "FAIL " << message << '\n';
        std::exit(1);
    }
}
void reset(Failure next = Failure::None)
{
    failure = next;
    events.clear();
    openedOn = closedOn = nullptr;
}
DmaParameters parameters()
{
    return {"/fake/dma", "/fake/mem", 0x80002004u, 4096};
}
class ProbeHandle final : public DmaHandle
{
public:
    ProbeHandle() : DmaHandle([](const QByteArray &, RxTime) {}) {}
    ~ProbeHandle() override { stop(); }
    QSemaphore connected;
    int errorCount = 0;
    int closeCount = 0;
private:
    QSemaphore wake;
    void connectionChanged(bool value) override
    {
        if (value)
            connected.release();
        else
            ++closeCount;
    }
    void logMessage(const QString &) const override { ++const_cast<ProbeHandle *>(this)->errorCount; }
    bool connLoop(std::shared_ptr<ConnType>, Sink) override
    {
        // 测试只驻留连接，不提交 DMA、不读写寄存器。
        wake.acquire();
        return true;
    }
    void connBreak(std::shared_ptr<ConnType>) const override
    {
        const_cast<ProbeHandle *>(this)->wake.release();
    }
    bool connSend(std::shared_ptr<ConnType>, const QByteArray &) const override { return false; }
    RXRuling onRX(const QByteArray &, RxTime, std::shared_ptr<ConnType>) override { return {}; }
    TXRuling onTX(const QByteArray &, std::shared_ptr<ConnType>) const override { return {}; }
};
}

extern "C" long __wrap_sysconf(int) { return 4096; }
extern "C" int __wrap_open64(const char *path, int flags, ...)
{
    openedOn = QThread::currentThread();
    if (std::string_view(path) == "/fake/dma") {
        events.emplace_back("open-dma");
        dmaFlags = flags;
        if (failure == Failure::DmaOpen) { errno = EBUSY; return -1; }
        return 101;
    }
    check(std::string_view(path) == "/fake/mem", "mapping device path");
    events.emplace_back("open-mem");
    memoryFlags = flags;
    if (failure == Failure::MemoryOpen) { errno = EACCES; return -1; }
    return 102;
}
extern "C" int __wrap_close(int fd)
{
    check(fd == 101 || fd == 102, "close owned fd");
    events.emplace_back(fd == 101 ? "close-dma" : "close-mem");
    closedOn = QThread::currentThread();
    // 成功的清理调用也不保证保留 errno；故意覆盖以检验原始错误已被保存。
    errno = EBADF;
    return 0;
}
extern "C" int __wrap_ioctl(int fd, unsigned long command, ...)
{
    check(fd == 101, "ioctl DMA fd");
    va_list args;
    va_start(args, command);
    auto *out = va_arg(args, __u32 *);
    va_end(args);
    if (command == CDL5_DMA_CAPS) {
        events.emplace_back("caps");
        if (failure == Failure::Caps) { errno = ENOTTY; return -1; }
        *out = failure == Failure::Unsupported ? 0u : 1u;
    } else {
        check(command == CDL5_DMA_CAPACITY, "resource layer never starts or resets DMA");
        events.emplace_back("capacity");
        if (failure == Failure::Capacity) { errno = EIO; return -1; }
        *out = failure == Failure::ZeroCapacity ? 0u : 2097152u;
    }
    return 0;
}
extern "C" void *__wrap_mmap64(void *, size_t length, int prot, int flags, int fd, off64_t offset)
{
    events.emplace_back("mmap");
    check(fd == 102 && prot == (PROT_READ | PROT_WRITE) && flags == MAP_SHARED, "mapping permissions");
    mappedBytes = length;
    mappedOffset = offset;
    if (failure == Failure::Mapping) { errno = ENOMEM; return MAP_FAILED; }
    return memory.data();
}
extern "C" int __wrap_munmap(void *address, size_t length)
{
    events.emplace_back("munmap");
    check(address == memory.data() && length == mappedBytes, "unmap original base and full length");
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    static_assert(std::is_abstract_v<DmaHandle>);
    static_assert(!std::is_copy_constructible_v<DmaConnection>);
    static_assert(!std::is_move_constructible_v<DmaConnection>);
    reset();
    {
        auto conn = DmaConnection::open(parameters());
        check((dmaFlags & O_ACCMODE) == O_RDONLY && (dmaFlags & O_CLOEXEC), "DMA read-only and close-on-exec");
        check((memoryFlags & O_ACCMODE) == O_RDWR && (memoryFlags & O_CLOEXEC) && (memoryFlags & O_SYNC), "register memory flags");
        check(mappedOffset == 0x80002000 && mappedBytes == 4100, "unaligned base crossing page boundary");
        check(conn->registers() == memory.data() + 4 && conn->registerBytes() == 4096, "exposed register subrange");
        check(conn->capacity() == 2097152 && conn->dmaDescriptor() == 101, "queried capacity and borrowed descriptor");
        check(events.back() == "close-mem", "memory fd closed after mapping");
    }
    check(events[events.size() - 2] == "munmap" && events.back() == "close-dma", "reverse resource cleanup");

    for (const auto point : {Failure::DmaOpen, Failure::Caps, Failure::Unsupported,
                            Failure::Capacity, Failure::ZeroCapacity, Failure::MemoryOpen, Failure::Mapping}) {
        reset(point);
        bool rejected = false;
        try { (void)DmaConnection::open(parameters()); }
        catch (const std::system_error &error) {
            rejected = true;
            if (point == Failure::DmaOpen)
                check(error.code().value() == EBUSY, "exclusive-open error retained");
            if (point == Failure::MemoryOpen)
                check(error.code().value() == EACCES, "memory-open error retained across DMA cleanup");
            if (point == Failure::Mapping)
                check(error.code().value() == ENOMEM, "mmap errno retained across close");
        }
        check(rejected, "failure propagated");
        check(events.back() == (point == Failure::DmaOpen ? "open-dma" : "close-dma"), "partial failure closes DMA");
        check(std::find(events.begin(), events.end(), "munmap") == events.end(), "failed mapping never unmapped");
        if (point == Failure::Mapping)
            check(events[events.size() - 2] == "close-mem", "failed mmap closes memory fd");
    }
    for (int which = 0; which != 3; ++which) {
        reset();
        auto params = parameters();
        if (which == 0) params.registerBytes = 0;
        if (which == 1) params.registerBase = UINT64_MAX;
        if (which == 2) params.registerBytes = SIZE_MAX;
        bool rejected = false;
        try { (void)DmaConnection::open(params); }
        catch (const std::system_error &) { rejected = true; }
        check(rejected && events.empty(), "invalid address or size rejected before opening");
    }
    reset();
    {
        ProbeHandle handle;
        check(handle.start(parameters()), "Handle accepts startup");
        check(handle.connected.tryAcquire(1, 2000), "Handle opens in worker");
        handle.stop();
        check(openedOn == &handle, "resource acquisition on worker");
        check(closedOn == &handle || closedOn == app.thread(), "cleanup completed by worker or final stop borrower");
        check(!handle.isRunning() && !handle.isOpen(), "stop joins worker and releases connection");
        check(handle.closeCount == 1, "one close notification");
        const auto count = events.size();
        handle.stop();
        check(events.size() == count && handle.closeCount == 1, "repeated stop does not duplicate cleanup");
        check(handle.start(parameters()), "connection can reopen");
        check(handle.connected.tryAcquire(1, 2000), "reopened connection ready");
    }
    check(events.back() == "close-dma", "final destructor synchronously closes");
    reset(Failure::MemoryOpen);
    {
        ProbeHandle handle;
        check(handle.start(parameters()) && handle.wait(2000), "failed initial open exits worker");
        check(!handle.isOpen() && handle.errorCount >= 1 && events.back() == "close-dma", "failure logged and cleaned");
    }
    std::cout << "PASS " << checks.load() << " checks; no DMA submitted, no FPGA registers written\n";
}
