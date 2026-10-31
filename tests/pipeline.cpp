#include <Cdl5/Device.h>
#include "FakePcap.h"
#include <QCoreApplication>
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
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir output;check(output.isValid(),"temporary output directory");
    FakePcap::reset();
    bool ok=false;
    PcapParams p;
    p.host={"fake","fake","fake0",MAC::fromString("02:00:00:00:00:01",ok)};
    p.deviceMAC=MAC::fromString("02:00:00:00:00:02",ok);
    p.hostIPv4=IPv4::fromString("192.0.2.1",ok);p.deviceIPv4=IPv4::fromString("192.0.2.2",ok);
    p.hostPort=55200;p.devicePort=55200;
    Cdl5Device backend(nullptr);
    check(backend.device().open(p),"open UDP backend");
    until([&] { return backend.device().isOpen(); });
    int signals=0;
    QObject::connect(&backend,&Cdl5Device::spectrumReady,&app,[&](int channel,const QVector<double> &values) {
        check(QThread::currentThread()==app.thread(),"plot data delivered to main thread");
        check(values.size()==64000&&values.front()==channel+1.25&&values.back()==channel+1.25,"three independent spectra");
        ++signals;
    });
    bool completed=false;
    DAQContext context;context.saveRoot=QDir(output.path());context.sessionDir="probe";
    const auto prototype=backend.collectorFor(1,std::move(context));
    check(backend.begin(prototype,[&](std::shared_ptr<const DAQCollector> frame) { completed=bool(frame); },{}),"begin typed collection");
    const auto sent=FakePcap::sent();
    check(!sent.empty()&&UDPDatagram(sent.front().bytes).bodyView()==Cdl5::command(1),"collector command through Pcap");
    QByteArray frame(64001*32,0);
    qToLittleEndian(Cdl5::FrameMagic,frame.data());qToLittleEndian(quint32(0x05040507),frame.data()+4);
    qToLittleEndian(quint32((500<<16)|128),frame.data()+8);
    for(int i=0;i<64000;++i) for(int c=0;c<3;++c)
        qToLittleEndian(std::bit_cast<quint32>(float(c)+1.25f),frame.data()+(i+1)*32+c*4);
    const auto session=FakePcap::last();
    for(qsizetype offset=0;offset<frame.size();offset+=1024)
        FakePcap::inject(session,UDPDatagram(p.host.macAddress,p.deviceMAC,p.deviceIPv4,p.hostIPv4,
                                           p.devicePort,p.hostPort,frame.mid(offset,1024)));
    until([&] { return completed&&signals==3; });
    QStringList files;
    until([&] {
        files.clear();QDirIterator it(output.path(),{"*.bin"},QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()) files<<it.next();
        if(files.size()!=3) return false;
        for(const auto &path:files) if(QFileInfo(path).size()!=64000*8) return false;
        return true;
    });
    for(int channel=0;channel<3;++channel) {
        QString file;
        for(const auto &path:files) if(QFileInfo(path).fileName().startsWith(QString("ADC%1_").arg(channel))) file=path;
        check(!file.isEmpty(),"per-channel filenames");QFile f(file);check(f.open(QIODevice::ReadOnly),"open result");
        const auto bytes=f.readAll();
        for(qsizetype i=0;i<64000;++i)
            check(std::bit_cast<double>(qFromLittleEndian<quint64>(bytes.constData()+i*8))==channel+1.25,"little-endian double saved intact");
    }
    backend.close();
    std::cout<<"PASS UDP -> Device/Collector -> consume thread -> three signals and three saved spectra\n";
}
