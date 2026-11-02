#include <Cdl5/Collector.h>
#include <Cdl5/WindProcessing.h>
#include <QFile>
#include <numbers>
#include <cstdlib>
#include <iostream>
#include <type_traits>
void check(bool value, const char *reason)
{ if (!value) { std::cerr << reason << '\n'; std::exit(1); } }
template<int Gates> void verify()
{
    using C = Cdl5Collector<Gates>;
    const auto prototype = C::createPrototype(2, {});
    check(prototype->constFrame()[0].capacity() == 0, "prototype allocates no spectrum");
    const auto result = prototype->makeSelf();
    const auto retry = result->makeSelf();
    check(result->constFrame()[2].capacity() >= C::Points, "three receive areas reserved");
    const RxTime stamp = RxTime{} + std::chrono::seconds(123);
    QByteArray frame((C::Points + 1) * 32, char(-1));
    qToLittleEndian(Cdl5::FrameMagic, frame.data());
    qToLittleEndian(quint32(0x05040507), frame.data() + 4);
    qToLittleEndian(quint32((Gates << 16) | 128), frame.data() + 8);
    for (qsizetype i = 0; i < C::Points; ++i)
        for (int c = 0; c < 3; ++c)
            qToLittleEndian(std::bit_cast<quint32>(float(i) + 0.25f * (c + 1)), frame.data() + (i + 1) * 32 + c * 4);
    check(!result->feed(QByteArray(31, 0), stamp), "unaligned packet ignored");
    auto wrong = frame.first(1024);
    qToLittleEndian(quint32(0), wrong.data() + 8);
    check(!result->feed(wrong, stamp) && result->constFrame()[0].isEmpty(), "wrong dimensions rejected");
    int packets = 0;
    for (qsizetype offset = 0; offset < frame.size(); offset += 1024) {
        const auto packet = frame.mid(offset, 1024);
        const bool last = offset + packet.size() == frame.size();
        check(result->feed(packet, stamp) == last, "complete only at exact full frame size");
        ++packets;
    }
    check(packets == (Gates == 500 ? 2001 : 1801), "1024-byte chunks and 32-byte tail");
    for (qsizetype i = 0; i < C::Points; ++i)
        for (int c = 0; c < 3; ++c)
            check(result->constFrame()[c][i] == double(i) + 0.25 * (c + 1), "FP32 LE decoded and widened in arrival order, meta ignored");
    check(result->frameTime() == stamp && retry->frameTime() == RxTime{}, "first packet stamp and fresh retry");
    check(retry->constFrame()[0].isEmpty() && prototype->constFrame()[0].isEmpty(), "independent receive areas");
    check(result->feed(QByteArray(1024, 0), stamp) && result->constFrame()[0][0] == .25, "settled cannot be rewritten");
    auto missing = prototype->makeSelf();
    for (qsizetype offset = 0; offset < frame.size() - 32; offset += 1024)
        missing->feed(frame.mid(offset, 1024), stamp);
    check(!missing->settled(), "lost tail remains incomplete for watchdog retry");
    auto splitHeader = prototype->makeSelf();
    splitHeader->feed(frame.first(32), stamp);
    splitHeader->feed(frame.mid(32, 1024), stamp);
    check(splitHeader->constFrame()[0].size() == 32, "header-only packet not confused with no header");

    // Npcap can receive seven original 1024-byte chunks as one 7168-byte batch.
    auto coalesced = prototype->makeSelf();
    check(!coalesced->feed(frame.first(7169), stamp) && coalesced->constFrame()[0].isEmpty(),
          "coalesced but unaligned input rejected without consuming header");
    for (qsizetype offset = 0; offset < frame.size() - 32; offset += 7168) {
        const auto batch = frame.mid(offset, std::min<qsizetype>(7168, frame.size() - 32 - offset));
        check(!coalesced->feed(batch, stamp), "coalesced batches still require final record");
    }
    const auto before = coalesced->constFrame()[0].size();
    check(!coalesced->feed(QByteArray(64, 0), stamp) && coalesced->constFrame()[0].size() == before,
          "oversized tail rejected without partial append");
    check(coalesced->feed(frame.last(32), stamp), "coalesced frame completes on actual tail");
    check(coalesced->constFrame() == result->constFrame() && coalesced->frameTime() == stamp,
          "coalescing preserves every ADC value and first arrival time");
}
int main(int argc, char **argv)
{
    static_assert(std::is_same_v<decltype(Cdl5FourKCollector::createPrototype(1, {})), std::shared_ptr<const Cdl5FourKCollector>>);
    const auto unaligned = QByteArray::fromHex("780000a03f");
    check(Cdl5::power(unaligned.constData() + 1) == 1.25, "unaligned little-endian float bit interpretation");
    check(Cdl5::command(2).toHex() == "43444c3502000000", "command wire bytes");
    check(!Cdl5::decodeCommand(Cdl5::command(14)), "invalid duration rejected");
    verify<500>(); verify<450>();
    const auto ranges = Cdl5::gateRanges(8, false, 450);
    check(ranges.size() == 450 && ranges[199] == 2985 && ranges[200] == 3000
          && ranges[349] == 11940 && ranges[350] == 12000 && ranges.last() == 26850,
          "Profile 8 mixed gate origins match host coordinates");
    check(Cdl5::gateRanges(8, true, 450).last() == 26940 && Cdl5::gateRanges(1, false, 450).isEmpty(),
          "frame-selected uniform mode and unsupported profile");
    QVector<double> peak(128);
    for (int i = 0; i < 128; ++i) peak[i] = 2 + 30 * std::exp(-0.5 * std::pow((i - 68) / 3.0, 2));
    const auto [wind, valid] = Cdl5::radial(peak, 1, 128, 15, 80.0);
    const auto [uncal, absent] = Cdl5::radial(peak, 1, 128, 15, std::nullopt);
    check(valid && !absent && std::abs(wind.ws.front() - (83 * 500.0 / 512 - 80) * 0.775) < 0.02
          && std::isnan(uncal.ws.front()) && wind.cnr.front() == uncal.cnr.front()
          && std::abs(wind.cnr.front() - 10 * std::log10(std::sqrt(2 * std::numbers::pi) * 90 / 256)) < 0.02,
          "known peak, real bin offset, missing reference still produces identical CNR");
    std::cout << "PASS both FPGA geometries, FP32, prototype/retry, chunks and coalesced batches\n";
    for (int i = 1; i < argc; ++i) {
        QFile file(QString::fromLocal8Bit(argv[i]));
        check(file.open(QIODevice::ReadOnly), "open captured frame");
        const auto collector = Cdl5FiveKCollector::createPrototype(1, {})->makeSelf();
        while (!file.atEnd()) collector->feed(file.read(7168), {});
        check(collector->settled() && collector->profile() == 8, "captured Profile 8 frame");
        const auto physical = Cdl5::gateRanges(collector->profile(), collector->uniformRange(), collector->Rows);
        for (const auto &spectrum : collector->constFrame()) {
            const auto [measured, configured] = Cdl5::radial(spectrum, collector->Rows, collector->Columns,
                                                              collector->firstBin(), {});
            check(!configured && measured.cnr.size() == physical.size()
                  && std::ranges::any_of(measured.cnr, [](double v) { return std::isfinite(v); }),
                  "real spectrum produces CNR without inventing a calibrated velocity");
        }
        std::cout << "Replay: 3 ADCs x " << physical.size() << " gates, first bin " << collector->firstBin()
                  << ", range " << physical.first() << ".." << physical.last() << " m\n";
    }
}
