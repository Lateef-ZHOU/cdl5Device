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
// 500 MSPS、512 点 FFT，波长 1550 nm（λ/2 = 0.775 m/s/MHz）。频移向上取正，与交付上位机的径向风换算一致。
inline constexpr int SampleRateHz = 500000000;
inline constexpr int FftLength = 512;
inline constexpr double BinMHz = SampleRateHz / 1000000.0 / FftLength;
inline constexpr double WavelengthM = 1550e-9;
// PL 0x26110401 的起始 bin：频率轴第 i 列 = (FirstBin + i)·BinMHz；帧头起始 bin 不符即拒收该帧。
inline constexpr int FirstBin = 15;
} // namespace Cdl5

// 三路规格：门宽不同，其余共用（ADC0 同一零风速参考、波长与频率轴）。
template <int Index>
struct Cdl5ChannelSpec
{
    static_assert(Cdl5::GateSpacingM == std::array<int, Cdl5::Channels>{6, 15, 30}, "通道名须与门宽一致");
    static constexpr const char *name = Index == 0 ? "ADC0_6m" : Index == 1 ? "ADC0_15m" : "ADC0_30m";
    static constexpr double wavelengthM = Cdl5::WavelengthM, zeroWindMHz = Cdl5::ZeroWindReferenceMHz;
    static constexpr int rows = Cdl5::Rows, columns = Cdl5::Columns;
    static constexpr double rangeStartM = Cdl5::RangeStartM, rangeStepM = Cdl5::GateSpacingM[Index];
    static constexpr double frequencyStartMHz = Cdl5::FirstBin * Cdl5::BinMHz, frequencyStepMHz = Cdl5::BinMHz;
};

// 一帧 = 一条帧头记录 + Rows × Columns 条数据记录，每条 32 字节、含三路 FP32。收包只校验帧头与记录对齐并按序放置字节；
// 凑齐后解析：三路交错拆成各自的功率谱，轴与系数取各路规格。
class Cdl5Collector final : public DAQCollector
{
public:
    static constexpr qsizetype Points = Cdl5::Rows * Cdl5::Columns;
    static constexpr qsizetype FrameBytes = (Points + 1) * Cdl5::RecordBytes;
    // 族模板层按已校验的请求参数直接构造完整实例；看门狗重试以同一参数另造新斗。
    explicit Cdl5Collector(const DAQParameters &parameters)
        : DAQCollector(parameters) {}
    int timeoutMs() const override { return (parameters.accumSec + 5) * 1000; }
    QVector<QByteArray> buildStart() const override { return {Cdl5::command(static_cast<quint32>(parameters.accumSec))}; }
    QVector<QByteArray> buildStop() const override { return {Cdl5::command(0)}; }

private:

    std::optional<DAQChannels> assemble(QByteArrayView bytes, RxTime at) override
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
                      || ((Cdl5::word(bytes.data() + 16) >> 12) & 15) != quint32(Cdl5::Profile)
                      || (Cdl5::word(bytes.data() + 16) & 0x1ff) != quint32(Cdl5::FirstBin)))
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

    // 凑齐后解析：跳过帧头记录，三路交错拆成各自的数值，交各路规格构造通道。
    DAQChannels decode() const
    {
        const char *const data = raw_.constData();
        std::array<std::vector<double>, Cdl5::Channels> values;
        for (auto &channel : values)
            channel.reserve(Points);
        for (qsizetype offset = Cdl5::RecordBytes; offset < raw_.size(); offset += Cdl5::RecordBytes)
            for (int channel = 0; channel != Cdl5::Channels; ++channel)
                values[channel].push_back(Cdl5::power(data + offset + channel * 4));
        return {std::make_shared<TypedDAQChannel<Cdl5ChannelSpec<0>>>(std::move(values[0])),
                std::make_shared<TypedDAQChannel<Cdl5ChannelSpec<1>>>(std::move(values[1])),
                std::make_shared<TypedDAQChannel<Cdl5ChannelSpec<2>>>(std::move(values[2]))};
    }

    QByteArray raw_{FrameBytes, Qt::Uninitialized}; // 原样帧（含帧头记录）；解析后释放
    qsizetype received_ = 0;                        // 已收字节，凑满 FrameBytes 即齐
};
static_assert(DAQCollectorLike<Cdl5Collector>);
