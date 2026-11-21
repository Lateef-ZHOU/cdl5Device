#pragma once
#include <array>
#include <vector>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <QByteArray>
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
// PL 0x26111901 的起始 bin：频率轴第 i 列 = (FirstBin + i)·BinMHz；帧头起始 bin 不符即拒收该帧。
inline constexpr int FirstBin = 15;

// 第四个硬件槽只提供远段。近段覆盖 240 × 15 m，切换处正好是 60 m 槽的下标 60。
inline constexpr int NearGateWidth = GateSpacingM[1];
inline constexpr int FarGateWidth = GateSpacingM[3];
static_assert((Rows * NearGateWidth) % FarGateWidth == 0);
inline constexpr int FirstFarGate = Rows * NearGateWidth / FarGateWidth;
inline constexpr int MixedGateCount = Rows + (Rows - FirstFarGate);
// 混合通道的 421 点边界表：由 constexpr 函数生成，映射内部静态持有；类型签名只带函数符号。
constexpr std::array<double, MixedGateCount + 1> mixedRangeEdges()
{
    std::array<double, MixedGateCount + 1> edges{};
    for (int i = 0; i < Rows; ++i)
        edges[i] = RangeStartM + i * NearGateWidth;
    for (int i = FirstFarGate; i <= Rows; ++i)
        edges[Rows + i - FirstFarGate] = RangeStartM + i * FarGateWidth;
    return edges;
}
} // namespace Cdl5

static_assert(Cdl5::GateSpacingM == std::array{6, 15, 30, 60}, "通道名须与门宽一致");
// 频率轴三路共用；距离轴按门宽区分，混合通道用 421 点表。
using Cdl5Frequency = LinearMapping<Cdl5::FirstBin * Cdl5::BinMHz, Cdl5::BinMHz, Cdl5::Columns>;
// 频率 → 风速按验收口径分段：0–200 MHz 用 (f−80)/1.29，200–250 MHz 折回 (f−330)/1.29。
// 换到下标域后曲线在 f=200 处有跳变（约 bin 189.8），不是单调表；风速值是映射不是轴，
// 用 TableMapping 交给族基，轴仍用 NonLinearMapping 保证单调。
namespace Cdl5WindDetail
{
inline constexpr double Divisor = 1.29;
inline constexpr double UpperBandOffsetMHz = 330.0;

[[nodiscard]] constexpr double velocityAtBin(const double bin) noexcept
{
    const double f = (Cdl5::FirstBin + bin) * Cdl5::BinMHz;
    return (f <= 200.0 ? f - Cdl5::ZeroWindReferenceMHz : f - UpperBandOffsetMHz) / Divisor;
}
} // namespace Cdl5WindDetail

constexpr std::array<double, Cdl5::Columns + 1> cdl5WindTable()
{
    std::array<double, Cdl5::Columns + 1> table{};
    for (int i = 0; i < Cdl5::Columns; ++i)
        table[i] = Cdl5WindDetail::velocityAtBin(static_cast<double>(i));
    table[Cdl5::Columns] = table[Cdl5::Columns - 1]; // 表尾供插值取上界；落盘只写前 count 项
    return table;
}

using Cdl5Wind = TableMapping<cdl5WindTable>;

// 镜面在 480 m：绘图遮蔽镜面之前的门，各门宽对应 480/门宽 个门。
inline constexpr int Cdl5MirrorM = 480;
template<int GateWidthM>
inline constexpr int Cdl5MirrorMask = Cdl5MirrorM / GateWidthM;

template<int GateWidthM>
using Cdl5Range = LinearMapping<Cdl5::RangeStartM, static_cast<double>(GateWidthM), Cdl5::Rows>;
using Cdl5SixRange = MaskedAxis<Cdl5Range<Cdl5::GateSpacingM[0]>, Cdl5MirrorMask<Cdl5::GateSpacingM[0]>>;
using Cdl5ThirtyRange = MaskedAxis<Cdl5Range<Cdl5::GateSpacingM[2]>, Cdl5MirrorMask<Cdl5::GateSpacingM[2]>>;
// 混合通道近段是 15 m 门，遮蔽门数按 15 m 算。
using Cdl5MixedRange = MaskedAxis<NonLinearMapping<Cdl5::mixedRangeEdges>, Cdl5MirrorMask<Cdl5::GateSpacingM[1]>>;
static_assert(Cdl5MixedRange::count == Cdl5::MixedGateCount);
using Cdl5SixFrame = Grid2D<Cdl5SixRange, Cdl5Frequency>;
using Cdl5ThirtyFrame = Grid2D<Cdl5ThirtyRange, Cdl5Frequency>;
using Cdl5MixedFrame = Grid2D<Cdl5MixedRange, Cdl5Frequency>;
using Cdl5SixChannel = DAQChannel<Cdl5SixRange, Cdl5Frequency, Cdl5Wind>;
using Cdl5ThirtyChannel = DAQChannel<Cdl5ThirtyRange, Cdl5Frequency, Cdl5Wind>;
using Cdl5MixedChannel = DAQChannel<Cdl5MixedRange, Cdl5Frequency, Cdl5Wind>;

// 线帧保持四个 FP32 槽。收齐后解出两个均匀通道和一个拼接通道。
class Cdl5Collector final
    : public TypedDAQCollector<Cdl5SixChannel, Cdl5ThirtyChannel, Cdl5MixedChannel>
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

    std::optional<Frame> assemble(std::span<const unsigned char> bytes, RxTime at) override
    {
        // DMA emits at most ChunkBytes; receive offload can coalesce several chunks.
        // Validate record alignment and remaining frame capacity, not the send chunk size.
        if (bytes.empty() || bytes.size() % Cdl5::RecordBytes != 0
            || bytes.size() > static_cast<std::size_t>(FrameBytes - received_))
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
        std::memcpy(raw_.data() + received_, bytes.data(), bytes.size());
        received_ += static_cast<qsizetype>(bytes.size());
        if (received_ != FrameBytes)
            return std::nullopt;
        auto channels = decode();
        raw_ = QByteArray();
        return channels;
    }

    // 从硬件帧直接填最终谱。混合谱近段取 15 m 槽，远段取 60 m 槽。
    Frame decode() const
    {
        const char *const data = raw_.constData();
        auto six = std::make_unique<Cdl5SixFrame>();
        auto thirty = std::make_unique<Cdl5ThirtyFrame>();
        auto fifteen = std::make_unique<Cdl5MixedFrame>();
        for (qsizetype i = 0; i < Points; ++i) {
            const char *record = data + (i + 1) * Cdl5::RecordBytes;
            (*six)[i] = Cdl5::power(record);
            (*fifteen)[i] = Cdl5::power(record + 4);
            (*thirty)[i] = Cdl5::power(record + 8);
            if (i >= Cdl5::FirstFarGate * Cdl5::Columns)
                (*fifteen)[i + (Cdl5::Rows - Cdl5::FirstFarGate) * Cdl5::Columns] = Cdl5::power(record + 12);
        }
        return {Cdl5SixChannel(std::move(six), Cdl5Wind{}, "ADC0_6m"),
                Cdl5ThirtyChannel(std::move(thirty), Cdl5Wind{}, "ADC0_30m"),
                Cdl5MixedChannel(std::move(fifteen), Cdl5Wind{}, "ADC0_15m")};
    }

    QByteArray raw_{FrameBytes, Qt::Uninitialized}; // 原样帧（含帧头记录）；解析后释放
    qsizetype received_ = 0;                        // 已收字节，凑满 FrameBytes 即齐
};
static_assert(DAQCollectorLike<Cdl5Collector>);
