#include "PacketFixtures.h"
#include <Device.h>
#include "FakePcap.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

namespace {
using DeviceT = Cdl5Device;
using CollectorT = Cdl5Collector;
constexpr std::array channelNames{"ADC0_6m", "ADC0_30m", "ADC0_15m"};
constexpr std::array gateCounts{240, 240, 420};

void check(bool condition, const char *message)
{
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

template<class F>
void until(F ready, const char *message)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    check(ready(), message);
}

QByteArray fixtureFrame()
{
    QByteArray frame(CollectorT::FrameBytes, 0);
    qToLittleEndian(Cdl5::FrameMagic, frame.data());
    qToLittleEndian(Cdl5::FrameFormat, frame.data() + 4);
    qToLittleEndian(quint32((Cdl5::Rows << 16) | Cdl5::Columns), frame.data() + 8);
    qToLittleEndian(quint32((Cdl5::Profile << 12) | Cdl5::FirstBin), frame.data() + 16);
    for (qsizetype i = 0; i < CollectorT::Points; ++i)
        for (int slot = 0; slot < Cdl5::Channels; ++slot)
            qToLittleEndian(std::bit_cast<quint32>(float(slot) + 1.25f), frame.data() + (i + 1) * 32 + slot * 4);
    return frame;
}

double originalPower(const QByteArray &frame, int channel, int gate, int bin)
{
    const bool farSegment = channel == 2 && gate >= 240;
    constexpr std::array nearSlots{0, 2, 1};
    const int slot = farSegment ? 3 : nearSlots[channel];
    const int sourceGate = farSegment ? gate - 180 : gate;
    const double raw = Cdl5::power(frame.constData() + ((sourceGate * 240 + bin) + 1) * 32 + slot * 4);
    const double floor = Cdl5::power(frame.constData() + (bin + 1) * 32 + slot * 4); // 本底 = 该槽第一门
    return raw - floor;
}

QJsonObject savedJson(const QString &path)
{
    QJsonDocument document;
    until([&] {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return false;
        document = QJsonDocument::fromJson(file.readAll());
        return document.isObject();
    }, qPrintable(QStringLiteral("no valid saved JSON at %1").arg(path)));
    return document.object();
}

void verifySaved(const QJsonObject &saved, const QJsonObject &profile, const QByteArray &frame)
{
    const auto channels = saved.value("channels").toArray();
    const auto profiles = profile.value("channels").toArray();
    check(channels.size() == 3 && profiles.size() == 3, "three saved channels and profiles");
    check(saved.value("frame").toObject().value("session").toString() == "probe"
          && profile.value("frame").toObject().value("session").toString() == "probe", "saved session");
    for (int channel = 0; channel < 3; ++channel) {
        const auto data = channels[channel].toObject();
        const auto wind = profiles[channel].toObject();
        const int count = gateCounts[channel];
        const auto name = QString::fromLatin1(channelNames[channel]);
        check(data.value("name").toString() == name && wind.value("name").toString() == name, "channel order");
        const auto rows = data.value("spectrum").toArray();
        check(rows.size() == count && data.value("radialWind").toArray().size() == count
              && data.value("cnr").toArray().size() == count && wind.value("verticalSpeed").toArray().size() == count,
              "saved gate counts agree across the four outputs");
        for (int gate = 0; gate < count; ++gate) {
            const auto row = rows[gate].toArray();
            check(row.size() == 240, "saved spectrum width");
            for (int bin = 0; bin < 240; ++bin)
                check(row[bin].toDouble() == QByteArray::number(originalPower(frame, channel, gate, bin), 'g', 6).toDouble(),
                      "saved spectrum matches the input slot and gate");
        }
        if (channel != 2) {
            const int step = channel == 0 ? 6 : 30;
            check(data.value("startRange").toDouble(-1) == 0 && data.value("rangeStep").toDouble() == step
                  && wind.value("startHeight").toDouble(-1) == 0 && wind.value("heightStep").toDouble() == step / 2.0
                  && !data.contains("ranges") && !wind.contains("heights"), "uniform JSON axes");
        } else {
            const auto ranges = data.value("ranges").toArray();
            const auto heights = wind.value("heights").toArray();
            check(ranges.size() == count && heights.size() == count
                  && !data.contains("rangeStep") && !wind.contains("heightStep"), "non-uniform JSON axes");
            for (int gate = 0; gate < count; ++gate) {
                const int distance = gate < 240 ? gate * 15 : (gate - 180) * 60;
                check(ranges[gate].toDouble(-1) == distance
                      && std::abs(heights[gate].toDouble(-1) - distance / 2.0) < 1e-9,
                      "non-uniform coordinates at every gate");
            }
        }
    }
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    check(arguments.size() == 1 || arguments.size() == 3, "usage: cdl5_pipeline_probe [raw-frame output-directory]");
    const bool replay = arguments.size() == 3;
    QByteArray frame;
    if (replay) {
        QFile file(arguments[1]);
        check(file.open(QIODevice::ReadOnly), "open captured frame");
        frame = file.readAll();
    } else {
        frame = fixtureFrame();
    }
    check(frame.size() == CollectorT::FrameBytes, "exact capture size");
    QTemporaryDir temporary;
    check(replay || temporary.isValid(), "temporary output directory");
    const QDir output(QDir(replay ? arguments[2] : temporary.path()).absolutePath());
    check(output.exists(), "output directory must exist");
    check(!QFile::exists(output.filePath("probe")), "use an output directory without a probe session");

    FakePcap::reset();
    PcapParams p;
    p.pcapDeviceName = "fake0";
    p.hostMAC = *PacketProtocol::EthernetHeader::MAC::fromString("02:00:00:00:00:01");
    p.deviceMAC = *PacketProtocol::EthernetHeader::MAC::fromString("02:00:00:00:00:02");
    p.hostIPv4 = *PacketProtocol::IPv4Header::Address::fromString("192.0.2.1");
    p.deviceIPv4 = *PacketProtocol::IPv4Header::Address::fromString("192.0.2.2");
    p.hostPort = 55200;
    p.devicePort = 55200;
    DeviceT backend(nullptr);
    check(backend.PcapHandle::open(p), "open UDP backend");
    until([&] { return backend.isOpen(); }, "UDP backend did not open");
    const auto session = FakePcap::last();

    DeviceT::Channels latest;
    DeviceT::Profiles profiles;
    QObject::connect(&backend, &DeviceT::channelsReady, &app, [&](const DeviceT::Channels &channels) {
        check(QThread::currentThread() == app.thread(), "plot data delivered to main thread");
        latest = channels;
    });
    QObject::connect(&backend, &DeviceT::profilesReady, &app, [&](const DeviceT::Profiles &result) {
        check(QThread::currentThread() == app.thread(), "profiles delivered to main thread");
        profiles = result;
    });

    // 同一实采帧分别走四束。仰角错开但均值为 30°，避免同一仰角落到静风方向的不定点。
    constexpr std::array elevations{20.0, 25.0, 35.0, 40.0};
    RxTime capturedAt;
    for (int beam = 0; beam < WindProfile::kBeams; ++beam) {
        const auto sentBefore = FakePcap::sent().size();
        const bool sync = beam == WindProfile::kBeams - 1;
        const auto context = backend.paramsFor(1, WorldModel(beam * 90.0, elevations[beam]),
            sync ? std::optional(output.filesystemPath()) : std::nullopt, "probe", sync);
        bool completed = false;
        latest.reset();
        check(context && backend.begin(*context, [&](std::shared_ptr<const DAQCollector> result) {
            check(bool(result), "collector unexpectedly cancelled");
            capturedAt = result->frameTime();
            completed = true;
        }, {}), "begin typed collection");
        const auto sent = FakePcap::sent();
        check(sent.size() == sentBefore + 1 && PacketTest::body(sent.back().bytes) == Cdl5::command(1),
              "collector command through Pcap");
        for (qsizetype offset = 0; offset < frame.size(); offset += 1024)
            FakePcap::inject(session, PacketTest::udp(p.hostMAC, p.deviceMAC, p.deviceIPv4, p.hostIPv4,
                                                     p.devicePort, p.hostPort, frame.mid(offset, 1024)));
        until([&] { return completed && latest; }, "collector did not complete and publish");
    }

    const auto &mixed = std::get<2>(*latest);
    for (int gate : {239, 240, 241, 419})
        for (int bin = 0; bin < 240; ++bin)
            check(mixed.spectrum->row(gate)[bin] == originalPower(frame, 2, gate, bin), "seam spectrum before save");
    const auto &mixedProfile = std::get<2>(profiles);
    check(mixedProfile && mixedProfile->size() == 420, "production four-beam inversion produced the mixed profile");
    const auto stem = AcqSaver::fileStem(capturedAt);
    verifySaved(savedJson(output.filePath("probe/" + stem + ".json")),
                savedJson(output.filePath("probe/profile_" + stem + ".json")), frame);
    backend.close();
    std::cout << "PASS UDP -> Device/Collector -> three spectra and synchronized wind profiles, uniform and mixed JSON\n";
}
