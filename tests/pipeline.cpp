#include "PacketFixtures.h"
#include <Device.h>
#include "FakePcap.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDirIterator>
#include <QFile>
#include <iostream>
#include <cstdlib>
static constexpr const char *channelNames[] = {"ADC0_6m", "ADC0_15m", "ADC0_30m"};

void check(bool condition,const char *message)
{ if(!condition) { std::cerr<<message<<'\n';std::exit(1); } }
template<class F> void until(F ready)
{
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!ready()&&std::chrono::steady_clock::now()<deadline) {
        QCoreApplication::processEvents();QThread::msleep(1);
    }
    check(ready(),"production pipeline timed out");
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using DeviceT = Cdl5Device;
    using C = Cdl5Collector;
    QTemporaryDir output;check(output.isValid(),"temporary output directory");
    FakePcap::reset();
    PcapParams p;
    p.pcapDeviceName="fake0";
    p.hostMAC=*PacketProtocol::EthernetHeader::MAC::fromString("02:00:00:00:00:01");
    p.deviceMAC=*PacketProtocol::EthernetHeader::MAC::fromString("02:00:00:00:00:02");
    p.hostIPv4=*PacketProtocol::IPv4Header::Address::fromString("192.0.2.1");p.deviceIPv4=*PacketProtocol::IPv4Header::Address::fromString("192.0.2.2");
    p.hostPort=55200;p.devicePort=55200;
    DeviceT backend(nullptr);
    constexpr auto points = C::Points;
    check(backend.open(p),"open UDP backend");
    until([&] { return backend.isOpen(); });
    int signals=0;
    QObject::connect(&backend,&DeviceT::channelsReady,&app,[&](const DeviceT::Channels &channels) {
        check(QThread::currentThread()==app.thread(),"plot data delivered to main thread");
        int index = 0;
        std::apply([&](const auto &...channel) {
            const auto verify = [&](const auto &value) {
                check(static_cast<qsizetype>(value.spectrum->size()) == points
                    && (*value.spectrum)[0] == index + 1.25 && (*value.spectrum)[points - 1] == index + 1.25,
                    "three independent static spectra");
                ++index; ++signals;
            };
            (verify(channel), ...);
        }, *channels);
    });
    bool completed=false;
    const auto context=*backend.paramsFor(1,std::nullopt,QDir(output.path()),"probe",false);
    check(backend.begin(context,[&](std::shared_ptr<const DAQCollector> frame) { completed=bool(frame); },{}),"begin typed collection");
    const auto sent=FakePcap::sent();
    check(!sent.empty()&&PacketTest::body(sent.front().bytes)==Cdl5::command(1),"collector command through Pcap");
    QByteArray frame((points+1)*32,0);
    qToLittleEndian(Cdl5::FrameMagic,frame.data());qToLittleEndian(Cdl5::FrameFormat,frame.data()+4);
    qToLittleEndian(quint32((Cdl5::Rows<<16)|Cdl5::Columns),frame.data()+8);
    qToLittleEndian(quint32((Cdl5::Profile << 12) | 15),frame.data()+16);
    for(qsizetype i=0;i<points;++i) for(int c=0;c<3;++c)
        qToLittleEndian(std::bit_cast<quint32>(float(c)+1.25f),frame.data()+(i+1)*32+c*4);
    const auto session=FakePcap::last();
    for(qsizetype offset=0;offset<frame.size();offset+=1024)
        FakePcap::inject(session,PacketTest::udp(p.hostMAC,p.deviceMAC,p.deviceIPv4,p.hostIPv4,
                                           p.devicePort,p.hostPort,frame.mid(offset,1024)));
    until([&] { return completed&&signals==3; });
    // 族落盘：每帧一个 JSON，三路按通道规格顺序、名称写出功率谱。
    QJsonObject saved;
    until([&] {
        QDirIterator it(output.path(),{"*.json"},QDir::Files,QDirIterator::Subdirectories);
        if(!it.hasNext()) return false;
        QFile f(it.next());
        if(!f.open(QIODevice::ReadOnly)) return false;
        const auto document=QJsonDocument::fromJson(f.readAll());
        saved=document.object();
        return document.isObject();
    });
    const auto channels=saved.value("channels").toArray();
    check(channels.size()==3&&saved.value("frame").toObject().value("session").toString()=="probe","one frame file per completed frame");
    for(int channel=0;channel<3;++channel) {
        const auto object=channels[channel].toObject();
        check(object.value("name").toString()==QString::fromUtf8(channelNames[channel]),"channel names in specification order");
        const auto rows=object.value("spectrum").toArray();
        check(rows.size()==Cdl5::Rows&&rows[0].toArray().size()==Cdl5::Columns,"spectrum rows by range gate");
        for(const auto &row:rows) for(const auto &value:row.toArray())
            check(value.toDouble()==channel+1.25,"spectrum values saved");
    }
    backend.close();
    std::cout<<"PASS UDP -> Device/Collector -> consume thread -> three signals and three saved spectra\n";
}
