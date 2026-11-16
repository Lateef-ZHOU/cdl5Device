#include "PacketFixtures.h"
#include <vector>
#include <string>
#include "CallbackHandle.h"
#include <PcapHandle.h>
#include <QCoreApplication>
#include <QObject>
#include <cstdio>
#include <cstdlib>
#include <type_traits>


// 将既有 Qt 测试命令转换为自有字节。
static std::vector<unsigned char> sendBytes(const QByteArray &bytes)
{
    return {reinterpret_cast<const unsigned char *>(bytes.constData()),
            reinterpret_cast<const unsigned char *>(bytes.constData()) + bytes.size()};
}

namespace {
void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", message);
        std::exit(1);
    }
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto host = *PacketProtocol::IPv4Header::Address::fromString("192.0.2.1");
    const auto peer = *PacketProtocol::IPv4Header::Address::fromString("192.0.2.2");
    const auto hostMac = *PacketProtocol::EthernetHeader::MAC::fromString("02:00:00:00:00:01");
    const auto peerMac = *PacketProtocol::EthernetHeader::MAC::fromString("02:00:00:00:00:02");

    // Opaque byte payloads, including embedded zeroes and the final 32-byte block.
    // This tests transport round-tripping, not FPGA frame parsing or acquisition.
    for (const auto size : {32, 1024}) {
        QByteArray payload(size, '\0');
        for (int i = 0; i < size; ++i)
            payload[i] = static_cast<char>(i % 256);
        const auto frame = PacketTest::udp(hostMac, peerMac, peer, host, 5678, 1234, payload);
        check(PacketTest::kind(frame) == PacketProtocol::FrameKind::UDP, "Ethernet UDP identified");
        check(PacketTest::body(frame) == payload, "payload preserved without a second envelope");
    }

    int diagnostics = 0;
    CallbackHandle<PcapHandle> handle([](const QByteArray &, RxTime) {});
    QObject::connect(&handle, &PcapHandle::logMessage, &app,
                     [&](const std::string &) { ++diagnostics; });
    check(!handle.send(sendBytes(QByteArray("offline"))), "unconnected transport rejects send");
    check(diagnostics == 1, "Handle owns failure diagnostic");
    handle.close();
    handle.close();
    check(!handle.isOpen() && !handle.isRunning(), "offline stop is idempotent");
    std::puts("PASS UDP target, binary payload roundtrip and offline lifecycle; no network opened");
}
