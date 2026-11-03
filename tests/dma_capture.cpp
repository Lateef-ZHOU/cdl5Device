#include "DmaFixture.h"
int main(int argc,char **argv)
{
    DmaFixture fixture;
    QCoreApplication app(argc,argv);
    for(int gates:{500,450,240}) {
        prepare(gates);
        Cdl5DmaHandle handle([&](const QByteArray &bytes,RxTime) {
            chunks.push_back(bytes);
            if(chunks.size()==static_cast<size_t>(gates==500?2001:1801)) delivered.release();
        });
        QObject::connect(&handle,&Cdl5DmaHandle::connectionChanged,&handle,[](HandleCore::ResourcePhase phase) { if(phase == HandleCore::ResourcePhase::Acquired)connected.release(); },Qt::DirectConnection);
        QObject::connect(&handle,&Cdl5DmaHandle::logMessage,&handle,[](const QString &s) { ++logs;std::cerr<<s.toStdString()<<'\n'; },Qt::DirectConnection);
        auto params=Cdl5DmaHandle::parameters("/fake/dma");params.memoryDevice="/fake/mem";
        check(handle.start(params)&&connected.tryAcquire(1,2000),"open concrete DMA handle");
        check(!handle.send(QByteArray("bad")),"bad command rejected");
        check(handle.send(Cdl5::command(1)),"start command accepted");
        check(delivered.tryAcquire(1,4000),"full frame delivered");
        handle.stop();
        QByteArray combined;
        for(size_t i=0;i<chunks.size();++i) {
            check(chunks[i].size()==(i+1==chunks.size()?32:1024),"record-aligned tail");combined+=chunks[i];
        }
        check(combined==source,"short reads and EINTR preserve every byte");
        check(starts==1&&resets==1&&!handle.isRunning(),"one transfer, reset, joined worker");
        while(started.tryAcquire()) {}
        cancelCase=true;chunks.clear();
        check(handle.start(params)&&connected.tryAcquire(1,2000),"reopen");
        check(handle.send(Cdl5::command(1))&&started.tryAcquire(1,2000),"capture pending");
        handle.stop();
        check(chunks.empty()&&!handle.isRunning(),"closing pending capture discards and joins");
        cancelCase=false;
    }
    prepare(240);
    regs[0x84/4] = (regs[0x84/4] & ~(15u << 24)) | (5u << 24);
    {
        QSemaphore rejected;
        Cdl5DmaHandle handle([](const QByteArray &,RxTime) { check(false,"invalid geometry cannot upload"); });
        QObject::connect(&handle,&Cdl5DmaHandle::connectionChanged,&handle,[](HandleCore::ResourcePhase phase) {
            if(phase == HandleCore::ResourcePhase::Acquired) connected.release();
        },Qt::DirectConnection);
        QObject::connect(&handle,&Cdl5DmaHandle::logMessage,&handle,[&](const QString &s) {
            if(s.startsWith("Unsupported")) rejected.release();
        },Qt::DirectConnection);
        auto params=Cdl5DmaHandle::parameters("/fake/dma");params.memoryDevice="/fake/mem";
        check(handle.start(params)&&connected.tryAcquire(1,2000),"geometry probe connected");
        check(handle.send(Cdl5::command(1))&&rejected.tryAcquire(1,2000),"mismatched lane count diagnosed");
        handle.stop();check(starts==0&&resets==0,"invalid profile does not arm DMA or modify configuration");
    }
    for(auto fail:{Fault::Start,Fault::Wait,Fault::Read,Fault::ShortTransfer}) {
        prepare(500);fault=fail;QSemaphore error;
        Cdl5DmaHandle handle([](const QByteArray &,RxTime) { check(false,"failed capture cannot upload"); });
        QObject::connect(&handle,&Cdl5DmaHandle::connectionChanged,&handle,[](HandleCore::ResourcePhase phase) { if(phase == HandleCore::ResourcePhase::Acquired)connected.release(); },Qt::DirectConnection);
        QObject::connect(&handle,&Cdl5DmaHandle::logMessage,&handle,[&](const QString &s) {
            if(s.startsWith("DMA")||s.startsWith("Unexpected")) error.release();
        },Qt::DirectConnection);
        auto params=Cdl5DmaHandle::parameters("/fake/dma");params.memoryDevice="/fake/mem";
        check(handle.start(params)&&connected.tryAcquire(1,2000),"error probe connected");
        check(handle.send(Cdl5::command(1)),"error probe accepted");
        check(error.tryAcquire(1,2000),"failure diagnosed");handle.stop();
        check(resets==1&&!handle.isRunning()&&regs[0x04/4]==0,"failed transfer released and worker joined");
    }
    std::cout<<"PASS DMA start/wait/read/cancel/errors, 4k, 5k and 10k, 1024-byte tails; mocked hardware only\n";
}
