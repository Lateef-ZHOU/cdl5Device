// SPDX-License-Identifier: GPL-2.0
#pragma once
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <algorithm>
#include <sys/ioctl.h>
#include <unistd.h>
#include <QScopeGuard>
#include <cdl5-dma-proxy.h>
#include "DmaHandle.h"
#include "Protocol.h"

// Only the worker touches FPGA registers and the DMA descriptor. Owner-thread sends
// replace the pending request; stop() wakes that same worker and joins it via HandleCore.
class Cdl5DmaHandle : public DmaHandle
{
    Q_OBJECT
public:
    using DmaHandle::DmaHandle;
    ~Cdl5DmaHandle() override = default;
    static DmaParameters parameters(std::string path = "/dev/cdl5_dma_proxy")
    {
        return {std::move(path), "/dev/mem", 0x80002000, 4096};
    }
Q_SIGNALS:
    void logMessage(const QString &message) const override;
    void connectionChanged(HandleCore::ResourcePhase connected) override;

private:
    using Clock = std::chrono::steady_clock;
    enum Register : unsigned {
        Enable=0x04, Busy=0x08, Mask=0x0c, FrameWords=0x10,
        PeriodPending=0x14, Period=0x1c, Jesd=0x24, AccumPending=0x5c,
        ShiftPending=0x60, Accum=0x64, Shift=0x68, Epoch=0x74,
        BinPending=0x7c, Bin=0x80, Geometry=0x84, MapPending=0x88, Map=0x8c,
        RunState=0xb0, RangePending=0xb8, Range=0xbc, Apply=0xc0
    };
    static quint32 readRegister(const DmaConnection &conn, Register reg)
    {
        const auto value = *reinterpret_cast<volatile quint32 *>(conn.registers() + reg);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        return value;
    }
    static void writeRegister(const DmaConnection &conn, Register reg, quint32 value)
    {
        *reinterpret_cast<volatile quint32 *>(conn.registers() + reg) = value;
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
    static bool stopped(const DmaConnection &conn)
    {
        return ((readRegister(conn, RunState) >> 1) & 3) == 0 && readRegister(conn, Busy) == 0;
    }
    static void systemCheck(bool success, const char *action)
    {
        if (!success) throw std::system_error(errno, std::generic_category(), action);
    }
    static void require(bool valid, const char *message)
    {
        if (!valid) throw std::runtime_error(message);
    }
    std::shared_ptr<DmaConnection> connOpen(const DmaParameters &params) override
    {
        // Before Handle publishes the new connection or emits connectionChanged(Acquired).
        { const std::lock_guard lock(gate_); pending_.reset(); }
        if (params.registerBytes < Apply + sizeof(quint32) || params.registerBase % alignof(quint32)) {
            Q_EMIT logMessage(QStringLiteral("CDL5 寄存器映射范围或对齐错误"));
            return {};
        }
        return DmaHandle::connOpen(params);
    }
    void connBreak(std::shared_ptr<DmaConnection>) const override { post(-1); }
    bool connSend(std::shared_ptr<DmaConnection>, const QByteArray &bytes) const override
    {
        const auto request = Cdl5::decodeCommand(bytes);
        if (!request) return false;
        post(static_cast<int>(*request));
        return true; // Accepted by the worker; FPGA completion is not synchronous.
    }
    RXRuling onRX(const QByteArray &bytes, RxTime at, std::shared_ptr<DmaConnection>) override
    { return {bytes, RXRuling::Decode, at}; }
    TXRuling onTX(const QByteArray &bytes, std::shared_ptr<DmaConnection> conn) const override
    {
        if (!conn || !Cdl5::decodeCommand(bytes)) {
            Q_EMIT logMessage(QStringLiteral("CDL5 未连接或采集命令无效"));
            return {};
        }
        return {bytes, TXRuling::Encode};
    }
    void post(int request) const
    {
        { const std::lock_guard lock(gate_); pending_ = request; }
        wake_.notify_one();
    }
    bool interrupted() const
    {
        const std::lock_guard lock(gate_);
        return pending_.has_value();
    }
    // RESET cancels DMA only. A timed-out FPGA epoch also needs the documented PL soft reset.
    void releaseCapture(const DmaConnection &conn) const noexcept
    {
        writeRegister(conn, Enable, 0);
        if (::ioctl(conn.dmaDescriptor(), CDL5_DMA_RESET) != 0)
            Q_EMIT logMessage(QStringLiteral("CDL5 DMA RESET 失败：%1").arg(errno));
        if (!stopped(conn)) {
            writeRegister(conn, Enable, 1u << 16);
            const auto deadline = Clock::now() + std::chrono::milliseconds(200);
            while (!stopped(conn) && Clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (!stopped(conn)) Q_EMIT logMessage(QStringLiteral("CDL5 FPGA 停止超时，下一次采集将拒绝"));
        }
    }
    void capture(const std::shared_ptr<DmaConnection> &conn, int seconds, const Sink &sink)
    {
        require(stopped(*conn), "FPGA is not stopped; no configuration applied");
        const quint32 geometry = readRegister(*conn, Geometry);
        const quint32 gates = (geometry >> 8) & 0xffff;
        const quint32 bins = geometry & 0xff;
        const auto frameBytes = quint64(readRegister(*conn, FrameWords)) * 4;
        const auto config = Cdl5::configuration(geometry >> 28);
        require(config && gates == quint32(config->rows) && bins == quint32(config->columns)
                && ((geometry >> 24) & 0xf) == quint32(config->lanes)
                && (readRegister(*conn, Mask) & 0x1f) == ((1u << config->channels) - 1)
                && frameBytes == (quint64(gates) * bins + 1) * Cdl5::RecordBytes
                && frameBytes <= conn->capacity(), "Unsupported frame geometry or DMA capacity");
        const auto period = readRegister(*conn, Period);
        require(period != 0, "FPGA trigger period is zero");
        const auto accum = (quint64(seconds) * 250000000 + period - 1) / period;
        require(accum && accum <= 65535, "Accumulation does not fit FPGA register");
        const auto cleanup = qScopeGuard([&] { releaseCapture(*conn); });
        // Preserve the hardware's active profile; only accumulation changes per request.
        for (auto pair : {std::pair{PeriodPending, Period}, {ShiftPending, Shift},
                          {BinPending, Bin}, {MapPending, Map}, {RangePending, Range}})
            writeRegister(*conn, pair.first, readRegister(*conn, pair.second));
        writeRegister(*conn, AccumPending, static_cast<quint32>(accum));
        const auto epoch = readRegister(*conn, Epoch);
        writeRegister(*conn, Apply, 1);
        const auto configDeadline = Clock::now() + std::chrono::milliseconds(200);
        while (readRegister(*conn, Epoch) == epoch) {
            if (interrupted()) return;
            require(Clock::now() < configDeadline, "FPGA configuration apply timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(readRegister(*conn, Accum) == accum, "FPGA rejected accumulation");
        const auto jesd = readRegister(*conn, Jesd);
        require((jesd & 0x00000ff8u) == 0x00000ff8u && (jesd & 0x00003007u) == 0,
                "JESD is not ready");
        cdl5_dma_ioc request{};
        request.length = static_cast<quint32>(frameBytes);
        request.timeout_ms = 100;
        systemCheck(::ioctl(conn->dmaDescriptor(), CDL5_DMA_START, &request) == 0, "DMA START");
        writeRegister(*conn, Enable, 3); // DMA is armed before the single-shot trigger.
        const auto deadline = Clock::now() + std::chrono::seconds(seconds + 3);
        while (true) {
            if (interrupted()) writeRegister(*conn, Enable, 0);
            const int result = ::ioctl(conn->dmaDescriptor(), CDL5_DMA_WAIT_KEEP, &request);
            if (result == 0) break;
            const int error = errno;
            if (interrupted() && stopped(*conn)) return;
            require(Clock::now() < deadline, "DMA completion timed out");
            if (error == EAGAIN || error == EINTR) continue;
            throw std::system_error(error, std::generic_category(), "DMA WAIT_KEEP");
        }
        if (interrupted()) return; // Drain a cancelled epoch without delivering its data.
        require(request.status == 0 && request.transferred == frameBytes, "DMA transfer incomplete");
        systemCheck(::lseek(conn->dmaDescriptor(), 0, SEEK_SET) == 0, "DMA rewind");
        const auto at = std::chrono::system_clock::now();
        for (quint64 sent = 0; sent < frameBytes;) {
            if (interrupted()) return;
            const auto length = static_cast<qsizetype>(std::min<quint64>(Cdl5::ChunkBytes, frameBytes - sent));
            QByteArray bytes(length, Qt::Uninitialized);
            for (qsizetype done = 0; done != length;) {
                const auto count = ::read(conn->dmaDescriptor(), bytes.data() + done, static_cast<size_t>(length - done));
                if (count < 0 && errno == EINTR) continue;
                systemCheck(count >= 0, "DMA read");
                require(count != 0, "Unexpected DMA EOF");
                done += count;
            }
            sink(conn, bytes, at); // Owning QByteArray; Bridge retains its implicit-sharing storage.
            sent += static_cast<quint64>(length);
        }
    }
    bool connLoop(std::shared_ptr<DmaConnection> conn, Sink sink) override
    {
        try {
            while (true) {
                int request;
                {
                    std::unique_lock lock(gate_);
                    wake_.wait(lock, [this] { return pending_.has_value(); });
                    request = *pending_;
                    pending_.reset();
                }
                if (request < 0) return true;
                if (request == 0) { releaseCapture(*conn); continue; }
                capture(conn, request, sink);
            }
        } catch (const std::exception &error) {
            Q_EMIT logMessage(QString::fromUtf8(error.what()));
            return false;
        }
    }
    mutable std::mutex gate_;
    mutable std::condition_variable wake_;
    mutable std::optional<int> pending_; // -1 closes, 0 cancels, >0 starts; latest owner request wins.
};
