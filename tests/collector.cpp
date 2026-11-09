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
template<Cdl5::Configuration Config> void verify()
{
    using C = Cdl5Collector<Config>;
    const auto prototype = C::create({2, {}});
    check(prototype->size() == 3 && (*prototype)[0].spectrum.capacity() >= C::Points, "factory reserves every channel");
    const auto result = prototype->makeSelf();
    const auto retry = result->makeSelf();
    check((*result)[2].spectrum.capacity() >= C::Points, "three receive areas reserved");
    const RxTime stamp = RxTime{} + std::chrono::seconds(123);
    QByteArray frame((C::Points + 1) * 32, char(-1));
    qToLittleEndian(Cdl5::FrameMagic, frame.data());
    qToLittleEndian(quint32(Config.frameFormat()), frame.data() + 4);
    qToLittleEndian(quint32((Config.rows << 16) | Config.columns), frame.data() + 8);
    qToLittleEndian(quint32(25000), frame.data() + 12);
    qToLittleEndian(quint32((10000 << 16) | (Config.profile << 12) | 15), frame.data() + 16);
    for (qsizetype i = 0; i < C::Points; ++i)
        for (int c = 0; c < 3; ++c)
            qToLittleEndian(std::bit_cast<quint32>(float(i) + 0.25f * (c + 1)), frame.data() + (i + 1) * 32 + c * 4);
    check(!result->feed(QByteArray(31, 0), stamp), "unaligned packet ignored");
    auto wrong = frame.first(1024);
    qToLittleEndian(quint32(0), wrong.data() + 8);
    check(!result->feed(wrong, stamp) && (*result)[0].spectrum.isEmpty(), "wrong dimensions rejected");
    wrong = frame.first(1024);
    qToLittleEndian(quint32(0x05040707), wrong.data() + 4);
    check(!result->feed(wrong, stamp) && (*result)[0].spectrum.isEmpty(), "wrong lane format rejected");
    wrong = frame.first(1024);
    qToLittleEndian(quint32(0), wrong.data() + 16);
    check(!result->feed(wrong, stamp) && (*result)[0].spectrum.isEmpty(), "wrong profile rejected");
    const auto clamped = C::create({999, {}});
    check(Cdl5::decodeCommand(clamped->buildStart().front()) == Config.maxAccumSeconds(), "model accumulation upper bound");
    int packets = 0;
    for (qsizetype offset = 0; offset < frame.size(); offset += 1024) {
        const auto packet = frame.mid(offset, 1024);
        const bool last = offset + packet.size() == frame.size();
        check(result->feed(packet, stamp) == last, "complete only at exact full frame size");
        ++packets;
    }
    check(packets == (Config.rows == 500 ? 2001 : 1801), "1024-byte chunks and 32-byte tail");
    for (qsizetype i = 0; i < C::Points; ++i)
        for (int c = 0; c < 3; ++c)
            check((*result)[c].spectrum[i] == double(i) + 0.25 * (c + 1), "FP32 LE decoded and widened in arrival order, meta ignored");
    check(result->frameTime() == stamp && retry->frameTime() == RxTime{}, "first packet stamp and fresh retry");
    check(result->firstBin() == 15 && result->profile() == Config.profile, "frame bin metadata");
    // 通道规格：频率轴取帧头起始 bin，多门宽规格的距离轴固定，其余规格按帧头距离模式。
    for (int c = 0; c < 3; ++c) {
        const auto &channel = (*result)[c];
        check(channel.name == Cdl5::channelTag(Config, c) && channel.rows == Config.rows && channel.columns == Config.columns
              && channel.freqMHz.size() == Config.columns && channel.freqMHz.front() == 15 * Cdl5::BinMHz
              && channel.zeroWindMHz == Cdl5::ZeroWindReferenceMHz
              && channel.rangeM == Cdl5::channelRanges(Config, c, result->uniformRange()),
              "channel specification from configuration and frame header");
        check(channel.cnr.size() == (channel.rangeM.isEmpty() ? 0 : Config.rows), "standard processing ran on completion");
    }
    check((*retry)[0].spectrum.isEmpty() && (*prototype)[0].spectrum.isEmpty(), "independent receive areas");
    check(result->feed(QByteArray(1024, 0), stamp) && (*result)[0].spectrum[0] == .25, "settled cannot be rewritten");
    auto missing = prototype->makeSelf();
    for (qsizetype offset = 0; offset < frame.size() - 32; offset += 1024)
        missing->feed(frame.mid(offset, 1024), stamp);
    check(!missing->settled(), "lost tail remains incomplete for watchdog retry");
    auto splitHeader = prototype->makeSelf();
    splitHeader->feed(frame.first(32), stamp);
    splitHeader->feed(frame.mid(32, 1024), stamp);
    check((*splitHeader)[0].spectrum.size() == 32, "header-only packet not confused with no header");

    // Npcap can receive seven original 1024-byte chunks as one 7168-byte batch.
    auto coalesced = prototype->makeSelf();
    check(!coalesced->feed(frame.first(7169), stamp) && (*coalesced)[0].spectrum.isEmpty(),
          "coalesced but unaligned input rejected without consuming header");
    for (qsizetype offset = 0; offset < frame.size() - 32; offset += 7168) {
        const auto batch = frame.mid(offset, std::min<qsizetype>(7168, frame.size() - 32 - offset));
        check(!coalesced->feed(batch, stamp), "coalesced batches still require final record");
    }
    const auto before = (*coalesced)[0].spectrum.size();
    check(!coalesced->feed(QByteArray(64, 0), stamp) && (*coalesced)[0].spectrum.size() == before,
          "oversized tail rejected without partial append");
    check(coalesced->feed(frame.last(32), stamp), "coalesced frame completes on actual tail");
    check(std::ranges::equal(*coalesced, *result, [](const auto &l, const auto &r) { return l.spectrum == r.spectrum; })
          && coalesced->frameTime() == stamp,
          "coalescing preserves every ADC value and first arrival time");
}
int main(int argc, char **argv)
{
    static_assert(std::is_same_v<decltype(Cdl5FourKCollector::create({1, {}})), std::shared_ptr<Cdl5FourKCollector>>);
    const auto unaligned = QByteArray::fromHex("780000a03f");
    check(Cdl5::power(unaligned.constData() + 1) == 1.25, "unaligned little-endian float bit interpretation");
    check(Cdl5::command(2).toHex() == "43444c3502000000", "command wire bytes");
    check(!Cdl5::decodeCommand(Cdl5::command(17)), "invalid duration rejected");
    verify<Cdl5::FourK>(); verify<Cdl5::FiveK>(); verify<Cdl5::TenKAdc0>();
    static_assert(Cdl5::ZeroWindReferenceMHz == 80.0, "zero-wind reference is a fixed model constant");
    const QVector<double> radialSpeeds(240, 1.0);
    const std::array<WindProfile::Input, 4> beams{{
        {WorldModel(0, 30, 0), radialSpeeds}, {WorldModel(90, 30, 0), radialSpeeds},
        {WorldModel(180, 30, 0), radialSpeeds}, {WorldModel(270, 30, 0), radialSpeeds}
    }};
    constexpr std::array steps{6, 15, 30};
    for (int channel = 0; channel < 3; ++channel) {
        const auto positions = Cdl5::channelRanges(Cdl5::TenKAdc0, channel, false);
        check(positions.size() == 240
              && positions == Cdl5::channelRanges(Cdl5::TenKAdc0, channel, true),
              "new positions independent of wire range flag");
        const auto profile = WindProfile::Calc(beams, positions, RxTime{});
        check(profile && profile->heights.size() == 240, "separate wind profile for each grid");
        for (int gate = 0; gate < 240; ++gate)
            check(positions[gate] == gate * steps[channel]
                  && std::abs(profile->heights[gate] - positions[gate] / 2) < 1e-9,
                  "gate origins and world elevation projected height");
    }
    const auto ranges = Cdl5::gateRanges(8, false, 450);
    check(ranges.size() == 450 && ranges[199] == 2985 && ranges[200] == 3000
          && ranges[349] == 11940 && ranges[350] == 12000 && ranges.last() == 26850,
          "Profile 8 mixed gate origins match host coordinates");
    check(Cdl5::gateRanges(8, true, 450).last() == 26940 && Cdl5::gateRanges(1, false, 450).isEmpty(),
          "frame-selected uniform mode and unsupported profile");
    const auto tenK = Cdl5::gateRanges(7, false, 240);
    check(tenK.size() == 240 && tenK[199] == 2985 && tenK[200] == 3000 && tenK.last() == 5340,
          "Profile 7 mixed gates span 5.4 km");
    check(Cdl5::gateRanges(7, true, 240).last() == 14340
          && Cdl5::gateRanges(9, false, 500).last() == 34350
          && Cdl5::gateRanges(9, true, 500).isEmpty(), "uniform P7 and mixed P9 geometry");
    QVector<double> peak(128);
    for (int i = 0; i < 128; ++i) peak[i] = 2 + 30 * std::exp(-0.5 * std::pow((i - 68) / 3.0, 2));
    // 族标准处理：起始 bin 15 的频率轴，参考在带内换算风速，带外只出 CNR。
    const auto channelWith = [&](double zero) {
        DAQChannel c;
        c.rows = 1; c.columns = 128; c.rangeM = {0}; c.freqMHz = Cdl5::binFrequencies(15, 128);
        c.zeroWindMHz = zero; c.metersPerSecondPerMHz = Cdl5::MetersPerSecondPerMHz; c.spectrum = peak;
        c.process();
        return c;
    };
    const auto wind = channelWith(Cdl5::ZeroWindReferenceMHz), uncal = channelWith(1.0);
    check(wind.calibrated() && !uncal.calibrated() && std::abs(wind.radialWind.front() - (83 * 500.0 / 512 - 80) * 0.775) < 0.02
          && std::isnan(uncal.radialWind.front()) && wind.cnr.front() == uncal.cnr.front()
          && std::abs(wind.cnr.front() - 10 * std::log10(std::sqrt(2 * std::numbers::pi) * 90 / 256)) < 0.02,
          "known peak, real bin offset, out-of-band reference still produces identical CNR");
    std::cout << "PASS three FPGA geometries, FP32, prototype/retry, chunks and coalesced batches\n";
    const auto replay = []<class Collector>(const QString &path) {
        QFile file(path);
        check(file.open(QIODevice::ReadOnly), "open captured frame");
        const auto collector = Collector::create({1, {}});
        while (!file.atEnd()) collector->feed(file.read(7168), {});
        constexpr auto config = Collector::Specification;
        check(collector->settled() && collector->profile() == config.profile, "complete captured spectrum");
        if constexpr (config.adc0Multires())
            check(collector->firstBin() == 15, "ADC0 capture header metadata");
        for (int channel = 0; channel < config.channels; ++channel) {
            const auto &measured = (*collector)[channel];
            check(measured.cnr.size() == measured.rangeM.size()
                  && std::ranges::any_of(measured.cnr, [](double v) { return std::isfinite(v); }),
                  "real spectrum produces CNR on completion");
            std::cout << "Replay " << measured.name.toStdString()
                      << ": " << measured.rangeM.size() << " gates, first bin " << collector->firstBin()
                      << ", range " << measured.rangeM.first() << ".." << measured.rangeM.last() << " m\n";
        }
    };
    bool adc0 = false;
    for (int i = 1; i < argc; ++i) {
        const auto arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--adc0") {
            adc0 = true;
            continue;
        }
        if (adc0) replay.operator()<Cdl5TenKCollector>(arg);
        else replay.operator()<Cdl5FiveKCollector>(arg);
    }
}
