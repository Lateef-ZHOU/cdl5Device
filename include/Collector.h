#pragma once
#include <array>
#include <algorithm>
#include <chrono>
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
// 三个 Profile 均以每门起点为斜距；P7/P8 的 range_mode=1 使用统一 60 m。
inline QVector<double> gateRanges(int profile, bool uniform, int rows)
{
    const auto config = configuration(profile);
    if (!config || rows != config->rows || (uniform && !config->supportsUniform)) return {};
    QVector<double> ranges(rows);
    for (int i = 0; i < rows; ++i)
        ranges[i] = uniform ? i * 60 : i < 200 ? i * 15
                  : i < 350 ? 3000 + (i - 200) * 60 : 12000 + (i - 350) * 150;
    return ranges;
}

inline QVector<double> channelRanges(Configuration config, int channel, bool uniform)
{
    if (!config.adc0Multires()) return gateRanges(config.profile, uniform, config.rows);
    QVector<double> ranges(config.rows);
    const int step = config.rangeChannels[channel].spacingM;
    for (int gate = 0; gate < config.rows; ++gate)
        ranges[gate] = gate * step;
    return ranges;
}

inline QString channelTag(Configuration config, int channel)
{
    if (config.adc0Multires())
        return QStringLiteral("ADC0_%1m").arg(config.rangeChannels[channel].spacingM);
    return QStringLiteral("ADC%1").arg(channel);
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

// Three FPGA specifications share the same record decoding and retry lifecycle.
// 通道规格来自型号配置；频率轴与（非多门宽规格的）距离轴由帧头确定，收齐末包即按族标准方法处理。
template<Cdl5::Configuration Config>
class Cdl5Collector final : public DAQCollector
{
public:
    static constexpr int Rows = Config.rows;
    static constexpr int Columns = Config.columns;
    static constexpr qsizetype Points = Rows * Columns;
    static constexpr auto Specification = Config;
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
    bool uniformRange() const noexcept { return uniformRange_; }
    int timeoutMs() const override { return (parameters().accumSec + 5) * 1000; }
    QVector<QByteArray> buildStart() const override { return {Cdl5::command(static_cast<quint32>(parameters().accumSec))}; }
    QVector<QByteArray> buildStop() const override { return {Cdl5::command(0)}; }

private:
    explicit Cdl5Collector(DAQParameters parameters)
        : DAQCollector(std::move(parameters), channels()) {}

    static std::vector<DAQChannel> channels()
    {
        std::vector<DAQChannel> result(Config.channels);
        for (int i = 0; i < Config.channels; ++i) {
            auto &channel = result[i];
            channel.name = Cdl5::channelTag(Config, i);
            channel.rows = Rows;
            channel.columns = Columns;
            channel.zeroWindMHz = Cdl5::ZeroWindReferenceMHz;
            channel.metersPerSecondPerMHz = Cdl5::MetersPerSecondPerMHz;
            if constexpr (Config.adc0Multires())
                channel.rangeM = Cdl5::channelRanges(Config, i, false);
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
                || Cdl5::word(bytes.constData() + 4) != Config.frameFormat()
                || Cdl5::word(bytes.constData() + 8) != ((quint32(Rows) << 16) | Columns)
                || ((Cdl5::word(bytes.constData() + 16) >> 12) & 15) != quint32(Config.profile))
                return;
            offset = Cdl5::RecordBytes;
        }
        auto &first = front().spectrum;
        const auto count = (bytes.size() - offset) / Cdl5::RecordBytes;
        if (count > Points - first.size()) return;
        if (!headerReceived_) {
            binConfig_ = Cdl5::word(bytes.constData() + 16);
            uniformRange_ = (Cdl5::word(bytes.constData() + 28) >> 31) != 0;
            headerReceived_ = true;
            frameTime_ = at;
            for (int channel = 0; channel != Config.channels; ++channel) {
                auto &spec = (*this)[channel];
                spec.freqMHz = Cdl5::binFrequencies(firstBin(), Columns);
                if constexpr (!Config.adc0Multires())
                    spec.rangeM = Cdl5::channelRanges(Config, channel, uniformRange_);
            }
        }
        for (; offset < bytes.size(); offset += Cdl5::RecordBytes)
            for (int channel = 0; channel != Config.channels; ++channel)
                (*this)[channel].spectrum.append(Cdl5::power(bytes.constData() + offset + channel * 4));
        if (first.size() == Points)
            complete();
    }
    quint32 binConfig_ = 0;
    bool uniformRange_ = false;
    bool headerReceived_ = false; // Distinguishes a header-only first datagram from no frame yet.
};
using Cdl5FourKCollector = Cdl5Collector<Cdl5::FourK>;
using Cdl5FiveKCollector = Cdl5Collector<Cdl5::FiveK>;
using Cdl5TenKCollector = Cdl5Collector<Cdl5::TenKAdc0>;
static_assert(DAQCollectorLike<Cdl5FourKCollector> && DAQCollectorLike<Cdl5FiveKCollector> && DAQCollectorLike<Cdl5TenKCollector>);
