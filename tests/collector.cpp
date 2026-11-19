#include <Device.h>
#include <DAQFamily/WindProfile.h>
#include <QFile>

#include <cmath>
#include <tuple>
#include <array>
#include <numbers>
#include <cstdlib>
#include <iostream>
#include <type_traits>
static constexpr const char *channelNames[] = {"ADC0_6m", "ADC0_15m", "ADC0_30m"};
void check(bool value, const char *reason)
{ if (!value) { std::cerr << reason << '\n'; std::exit(1); } }
// 收集器测试不经设备：自行派生通行证造参数（通行证只防误用，不防刻意派生）。
struct TestKey : DAQParamsKey { TestKey() {} };
DAQParameters testParameters(const int accumSec) { return DAQParameters(TestKey(), accumSec, std::nullopt, std::nullopt, {}, false); }
// 一门 × 128 频点的测试规格：起始 bin 15、门宽 6 m，零风速参考由模板实参给出。
template <double Zero>
inline constexpr auto PeakSpec = [] {
    auto params = cdl5Params[0];
    params.zeroWindMHz = Zero;
    params.rangeGateCount = 1;
    params.frequencyBinCount = 128;
    return params;
}();
// 测试只借用数值区做逐字节解码对照，不创建动态传输帧。
struct ChannelView {
    std::span<const double> spectrum, radialWind, cnr;
};
std::array<ChannelView, Cdl5::Channels> view(const Cdl5Collector &collector)
{
    return std::apply([](const auto &...channel) {
        return std::array<ChannelView, Cdl5::Channels>{ChannelView{
            {channel.spectrum->begin(), channel.spectrum->size()},
            {channel.radialWind->begin(), channel.radialWind->size()},
            {channel.cnr->begin(), channel.cnr->size()}}...};
    }, collector.channels());
}
void verify()
{
    using C = Cdl5Collector;
    const auto prototype = std::make_shared<C>(testParameters(2));
    check(!prototype->settled(), "no channels before a full frame");
    const auto result = std::make_shared<C>(prototype->parameters);
    const auto retry = std::make_shared<C>(result->parameters);
    const RxTime stamp = RxTime{} + std::chrono::seconds(123);
    QByteArray frame((C::Points + 1) * 32, char(-1));
    qToLittleEndian(Cdl5::FrameMagic, frame.data());
    qToLittleEndian(Cdl5::FrameFormat, frame.data() + 4);
    qToLittleEndian(quint32((Cdl5::Rows << 16) | Cdl5::Columns), frame.data() + 8);
    qToLittleEndian(quint32(25000), frame.data() + 12);
    qToLittleEndian(quint32((10000 << 16) | (Cdl5::Profile << 12) | 15), frame.data() + 16);
    for (qsizetype i = 0; i < C::Points; ++i)
        for (int c = 0; c < 3; ++c)
            qToLittleEndian(std::bit_cast<quint32>(float(i) + 0.25f * (c + 1)), frame.data() + (i + 1) * 32 + c * 4);
    check(!result->feed(QByteArray(31, 0), stamp), "unaligned packet ignored");
    auto wrong = frame.first(1024);
    qToLittleEndian(quint32(0), wrong.data() + 8);
    check(!result->feed(wrong, stamp), "wrong dimensions rejected");
    wrong = frame.first(1024);
    qToLittleEndian(quint32(0x05040707), wrong.data() + 4);
    check(!result->feed(wrong, stamp), "wrong lane format rejected");
    wrong = frame.first(1024);
    qToLittleEndian(quint32(0), wrong.data() + 16);
    check(!result->feed(wrong, stamp), "wrong profile rejected");
    int packets = 0;
    for (qsizetype offset = 0; offset < frame.size(); offset += 1024) {
        const auto packet = frame.mid(offset, 1024);
        const bool last = offset + packet.size() == frame.size();
        check(result->feed(packet, stamp) == last, "complete only at exact full frame size");
        ++packets;
    }
    check(packets == 1801, "1024-byte chunks and 32-byte tail");
    for (qsizetype i = 0; i < C::Points; ++i)
        for (int c = 0; c < 3; ++c)
            check(view(*result)[c].spectrum[i] == double(i) + 0.25 * (c + 1), "FP32 LE decoded and widened in arrival order, meta ignored");
    check(result->frameTime() == stamp && retry->frameTime() == RxTime{}, "first packet stamp and fresh retry");
    check(std::tuple_size_v<C::Frame> == Cdl5::Channels, "three ADC0 channels decoded");
    // 三份静态规格与原帧头匹配，CNR 和风速只能沿用本通道规格。
    int index = 0;
    std::apply([&](const auto &...channel) {
        const auto verifyChannel = [&](const auto &c) {
            constexpr auto params = std::remove_cvref_t<decltype(*c.spectrum)>::params;
            check(QString::fromUtf8(c.name) == QString::fromUtf8(channelNames[index])
                && params.rangeGateCount == Cdl5::Rows && params.frequencyBinCount == Cdl5::Columns
                && params.frequencyStartMHz == 15 * Cdl5::BinMHz && params.frequencyStepMHz == Cdl5::BinMHz
                && params.rangeStartM == 0 && params.rangeStepM == Cdl5::GateSpacingM[index]
                && params.zeroWindMHz == Cdl5::ZeroWindReferenceMHz, "static channel specification");
            check(c.cnr->size() == Cdl5::Rows, "processing ran on completion");
            ++index;
        };
        (verifyChannel(channel), ...);
    }, result->channels());
    check(!retry->settled() && !prototype->settled(), "independent receive areas");
    check(result->feed(QByteArray(1024, 0), stamp) && view(*result)[0].spectrum[0] == .25, "settled cannot be rewritten");
    auto missing = std::make_shared<C>(prototype->parameters);
    for (qsizetype offset = 0; offset < frame.size() - 32; offset += 1024)
        missing->feed(frame.mid(offset, 1024), stamp);
    check(!missing->settled(), "lost tail remains incomplete for watchdog retry");
    // 起始 bin 是通道规格常量：帧头不符即拒收该帧，后续数据包也凑不成帧。
    QByteArray wrongBin = frame;
    qToLittleEndian(quint32((10000 << 16) | (Cdl5::Profile << 12) | (Cdl5::FirstBin + 1)), wrongBin.data() + 16);
    auto mismatched = std::make_shared<C>(prototype->parameters);
    for (qsizetype offset = 0; offset < wrongBin.size(); offset += 1024)
        mismatched->feed(wrongBin.mid(offset, 1024), stamp);
    check(!mismatched->settled(), "frame with a different first bin accepted");
    auto splitHeader = std::make_shared<C>(prototype->parameters);
    check(!splitHeader->feed(frame.first(32), stamp), "header-only packet accepted");
    for (qsizetype offset = 32; offset < frame.size(); offset += 1024)
        splitHeader->feed(frame.mid(offset, 1024), stamp);
    check(splitHeader->settled() && std::ranges::equal(view(*splitHeader)[0].spectrum, view(*result)[0].spectrum),
          "header-only packet not confused with no header");

    // Npcap can receive seven original 1024-byte chunks as one 7168-byte batch.
    auto coalesced = std::make_shared<C>(prototype->parameters);
    check(!coalesced->feed(frame.first(7169), stamp) && !coalesced->settled(),
          "coalesced but unaligned input rejected without consuming header");
    for (qsizetype offset = 0; offset < frame.size() - 32; offset += 7168) {
        const auto batch = frame.mid(offset, std::min<qsizetype>(7168, frame.size() - 32 - offset));
        check(!coalesced->feed(batch, stamp), "coalesced batches still require final record");
    }
    check(!coalesced->feed(QByteArray(64, 0), stamp), "oversized tail rejected without partial append");
    check(coalesced->feed(frame.last(32), stamp), "coalesced frame completes on actual tail");
    check(std::ranges::equal(view(*coalesced), view(*result), [](const auto &l, const auto &r) { return std::ranges::equal(l.spectrum, r.spectrum); })
          && coalesced->frameTime() == stamp,
          "coalescing preserves every ADC value and first arrival time");
}
int main(int argc, char **argv)
{
    static_assert(std::is_constructible_v<Cdl5Collector, const DAQParameters &>);
    const auto unaligned = QByteArray::fromHex("780000a03f");
    check(Cdl5::power(unaligned.constData() + 1) == 1.25, "unaligned little-endian float bit interpretation");
    check(Cdl5::command(2).toHex() == "43444c3502000000", "command wire bytes");
    check(Cdl5::decodeCommand(Cdl5::command(Cdl5::MaxAccumSeconds)) && !Cdl5::decodeCommand(Cdl5::command(Cdl5::MaxAccumSeconds + 1)),
          "duration beyond the 16-bit accumulation register rejected");
    verify();
    static_assert(Cdl5::ZeroWindReferenceMHz == 80.0, "zero-wind reference is a fixed model constant");
    constexpr std::array steps{6, 15, 30};
    const auto verifyProfile = [&]<int channel>() {
        constexpr auto params = cdl5Params[channel];
        std::array<WindProfile::Input<params>, 4> grid;
        for (int i = 0; i < 4; ++i) {
            auto speeds = std::make_shared<RadialWind<params>>();
            std::fill(speeds->begin(), speeds->end(), 1.0);
            grid[i] = {WorldModel(i * 90.0, 30.0), std::move(speeds)};
        }
        const auto profile = WindProfile::Calc<params>(grid, RxTime{});
        check(profile && profile->size() == 240, "separate wind profile for each grid");
        for (int gate = 0; gate < 240; ++gate)
            check(std::abs((profile->heightStartM() + gate * profile->heightStepM()) - gate * steps[channel] / 2.0) < 1e-9,
                  "gate origins and world elevation projected height");
    };
    verifyProfile.operator()<0>();
    verifyProfile.operator()<1>();
    verifyProfile.operator()<2>();
    const auto peak = []<double Zero>() {
        auto values = std::make_unique<Spectrum<PeakSpec<Zero>>>();
        for (int i = 0; i < 128; ++i) (*values)[i] = 2 + 30 * std::exp(-0.5 * std::pow((i - 68) / 3.0, 2));
        return DAQChannel<PeakSpec<Zero>>(std::move(values));
    };
    const auto wind = peak.operator()<Cdl5::ZeroWindReferenceMHz>();
    const auto outside = peak.operator()<1.0>();
    check(std::abs((*wind.radialWind)[0] - (83 * 500.0 / 512 - 80) * 0.775) < 0.02
          && std::abs((*outside.radialWind)[0] - (83 * 500.0 / 512 - 1) * 0.775) < 0.02 && (*wind.cnr)[0] == (*outside.cnr)[0]
          && std::abs((*wind.cnr)[0] - 10 * std::log10(std::sqrt(2 * std::numbers::pi) * 90 / 256)) < 0.02,
          "known peak, real bin offset, out-of-band reference still converts wind");
    std::cout << "PASS ADC0 6/15/30 m geometry, FP32, retry, chunks and coalesced batches\n";
    // 参数为实采帧文件路径：逐个回放并检查三路都能出 CNR。
    for (int i = 1; i < argc; ++i) {
        QFile file(QString::fromLocal8Bit(argv[i]));
        check(file.open(QIODevice::ReadOnly), "open captured frame");
        const auto collector = std::make_shared<Cdl5Collector>(testParameters(1));
        while (!file.atEnd()) collector->feed(file.read(7168), {});
        check(collector->settled() && cdl5Params[0].frequencyStartMHz == 15 * Cdl5::BinMHz,
              "complete captured spectrum and ADC0 header metadata");
        for (int channel = 0; channel < Cdl5::Channels; ++channel) {
            const auto &measured = view(*collector)[channel];
            check(measured.cnr.size() == Cdl5::Rows
                  && std::ranges::any_of(measured.cnr, [](double v) { return std::isfinite(v); }),
                  "real spectrum produces CNR on completion");
            std::cout << "Replay " << channelNames[channel]
                      << ": " << Cdl5::Rows << " gates, first bin " << cdl5Params[0].frequencyStartMHz / Cdl5::BinMHz << ", range "
                      << Cdl5::RangeStartM << ".." << (Cdl5::RangeStartM + (Cdl5::Rows - 1) * Cdl5::GateSpacingM[channel]) << " m\n";
        }
    }
}
