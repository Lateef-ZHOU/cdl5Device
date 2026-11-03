#include "CallbackHandle.h"
#include <PcapHandle.h>
#include <QCoreApplication>
#include <QObject>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

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
    bool ok = false;
    const auto host = IPv4::fromString("192.0.2.1", ok);
    check(ok, "test host address");
    const auto peer = IPv4::fromString("192.0.2.2", ok);
    check(ok, "test peer address");
    const auto hostMac = MAC::fromString("02:00:00:00:00:01", ok);
    check(ok, "test host MAC");
    const auto peerMac = MAC::fromString("02:00:00:00:00:02", ok);
    check(ok, "test peer MAC");

    // Opaque byte payloads, including embedded zeroes and the final 32-byte block.
    // This tests transport round-tripping, not FPGA frame parsing or acquisition.
    for (const auto size : {32, 1024}) {
        QByteArray payload(size, '\0');
        for (int i = 0; i < size; ++i)
            payload[i] = static_cast<char>(i % 256);
        const UDPDatagram frame(hostMac, peerMac, peer, host, 5678, 1234, payload);
        check(kindOf(frame) == FrameKind::UDPDatagram, "Ethernet UDP identified");
        check(UDPDatagram(frame).bodyView() == payload, "payload preserved without a second envelope");
    }

    int diagnostics = 0;
    CallbackHandle<PcapHandle> handle([](const QByteArray &, RxTime) {});
    QObject::connect(&handle, &PcapHandle::logMessage, &app,
                     [&](const QString &) { ++diagnostics; });
    check(!handle.send(QByteArray("offline")), "unconnected transport rejects send");
    check(diagnostics == 1, "Handle owns failure diagnostic");
    handle.stop();
    handle.stop();
    check(!handle.isOpen() && !handle.isRunning(), "offline stop is idempotent");
    std::puts("PASS UDP target, binary payload roundtrip and offline lifecycle; no network opened");
}
