#pragma once
#include <array>
#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>
#include <QVector>
#include <DAQCollector.h>
#include "Cdl5Protocol.h"
#include "Configuration.h"

// Three FPGA specifications share the same record decoding and retry lifecycle.
template<Cdl5::Configuration Config>
class Cdl5Collector final : public DAQCollector
{
public:
    static constexpr int Rows = Config.rows;
    static constexpr int Columns = Config.columns;
    static constexpr qsizetype Points = Rows * Columns;
    static constexpr auto Specification = Config;
    using Frame = std::array<QVector<double>, Config.channels>;
    static std::shared_ptr<const Cdl5Collector> createPrototype(int seconds, DAQContext context)
    {
        return std::shared_ptr<const Cdl5Collector>(new Cdl5Collector(seconds, std::move(context)));
    }
    std::shared_ptr<Cdl5Collector> makeSelf() const
    {
        auto result = std::shared_ptr<Cdl5Collector>(new Cdl5Collector(seconds_, context()));
        for (auto &channel : result->frame_) channel.reserve(Points);
        return result;
    }
    const Frame &constFrame() const noexcept { return frame_; }
    int profile() const noexcept { return (binConfig_ >> 12) & 15; }
    int firstBin() const noexcept { return binConfig_ & 0x1ff; }
    bool uniformRange() const noexcept { return uniformRange_; }
    bool settled() const override { return frame_[0].size() == Points; }
    int timeoutMs() const override { return (seconds_ + 5) * 1000; }
    QVector<QByteArray> buildStart() const override { return {Cdl5::command(static_cast<quint32>(seconds_))}; }
    QVector<QByteArray> buildStop() const override { return {Cdl5::command(0)}; }

private:
    explicit Cdl5Collector(int seconds, DAQContext context)
        : DAQCollector(std::move(context)), seconds_(std::clamp(seconds, 1, Config.maxAccumSeconds())) {}
    void onFeed(const QByteArray &bytes, RxTime at) override
    {
        // DMA emits at most ChunkBytes; receive offload can coalesce several chunks.
        // Validate record alignment and remaining frame capacity, not the send chunk size.
        if (bytes.isEmpty() || bytes.size() % Cdl5::RecordBytes != 0)
            return;
        qsizetype offset = 0;
        if (!headerReceived_) {
            // One header per complete frame, not one per UDP datagram. No per-record meta lookup.
            if (Cdl5::word(bytes.constData()) != Cdl5::FrameMagic
                || Cdl5::word(bytes.constData() + 4) != Config.frameFormat()
                || Cdl5::word(bytes.constData() + 8) != ((quint32(Rows) << 16) | Columns)
                || ((Cdl5::word(bytes.constData() + 16) >> 12) & 15) != quint32(Config.profile))
                return;
            offset = Cdl5::RecordBytes;
        }
        const auto count = (bytes.size() - offset) / Cdl5::RecordBytes;
        if (count > Points - frame_[0].size()) return;
        if (!headerReceived_) {
            binConfig_ = Cdl5::word(bytes.constData() + 16);
            uniformRange_ = (Cdl5::word(bytes.constData() + 28) >> 31) != 0;
            headerReceived_ = true;
            frameTime_ = at;
        }
        for (; offset < bytes.size(); offset += Cdl5::RecordBytes)
            for (int channel = 0; channel != Config.channels; ++channel)
                frame_[channel].append(Cdl5::power(bytes.constData() + offset + channel * 4));
    }
    const int seconds_;
    quint32 binConfig_ = 0;
    bool uniformRange_ = false;
    bool headerReceived_ = false; // Distinguishes a header-only first datagram from no frame yet.
    Frame frame_;
};
using Cdl5FourKCollector = Cdl5Collector<Cdl5::FourK>;
using Cdl5FiveKCollector = Cdl5Collector<Cdl5::FiveK>;
using Cdl5TenKCollector = Cdl5Collector<Cdl5::TenK>;
static_assert(DAQCollectorLike<Cdl5FourKCollector> && DAQCollectorLike<Cdl5FiveKCollector> && DAQCollectorLike<Cdl5TenKCollector>);
