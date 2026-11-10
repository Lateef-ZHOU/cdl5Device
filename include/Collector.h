#pragma once
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <QString>
#include <QVector>
#include <DAQCollector.h>
#include "Cdl5Protocol.h"
#include "Configuration.h"

namespace Cdl5 {
// 第 channel 路各门的起点斜距：门下标 × 该路门宽。
inline QVector<double> channelRanges(int channel)
{
    QVector<double> ranges(Rows);
    for (int gate = 0; gate < Rows; ++gate)
        ranges[gate] = gate * GateSpacingM[channel];
    return ranges;
}

inline QString channelTag(int channel)
{
    return QStringLiteral("ADC0_%1m").arg(GateSpacingM[channel]);
}

// 500 MSPS、512 点 FFT，波长 1550 nm。频移向上取正，与交付上位机的径向风换算一致。
inline constexpr int SampleRateHz = 500000000;
inline constexpr int FftLength = 512;
inline constexpr double BinMHz = SampleRateHz / 1000000.0 / FftLength;
inline constexpr double MetersPerSecondPerMHz = 0.775;

// 帧头给出起始 bin；第 i 列的频率为 (firstBin + i) · BinMHz。
inline QVector<double> binFrequencies(int firstBin, int columns)
{
    QVector<double> frequencies(columns);
    for (int i = 0; i < columns; ++i)
        frequencies[i] = (firstBin + i) * BinMHz;
    return frequencies;
}
} // namespace Cdl5

// 通道规格来自型号常量；频率轴由帧头起始 bin 确定，收齐末包即按族标准方法处理。
class Cdl5Collector final : public DAQCollector
{
public:
    static constexpr qsizetype Points = Cdl5::Rows * Cdl5::Columns;
    // 族模板层按请求参数造完整实例并备齐三路收集区；makeSelf 供看门狗重试另造新斗。
    static std::shared_ptr<Cdl5Collector> create(const DAQParameters &parameters)
    {
        auto result = std::shared_ptr<Cdl5Collector>(new Cdl5Collector(parameters));
        for (auto &channel : *result) channel.spectrum.reserve(Points);
        return result;
    }
    std::shared_ptr<Cdl5Collector> makeSelf() const { return create(parameters()); }
    int profile() const noexcept { return (binConfig_ >> 12) & 15; }
    int firstBin() const noexcept { return binConfig_ & 0x1ff; }
    int timeoutMs() const override { return (parameters().accumSec + 5) * 1000; }
    QVector<QByteArray> buildStart() const override { return {Cdl5::command(static_cast<quint32>(parameters().accumSec))}; }
    QVector<QByteArray> buildStop() const override { return {Cdl5::command(0)}; }

private:
    explicit Cdl5Collector(DAQParameters parameters)
        : DAQCollector(std::move(parameters), channels()) {}

    static std::vector<DAQChannel> channels()
    {
        std::vector<DAQChannel> result(Cdl5::Channels);
        for (int i = 0; i < Cdl5::Channels; ++i) {
            auto &channel = result[i];
            channel.name = Cdl5::channelTag(i);
            channel.rows = Cdl5::Rows;
            channel.columns = Cdl5::Columns;
            channel.zeroWindMHz = Cdl5::ZeroWindReferenceMHz;
            channel.metersPerSecondPerMHz = Cdl5::MetersPerSecondPerMHz;
            channel.rangeM = Cdl5::channelRanges(i);
        }
        return result;
    }

    void onFeed(const std::optional<QByteArray> &item, RxTime at) override
    {
        const auto &bytes = *item;
        // DMA emits at most ChunkBytes; receive offload can coalesce several chunks.
        // Validate record alignment and remaining frame capacity, not the send chunk size.
        if (bytes.isEmpty() || bytes.size() % Cdl5::RecordBytes != 0)
            return;
        qsizetype offset = 0;
        if (!headerReceived_) {
            // One header per complete frame, not one per UDP datagram. No per-record meta lookup.
            if (Cdl5::word(bytes.constData()) != Cdl5::FrameMagic
                || Cdl5::word(bytes.constData() + 4) != Cdl5::FrameFormat
                || Cdl5::word(bytes.constData() + 8) != ((quint32(Cdl5::Rows) << 16) | Cdl5::Columns)
                || ((Cdl5::word(bytes.constData() + 16) >> 12) & 15) != quint32(Cdl5::Profile))
                return;
            offset = Cdl5::RecordBytes;
        }
        auto &first = front().spectrum;
        const auto count = (bytes.size() - offset) / Cdl5::RecordBytes;
        if (count > Points - first.size()) return;
        if (!headerReceived_) {
            binConfig_ = Cdl5::word(bytes.constData() + 16);
            headerReceived_ = true;
            frameTime_ = at;
            for (auto &spec : *this)
                spec.freqMHz = Cdl5::binFrequencies(firstBin(), Cdl5::Columns);
        }
        for (; offset < bytes.size(); offset += Cdl5::RecordBytes)
            for (int channel = 0; channel != Cdl5::Channels; ++channel)
                (*this)[channel].spectrum.append(Cdl5::power(bytes.constData() + offset + channel * 4));
        if (first.size() == Points)
            complete();
    }
    quint32 binConfig_ = 0;
    bool headerReceived_ = false; // Distinguishes a header-only first datagram from no frame yet.
};
static_assert(DAQCollectorLike<Cdl5Collector>);
