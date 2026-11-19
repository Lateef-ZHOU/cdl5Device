#pragma once
#include <vector>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <QString>
#include <DAQFamily/DAQCollector.h>
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
static_assert(Cdl5::GateSpacingM == std::array{6, 15, 30}, "通道名须与门宽一致");
inline constexpr auto cdl5Params = [] {
    std::array<DAQParams, Cdl5::Channels> channels{};
    for (std::size_t i = 0; i < channels.size(); ++i) {
        channels[i] = {.wavelengthM = Cdl5::WavelengthM, .zeroWindMHz = Cdl5::ZeroWindReferenceMHz,
            .rangeStartM = Cdl5::RangeStartM, .rangeStepM = static_cast<double>(Cdl5::GateSpacingM[i]),
            .frequencyStartMHz = Cdl5::FirstBin * Cdl5::BinMHz, .frequencyStepMHz = Cdl5::BinMHz,
            .rangeGateCount = Cdl5::Rows, .frequencyBinCount = Cdl5::Columns};
    }
    return channels;
}();

// 一帧 = 一条帧头记录 + Rows × Columns 条数据记录，每条 32 字节、含三路 FP32。收包只校验帧头与记录对齐并按序放置字节；
// 凑齐后解析：三路交错拆成各自的功率谱，轴与系数取各路规格。
class Cdl5Collector final
    : public TypedDAQCollector<DAQChannel<cdl5Params[0]>, DAQChannel<cdl5Params[1]>, DAQChannel<cdl5Params[2]>>
{
public:
    static constexpr qsizetype Points = Cdl5::Rows * Cdl5::Columns;
    static constexpr qsizetype FrameBytes = (Points + 1) * Cdl5::RecordBytes;
    // 族模板层按已校验的请求参数直接构造完整实例；看门狗重试以同一参数另造新斗。
    explicit Cdl5Collector(const DAQParameters &parameters)
        : TypedDAQCollector(parameters) {}
    int timeoutMs() const override { return (parameters.accumSec + 5) * 1000; }
    std::vector<std::vector<unsigned char>> buildStart() const override
    {
        const auto command = Cdl5::command(static_cast<quint32>(parameters.accumSec));
        return {{command.cbegin(), command.cend()}};
    }
    std::vector<std::vector<unsigned char>> buildStop() const override
    {
        const auto command = Cdl5::command(0);
        return {{command.cbegin(), command.cend()}};
    }

private:

    std::optional<Frame> assemble(QByteArrayView bytes, RxTime at) override
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
    Frame decode() const
    {
        const char *const data = raw_.constData();
        auto six = std::make_unique<Spectrum<cdl5Params[0]>>();
        auto fifteen = std::make_unique<Spectrum<cdl5Params[1]>>();
        auto thirty = std::make_unique<Spectrum<cdl5Params[2]>>();
        for (qsizetype i = 0; i < Points; ++i) {
            const char *record = data + (i + 1) * Cdl5::RecordBytes;
            (*six)[i] = Cdl5::power(record);
            (*fifteen)[i] = Cdl5::power(record + 4);
            (*thirty)[i] = Cdl5::power(record + 8);
        }
        return {DAQChannel<cdl5Params[0]>(std::move(six), "ADC0_6m"),
                DAQChannel<cdl5Params[1]>(std::move(fifteen), "ADC0_15m"),
                DAQChannel<cdl5Params[2]>(std::move(thirty), "ADC0_30m")};
    }

    QByteArray raw_{FrameBytes, Qt::Uninitialized}; // 原样帧（含帧头记录）；解析后释放
    qsizetype received_ = 0;                        // 已收字节，凑满 FrameBytes 即齐
};
static_assert(DAQCollectorLike<Cdl5Collector>);
