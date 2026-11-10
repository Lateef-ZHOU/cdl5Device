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
template<class DeviceT> void verify(QCoreApplication &app)
{
    using C = typename DeviceT::CollectorType;
    QTemporaryDir output;check(output.isValid(),"temporary output directory");
    FakePcap::reset();
    bool ok=false;
    PcapParams p;
    p.host={"fake","fake","fake0",MAC::fromString("02:00:00:00:00:01",ok)};
    p.deviceMAC=MAC::fromString("02:00:00:00:00:02",ok);
    p.hostIPv4=IPv4::fromString("192.0.2.1",ok);p.deviceIPv4=IPv4::fromString("192.0.2.2",ok);
    p.hostPort=55200;p.devicePort=55200;
    DeviceT backend(nullptr);
    constexpr auto points = C::Points;
    check(backend.open(p),"open UDP backend");
    until([&] { return backend.isOpen(); });
    int signals=0;
    QObject::connect(&backend,&DeviceT::spectrumReady,&app,[&](int channel,const QVector<double> &values) {
        check(QThread::currentThread()==app.thread(),"plot data delivered to main thread");
        check(values.size()==points&&values.front()==channel+1.25&&values.back()==channel+1.25,"three independent spectra");
        ++signals;
    });
    bool completed=false;
    DAQParameters context{1};context.saveRoot=QDir(output.path());context.sessionDir="probe";
    check(backend.begin(context,[&](std::shared_ptr<const DAQCollector> frame) { completed=bool(frame); },{}),"begin typed collection");
    const auto sent=FakePcap::sent();
    check(!sent.empty()&&UDPDatagram(sent.front().bytes).bodyView()==Cdl5::command(1),"collector command through Pcap");
    QByteArray frame((points+1)*32,0);
    qToLittleEndian(Cdl5::FrameMagic,frame.data());qToLittleEndian(quint32(C::Specification.frameFormat()),frame.data()+4);
    qToLittleEndian(quint32((C::Rows<<16)|C::Columns),frame.data()+8);
    qToLittleEndian(quint32((C::Specification.profile << 12) | 15),frame.data()+16);
    for(qsizetype i=0;i<points;++i) for(int c=0;c<3;++c)
        qToLittleEndian(std::bit_cast<quint32>(float(c)+1.25f),frame.data()+(i+1)*32+c*4);
    const auto session=FakePcap::last();
    for(qsizetype offset=0;offset<frame.size();offset+=1024)
        FakePcap::inject(session,UDPDatagram(p.host.macAddress,p.deviceMAC,p.deviceIPv4,p.hostIPv4,
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
        check(object.value("name").toString()==Cdl5::channelTag(C::Specification, channel),"channel names in specification order");
        const auto rows=object.value("spectrum").toArray();
        check(rows.size()==C::Rows&&rows[0].toArray().size()==C::Columns,"spectrum rows by range gate");
        for(const auto &row:rows) for(const auto &value:row.toArray())
            check(value.toDouble()==channel+1.25,"spectrum values saved");
    }
    backend.close();
    std::cout<<"PASS UDP -> Device/Collector -> consume thread -> three signals and three saved spectra\n";
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    verify<Cdl5FourKDevice>(app);
    verify<Cdl5FiveKDevice>(app);
    verify<Cdl5TenKDevice>(app);
}
