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
static constexpr const char *channelNames[] = {"ADC0_6m", "ADC0_30m", "ADC0_15m"};
static std::span<const unsigned char> rxBytes(const QByteArray &bytes)
{
    return {reinterpret_cast<const unsigned char *>(bytes.constData()), static_cast<std::size_t>(bytes.size())};
}
void check(bool value, const char *reason)
{ if (!value) { std::cerr << reason << '\n'; std::exit(1); } }
// 收集器测试不经设备：自行派生通行证造参数（通行证只防误用，不防刻意派生）。
struct TestKey : DAQParamsKey { TestKey() {} };
DAQParameters testParameters(const int accumSec) { return DAQParameters(TestKey(), accumSec, std::nullopt, std::nullopt, {}, false); }
// 一门 × 128 频点的测试规格：起始 bin 15、门宽 6 m，零风速参考由模板实参给出。
using PeakRange = LinearMapping<Cdl5::RangeStartM, static_cast<double>(Cdl5::GateSpacingM[0]), 1>;
using PeakFrequency = LinearMapping<Cdl5::FirstBin * Cdl5::BinMHz, Cdl5::BinMHz, 128>;
template <double Zero>
using PeakWind = LinearMapping<(Cdl5::FirstBin * Cdl5::BinMHz - Zero) / 1.29,
    Cdl5::BinMHz / 1.29, 128>;
using PeakSpectrum = Grid2D<PeakRange, PeakFrequency>;
template <double Zero>
using PeakChannel = DAQChannel<PeakRange, PeakFrequency, PeakWind<Zero>>;
// 测试只借用数值区做逐字节解码对照，不创建动态传输帧。
struct ChannelView {
    std::span<const double> spectrum, radialWind, cnr;
};
std::array<ChannelView, std::tuple_size_v<Cdl5Collector::Frame>> view(const Cdl5Collector &collector)
{
    return std::apply([](const auto &...channel) {
        return std::array<ChannelView, std::tuple_size_v<Cdl5Collector::Frame>>{ChannelView{
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
    static_assert(Cdl5::FrameFormat == 0x0504060f && C::FrameBytes == 1843232);
    qToLittleEndian(quint32(0x0504060f), frame.data() + 4);
    qToLittleEndian(quint32((Cdl5::Rows << 16) | Cdl5::Columns), frame.data() + 8);
    qToLittleEndian(quint32(25000), frame.data() + 12);
    qToLittleEndian(quint32((10000 << 16) | (Cdl5::Profile << 12) | 15), frame.data() + 16);
    for (qsizetype i = 0; i < C::Points; ++i)
        for (int c = 0; c < Cdl5::Channels; ++c)
            qToLittleEndian(std::bit_cast<quint32>(float(i) + 0.25f * (c + 1)), frame.data() + (i + 1) * 32 + c * 4);
    check(!result->feed(rxBytes(QByteArray(31, 0)), stamp), "unaligned packet ignored");
    auto wrong = frame.first(1024);
    qToLittleEndian(quint32(0), wrong.data() + 8);
    check(!result->feed(rxBytes(wrong), stamp), "wrong dimensions rejected");
    wrong = frame.first(1024);
    qToLittleEndian(quint32(0x0504070f), wrong.data() + 4);
    check(!result->feed(rxBytes(wrong), stamp), "wrong lane format rejected");
    wrong = frame.first(1024);
    qToLittleEndian(quint32(0x05040607), wrong.data() + 4);
    check(!result->feed(rxBytes(wrong), stamp), "old three-slot mask rejected");
    wrong = frame.first(1024);
    qToLittleEndian(quint32(0), wrong.data() + 16);
    check(!result->feed(rxBytes(wrong), stamp), "wrong profile rejected");
    int packets = 0;
    for (qsizetype offset = 0; offset < frame.size(); offset += 1024) {
        const auto packet = frame.mid(offset, 1024);
        const bool last = offset + packet.size() == frame.size();
        check(result->feed(rxBytes(packet), stamp) == last, "complete only at exact full frame size");
        ++packets;
    }
    check(packets == 1801, "1024-byte chunks and 32-byte tail");
    const auto decoded = view(*result);
    for (qsizetype i = 0; i < C::Points; ++i) {
        check(decoded[0].spectrum[i] == double(i) + 0.25 && decoded[1].spectrum[i] == double(i) + 0.75,
              "uniform slots decoded in arrival order");
        check(decoded[2].spectrum[i] == double(i) + 0.50, "all 240 near gates come from 15 m");
    }
    for (int gate = 240; gate < 420; ++gate)
        for (int bin = 0; bin < 240; ++bin)
            check(decoded[2].spectrum[gate * 240 + bin] == double((gate - 180) * 240 + bin) + 1,
                  "far gates start at slot 3 gate 60");
    static_assert(Cdl5MixedRange::at(239) == 3585 && Cdl5MixedRange::at(240) == 3600
        && Cdl5MixedRange::at(241) == 3660 && Cdl5MixedRange::at(419) == 14340
        && Cdl5MixedRange::stop == 14400);
    check(result->frameTime() == stamp && retry->frameTime() == RxTime{}, "first packet stamp and fresh retry");
    static_assert(std::tuple_size_v<C::Frame> == 3);
    // 各路风速、CNR 和原始功率谱使用同一份静态通道规格。
    int index = 0;
    std::apply([&](const auto &...channel) {
        const auto verifyChannel = [&](const auto &c) {
            using SpectrumT = std::remove_cvref_t<decltype(*c.spectrum)>;
            using WindT = std::remove_cvref_t<decltype(*c.radialWind)>;
            using CnrT = std::remove_cvref_t<decltype(*c.cnr)>;
            using RangeT = typename SpectrumT::RowAxisType;
            using FrequencyT = typename SpectrumT::ColumnAxisType;
            static_assert(std::same_as<RangeT, typename WindT::AxisType>
                && std::same_as<RangeT, typename CnrT::AxisType>);
            check(QString::fromUtf8(c.name) == QString::fromUtf8(channelNames[index])
                && RangeT::count == (index == 2 ? 420 : 240) && FrequencyT::count == Cdl5::Columns
                && FrequencyT::start == 15 * Cdl5::BinMHz && FrequencyT::step == Cdl5::BinMHz
                && RangeT::at(0) == 0 && RangeT::uniform == (index < 2), "static channel specification");
            check(c.cnr->size() == c.radialWind->size()
                  && c.cnr->size() == static_cast<std::size_t>(RangeT::count),
                  "processing ran for every published gate");
            ++index;
        };
        (verifyChannel(channel), ...);
    }, result->channels());
    check(!retry->settled() && !prototype->settled(), "independent receive areas");
    check(result->feed(rxBytes(QByteArray(1024, 0)), stamp) && view(*result)[0].spectrum[0] == .25, "settled cannot be rewritten");
    auto missing = std::make_shared<C>(prototype->parameters);
    for (qsizetype offset = 0; offset < frame.size() - 32; offset += 1024)
        missing->feed(rxBytes(frame.mid(offset, 1024)), stamp);
    check(!missing->settled(), "lost tail remains incomplete for watchdog retry");
    // 起始 bin 是通道规格常量：帧头不符即拒收该帧，后续数据包也凑不成帧。
    QByteArray wrongBin = frame;
    qToLittleEndian(quint32((10000 << 16) | (Cdl5::Profile << 12) | (Cdl5::FirstBin + 1)), wrongBin.data() + 16);
    auto mismatched = std::make_shared<C>(prototype->parameters);
    for (qsizetype offset = 0; offset < wrongBin.size(); offset += 1024)
        mismatched->feed(rxBytes(wrongBin.mid(offset, 1024)), stamp);
    check(!mismatched->settled(), "frame with a different first bin accepted");
    auto splitHeader = std::make_shared<C>(prototype->parameters);
    check(!splitHeader->feed(rxBytes(frame.first(32)), stamp), "header-only packet accepted");
    for (qsizetype offset = 32; offset < frame.size(); offset += 1024)
        splitHeader->feed(rxBytes(frame.mid(offset, 1024)), stamp);
    check(splitHeader->settled() && std::ranges::equal(view(*splitHeader)[0].spectrum, view(*result)[0].spectrum),
          "header-only packet not confused with no header");

    // Npcap can receive seven original 1024-byte chunks as one 7168-byte batch.
    auto coalesced = std::make_shared<C>(prototype->parameters);
    check(!coalesced->feed(rxBytes(frame.first(7169)), stamp) && !coalesced->settled(),
          "coalesced but unaligned input rejected without consuming header");
    for (qsizetype offset = 0; offset < frame.size() - 32; offset += 7168) {
        const auto batch = frame.mid(offset, std::min<qsizetype>(7168, frame.size() - 32 - offset));
        check(!coalesced->feed(rxBytes(batch), stamp), "coalesced batches still require final record");
    }
    check(!coalesced->feed(rxBytes(QByteArray(64, 0)), stamp), "oversized tail rejected without partial append");
    check(coalesced->feed(rxBytes(frame.last(32)), stamp), "coalesced frame completes on actual tail");
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
    const auto verifyProfile = []<class Range>() {
        std::array<WindProfile::Input<Range>, 4> grid;
        for (int i = 0; i < 4; ++i) {
            auto speeds = std::make_shared<Array1D<Range>>();
            std::fill(speeds->begin(), speeds->end(), 1.0);
            grid[i] = {WorldModel(i * 90.0, 30.0), std::move(speeds)};
        }
        const auto profile = WindProfile::Calc<Range>(grid, RxTime{});
        check(profile && profile->size() == static_cast<std::size_t>(Range::count),
              "wind profile has one vector per gate");
        for (int gate = 0; gate < Range::count; ++gate)
            check(std::abs(profile->position(gate) - Range::at(gate) / 2.0) < 1e-9
                    && (*profile)[gate] && std::abs((*profile)[gate]->z() - 2.0) < 1e-9,
                  "every slant range is projected to height and inverted");
    };
    verifyProfile.operator()<Cdl5SixRange>();
    verifyProfile.operator()<Cdl5ThirtyRange>();
    verifyProfile.operator()<Cdl5MixedRange>();
    WindProfileFrame<Cdl5MixedRange> profile({}, 0.5);
    profile[239] = LinAlg::Vec3{2, 0, 0};
    profile[240] = LinAlg::Vec3{4, 0, 0};
    const auto seam = WindProfile::windAt(profile, (profile.position(239) + profile.position(240)) / 2.0);
    const auto exact = WindProfile::windAt(profile, profile.position(240));
    check(seam && std::abs(seam->x() - 3.0) < 1e-9
            && exact && exact->x() == 4.0
            && !WindProfile::windAt(profile, (profile.position(240) + profile.position(241)) / 2.0),
          "interpolation uses the two seam coordinates and preserves missing neighbors");
    const auto peak = []<double Zero>() {
        auto values = std::make_unique<PeakSpectrum>();
        for (int i = 0; i < 128; ++i) (*values)[i] = 2 + 30 * std::exp(-0.5 * std::pow((i - 68) / 3.0, 2));
        return PeakChannel<Zero>(std::move(values), PeakWind<Zero>{});
    };
    const auto wind = peak.operator()<Cdl5::ZeroWindReferenceMHz>();
    const auto outside = peak.operator()<1.0>();
    check(std::abs((*wind.radialWind)[0] - (83 * 500.0 / 512 - 80) / 1.29) < 0.02
          && std::abs((*outside.radialWind)[0] - (83 * 500.0 / 512 - 1) / 1.29) < 0.02 && (*wind.cnr)[0] == (*outside.cnr)[0]
          && std::abs((*wind.cnr)[0] - 10 * std::log10(std::sqrt(2 * std::numbers::pi) * 90 / 256)) < 0.02,
          "known peak, real bin offset, out-of-band reference still converts wind");
    // 验收口径：信号源 28/80/132/184/226 MHz → −40.31/0/40.31/80.62/−80.62 m/s。
    const auto binOf = [](const double f) { return f / Cdl5::BinMHz - Cdl5::FirstBin; };
    check(std::abs(RadialWindCalculation::windAtBin<Cdl5Wind>(binOf(28.0)) + 40.31) < 0.01
          && std::abs(RadialWindCalculation::windAtBin<Cdl5Wind>(binOf(80.0))) < 1e-9
          && std::abs(RadialWindCalculation::windAtBin<Cdl5Wind>(binOf(132.0)) - 40.31) < 0.01
          && std::abs(RadialWindCalculation::windAtBin<Cdl5Wind>(binOf(184.0)) - 80.62) < 0.01
          && std::abs(RadialWindCalculation::windAtBin<Cdl5Wind>(binOf(226.0)) + 80.62) < 0.01,
          "acceptance frequency-to-wind points drifted");
    std::cout << "PASS ADC0 6 m, 30 m and mixed 15/60 m geometry, FP32, retry, chunks and coalesced batches\n";
    // 参数为整帧文件路径：逐个回放并检查三路 CNR。
    for (int i = 1; i < argc; ++i) {
        QFile file(QString::fromLocal8Bit(argv[i]));
        check(file.open(QIODevice::ReadOnly), "open captured frame");
        const auto collector = std::make_shared<Cdl5Collector>(testParameters(1));
        while (!file.atEnd()) collector->feed(rxBytes(file.read(7168)), {});
        check(collector->settled() && Cdl5Frequency::start == 15 * Cdl5::BinMHz,
              "complete captured spectrum and ADC0 header metadata");
        const auto replayed = view(*collector);
        for (std::size_t channel = 0; channel < replayed.size(); ++channel) {
            const auto &measured = replayed[channel];
            check(measured.cnr.size() == (channel == 2 ? 420 : 240)
                  && std::ranges::any_of(measured.cnr, [](double v) { return std::isfinite(v); }),
                  "replayed spectrum produces CNR on completion");
            std::cout << "Replay " << channelNames[channel]
                      << ": " << measured.cnr.size() << " gates, first bin " << Cdl5::FirstBin << '\n';
        }
    }
}
