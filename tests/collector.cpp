#include <Device.h>
#include <WindProfile.h>
#include <QFile>

#include <cmath>
#include <numbers>
#include <cstdlib>
#include <iostream>
#include <type_traits>
void check(bool value, const char *reason)
{ if (!value) { std::cerr << reason << '\n'; std::exit(1); } }
// 收集器测试不经设备：自行派生通行证造参数（通行证只防误用，不防刻意派生）。
struct TestKey : DAQParamsKey { TestKey() {} };
DAQParameters testParameters(const int accumSec) { return DAQParameters(TestKey(), accumSec, std::nullopt, std::nullopt, {}, false); }
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
            check(result->channels()[c].spectrum[i] == double(i) + 0.25 * (c + 1), "FP32 LE decoded and widened in arrival order, meta ignored");
    check(result->frameTime() == stamp && retry->frameTime() == RxTime{}, "first packet stamp and fresh retry");
    check(result->channels().size() == Cdl5::Channels, "three ADC0 channels decoded");
    // 通道规格：频率轴取帧头起始 bin，距离轴按各路门宽固定。
    for (int c = 0; c < 3; ++c) {
        const auto &channel = result->channels()[c];
        check(channel.name == Cdl5::channelTag(c) && channel.spectrum.rows == Cdl5::Rows && channel.spectrum.columns == Cdl5::Columns
              && channel.spectrum.columnStart == 15 * Cdl5::BinMHz && channel.spectrum.columnStep == Cdl5::BinMHz
              && channel.spectrum.rowStart == 0 && channel.spectrum.rowStep == Cdl5::GateSpacingM[c]
              && channel.zeroWindMHz == Cdl5::ZeroWindReferenceMHz && channel.radialWind.start == 0
              && channel.radialWind.step == Cdl5::GateSpacingM[c] && channel.cnr.sameAxis(channel.radialWind),
              "channel specification from configuration and frame header");
        check(channel.cnr.size() == Cdl5::Rows, "standard processing ran on completion");
    }
    check(!retry->settled() && !prototype->settled(), "independent receive areas");
    check(result->feed(QByteArray(1024, 0), stamp) && result->channels()[0].spectrum[0] == .25, "settled cannot be rewritten");
    auto missing = std::make_shared<C>(prototype->parameters);
    for (qsizetype offset = 0; offset < frame.size() - 32; offset += 1024)
        missing->feed(frame.mid(offset, 1024), stamp);
    check(!missing->settled(), "lost tail remains incomplete for watchdog retry");
    auto splitHeader = std::make_shared<C>(prototype->parameters);
    check(!splitHeader->feed(frame.first(32), stamp), "header-only packet accepted");
    for (qsizetype offset = 32; offset < frame.size(); offset += 1024)
        splitHeader->feed(frame.mid(offset, 1024), stamp);
    check(splitHeader->settled() && splitHeader->channels()[0].spectrum == result->channels()[0].spectrum,
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
    check(std::ranges::equal(coalesced->channels(), result->channels(), [](const auto &l, const auto &r) { return l.spectrum == r.spectrum; })
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
    const std::vector<double> radialSpeeds(240, 1.0);
    const std::array<WindProfile::Input, 4> beams{{
        {WorldModel(0, 30, 0), {}}, {WorldModel(90, 30, 0), {}},
        {WorldModel(180, 30, 0), {}}, {WorldModel(270, 30, 0), {}}
    }};
    constexpr std::array steps{6, 15, 30};
    for (int channel = 0; channel < 3; ++channel) {
        std::array<WindProfile::Input, 4> grid = beams;
        for (auto &beam : grid) beam.ws = LinearArray<double>(Cdl5::RangeStartM, Cdl5::GateSpacingM[channel], radialSpeeds);
        const auto profile = WindProfile::Calc(grid, RxTime{});
        check(profile && profile->winds.size() == 240, "separate wind profile for each grid");
        for (int gate = 0; gate < 240; ++gate)
            check(std::abs(profile->winds.positionAt(gate) - gate * steps[channel] / 2.0) < 1e-9,
                  "gate origins and world elevation projected height");
    }
    std::vector<double> peak(128);
    for (int i = 0; i < 128; ++i) peak[i] = 2 + 30 * std::exp(-0.5 * std::pow((i - 68) / 3.0, 2));
    // 族标准处理：起始 bin 15 的频率轴，参考频率在不在频带内都照常换算风速（是否在带内属硬件配置）。
    const auto channelWith = [&](double zero) {
        return DAQChannel(QString(), LinearArray2D<double>(1, 128, 0, 6, 15 * Cdl5::BinMHz, Cdl5::BinMHz, peak), zero,
                          Cdl5::WavelengthM);
    };
    const auto wind = channelWith(Cdl5::ZeroWindReferenceMHz), outside = channelWith(1.0);
    check(std::abs(wind.radialWind.front() - (83 * 500.0 / 512 - 80) * 0.775) < 0.02
          && std::abs(outside.radialWind.front() - (83 * 500.0 / 512 - 1) * 0.775) < 0.02 && wind.cnr.front() == outside.cnr.front()
          && std::abs(wind.cnr.front() - 10 * std::log10(std::sqrt(2 * std::numbers::pi) * 90 / 256)) < 0.02,
          "known peak, real bin offset, out-of-band reference still converts wind");
    std::cout << "PASS ADC0 6/15/30 m geometry, FP32, retry, chunks and coalesced batches\n";
    // 参数为实采帧文件路径：逐个回放并检查三路都能出 CNR。
    for (int i = 1; i < argc; ++i) {
        QFile file(QString::fromLocal8Bit(argv[i]));
        check(file.open(QIODevice::ReadOnly), "open captured frame");
        const auto collector = std::make_shared<Cdl5Collector>(testParameters(1));
        while (!file.atEnd()) collector->feed(file.read(7168), {});
        check(collector->settled() && collector->channels().front().spectrum.columnStart == 15 * Cdl5::BinMHz,
              "complete captured spectrum and ADC0 header metadata");
        for (int channel = 0; channel < Cdl5::Channels; ++channel) {
            const auto &measured = collector->channels()[channel];
            check(measured.cnr.size() == Cdl5::Rows
                  && std::ranges::any_of(measured.cnr, [](double v) { return std::isfinite(v); }),
                  "real spectrum produces CNR on completion");
            std::cout << "Replay " << measured.name.toStdString()
                      << ": " << Cdl5::Rows << " gates, first bin " << measured.spectrum.columnStart / Cdl5::BinMHz << ", range "
                      << measured.radialWind.positionAt(0) << ".." << measured.radialWind.positionAt(Cdl5::Rows - 1) << " m\n";
        }
    }
}
