#pragma once
#include <cstring>
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
inline QString channelTag(int channel)
{
    return QStringLiteral("ADC0_%1m").arg(GateSpacingM[channel]);
}

// 500 MSPS、512 点 FFT，波长 1550 nm（λ/2 = 0.775 m/s/MHz）。频移向上取正，与交付上位机的径向风换算一致。
inline constexpr int SampleRateHz = 500000000;
inline constexpr int FftLength = 512;
inline constexpr double BinMHz = SampleRateHz / 1000000.0 / FftLength;
inline constexpr double WavelengthM = 1550e-9;
// PL 0x26110401 帧头的起始 bin；只用于首帧前的初始频率轴，实际每帧以帧头为准。
inline constexpr int FirstBinDefault = 15;
} // namespace Cdl5

// 一帧 = 一条帧头记录 + Rows × Columns 条数据记录，每条 32 字节、含三路 FP32。收包只校验帧头与记录对齐并按序放置字节；
// 凑齐后解析：频率起点由帧头起始 bin 确定（第 i 列 = (firstBin + i)·BinMHz），三路交错拆成各自的功率谱。
class Cdl5Collector final : public DAQCollector
{
public:
    static constexpr qsizetype Points = Cdl5::Rows * Cdl5::Columns;
    static constexpr qsizetype FrameBytes = (Points + 1) * Cdl5::RecordBytes;
    // 族模板层按已校验的请求参数直接构造完整实例；看门狗重试以同一参数另造新斗。
    explicit Cdl5Collector(const DAQParameters &parameters)
        : DAQCollector(parameters, Cdl5::WavelengthM) {}
    int timeoutMs() const override { return (parameters.accumSec + 5) * 1000; }
    QVector<QByteArray> buildStart() const override { return {Cdl5::command(static_cast<quint32>(parameters.accumSec))}; }
    QVector<QByteArray> buildStop() const override { return {Cdl5::command(0)}; }

private:

    std::optional<std::vector<DAQChannel>> assemble(QByteArrayView bytes, RxTime at) override
    {
        // DMA emits at most ChunkBytes; receive offload can coalesce several chunks.
        // Validate record alignment and remaining frame capacity, not the send chunk size.
        if (bytes.isEmpty() || bytes.size() % Cdl5::RecordBytes != 0 || bytes.size() > FrameBytes - received_)
            return std::nullopt;
        const bool first = received_ == 0;
        // One header per complete frame, not one per UDP datagram. The header record stays in the frame.
        if (first && (Cdl5::word(bytes.data()) != Cdl5::FrameMagic
                      || Cdl5::word(bytes.data() + 4) != Cdl5::FrameFormat
                      || Cdl5::word(bytes.data() + 8) != ((quint32(Cdl5::Rows) << 16) | Cdl5::Columns)
                      || ((Cdl5::word(bytes.data() + 16) >> 12) & 15) != quint32(Cdl5::Profile)))
            return std::nullopt;
        if (first)
            frameTime_ = at;
        std::memcpy(raw_.data() + received_, bytes.data(), static_cast<std::size_t>(bytes.size()));
        received_ += bytes.size();
        if (received_ != FrameBytes)
            return std::nullopt;
        auto channels = decode();
        raw_ = QByteArray();
        return channels;
    }

    // 凑齐后解析：帧头起始 bin 定频率起点，三路交错拆成各自的功率谱。
    std::vector<DAQChannel> decode() const
    {
        const char *const data = raw_.constData();
        const double freqStartMHz = (Cdl5::word(data + 16) & 0x1ff) * Cdl5::BinMHz;
        std::vector<LinearArray2D<double>> spectra;
        for (int channel = 0; channel != Cdl5::Channels; ++channel) {
            spectra.emplace_back(Cdl5::Rows, Cdl5::Columns, Cdl5::RangeStartM, Cdl5::GateSpacingM[channel],
                                 freqStartMHz, Cdl5::BinMHz);
            spectra.back().reserve(Points);
        }
        for (qsizetype offset = Cdl5::RecordBytes; offset < raw_.size(); offset += Cdl5::RecordBytes)
            for (int channel = 0; channel != Cdl5::Channels; ++channel)
                spectra[channel].push_back(Cdl5::power(data + offset + channel * 4));
        std::vector<DAQChannel> channels;
        channels.reserve(Cdl5::Channels);
        for (int channel = 0; channel != Cdl5::Channels; ++channel)
            channels.emplace_back(Cdl5::channelTag(channel), std::move(spectra[channel]), Cdl5::ZeroWindReferenceMHz,
                                  wavelengthM());
        return channels;
    }

    QByteArray raw_{FrameBytes, Qt::Uninitialized}; // 原样帧（含帧头记录）；解析后释放
    qsizetype received_ = 0;                        // 已收字节，凑满 FrameBytes 即齐
};
static_assert(DAQCollectorLike<Cdl5Collector>);
