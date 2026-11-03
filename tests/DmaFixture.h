#pragma once
#include <Cdl5/AcquisitionHandle.h>
#include <QCoreApplication>
#include <QSemaphore>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <vector>

namespace {
constexpr int DmaFd=30001, MemFd=30002;
volatile quint32 *regs;
QByteArray source;
qsizetype position=0;
std::atomic<int> starts{0}, resets{0}, reads{0}, waits{0};
std::atomic<bool> cancelCase{false};
enum class Fault { None, Start, Wait, Read, ShortTransfer };
std::atomic<Fault> fault{Fault::None};
QSemaphore connected, started, delivered;
std::vector<QByteArray> chunks;
std::atomic<int> logs{0};
void check(bool condition, const char *message)
{ if (!condition) { std::cerr << "FAIL " << message << '\n'; std::abort(); } }
void prepare(int gates)
{
    for (int i=0;i<1024;++i) regs[i]=0;
    const auto config = gates == 500 ? Cdl5::FourK : gates == 450 ? Cdl5::FiveK : Cdl5::TenK;
    regs[0x84/4]=(quint32(config.profile)<<28)|(quint32(config.lanes)<<24)|(quint32(gates)<<8)|config.columns;
    regs[0x0c/4]=7;
    regs[0x1c/4]=250000000 / config.rateHz;
    regs[0x24/4]=0xff8;
    source=QByteArray((gates*config.columns+1)*32,Qt::Uninitialized);
    for(qsizetype i=0;i<source.size();++i) source[i]=char(i%251);
    regs[0x10/4]=static_cast<quint32>(source.size()/4);
    starts=0;resets=0;reads=0;waits=0;logs=0;chunks.clear();fault=Fault::None;
}
}
extern "C" int __real_open64(const char *,int,...);
extern "C" int __real_close(int);
extern "C" int __real_ioctl(int,unsigned long,...);
extern "C" void *__real_mmap64(void *,size_t,int,int,int,off64_t);
extern "C" int __real_munmap(void *,size_t);
extern "C" ssize_t __real_read(int,void *,size_t);
extern "C" off64_t __real_lseek64(int,off64_t,int);
extern "C" int __wrap_open64(const char *path,int flags,...)
{
    if(std::string_view(path)=="/fake/dma") { check((flags&O_ACCMODE)==O_RDONLY,"read-only DMA");return DmaFd; }
    if(std::string_view(path)=="/fake/mem") return MemFd;
    mode_t mode=0;
    if(flags&O_CREAT) { va_list args;va_start(args,flags);mode=static_cast<mode_t>(va_arg(args,int));va_end(args); }
    return __real_open64(path,flags,mode);
}
extern "C" int __wrap_close(int fd)
{ return fd==DmaFd||fd==MemFd?0:__real_close(fd); }
extern "C" void *__wrap_mmap64(void *p,size_t n,int prot,int flags,int fd,off64_t off)
{ return fd==MemFd?const_cast<quint32 *>(regs):__real_mmap64(p,n,prot,flags,fd,off); }
extern "C" int __wrap_munmap(void *p,size_t n)
{ return p==regs?0:__real_munmap(p,n); }
extern "C" int __wrap_ioctl(int fd,unsigned long cmd,...)
{
    va_list args;va_start(args,cmd);
    void *p=cmd==CDL5_DMA_RESET?nullptr:va_arg(args,void *);va_end(args);
    if(fd!=DmaFd) return __real_ioctl(fd,cmd,p);
    if(cmd==CDL5_DMA_CAPS) { *static_cast<quint32 *>(p)=1;return 0; }
    if(cmd==CDL5_DMA_CAPACITY) { *static_cast<quint32 *>(p)=4*1024*1024;return 0; }
    if(cmd==CDL5_DMA_RESET) { ++resets;return 0; }
    auto *request=static_cast<cdl5_dma_ioc *>(p);
    if(cmd==CDL5_DMA_START) {
        check(regs[0x04/4]==0,"DMA armed before FPGA trigger");
        check(request->length==static_cast<quint32>(source.size()),"frame length read from FPGA");
        check(regs[0x64/4]==250000000u / regs[0x1c/4],"accumulation follows active trigger period");
        ++starts;position=0;waits=0;started.release();
        if(fault==Fault::Start) { errno=EIO;return -1; }
        return 0;
    }
    check(cmd==CDL5_DMA_WAIT_KEEP,"WAIT_KEEP, not destructive WAIT");
    check(request->timeout_ms==100,"bounded driver waits");
    if(fault==Fault::Wait) {
        regs[0x08/4]=1;regs[0xb0/4]=2; // Failed DMA leaves PL active until soft reset.
        errno=EIO;return -1;
    }
    if(cancelCase && waits.fetch_add(1)<5) {
        regs[0x08/4]=1;regs[0xb0/4]=4; // Disabling trigger still drains the current frame.
        usleep(1000);errno=EAGAIN;return -1;
    }
    request->status=0;request->transferred=request->length;regs[0xb0/4]=0;regs[0x08/4]=0;
    if(fault==Fault::ShortTransfer) request->transferred-=32;
    return 0;
}
extern "C" off64_t __wrap_lseek64(int fd,off64_t offset,int whence)
{
    if(fd!=DmaFd) return __real_lseek64(fd,offset,whence);
    check(offset==0&&whence==SEEK_SET,"read offset reset per frame");position=0;return 0;
}
extern "C" ssize_t __wrap_read(int fd,void *data,size_t size)
{
    if(fd!=DmaFd) return __real_read(fd,data,size);
    if(fault==Fault::Read) { errno=EIO;return -1; }
    if(reads.fetch_add(1)==0) { errno=EINTR;return -1; }
    const auto n=std::min<qsizetype>({static_cast<qsizetype>(size),137,source.size()-position});
    memcpy(data,source.constData()+position,static_cast<size_t>(n));position+=n;return n;
}

class DmaFixture {
public:
    DmaFixture() {
        regs=static_cast<volatile quint32 *>(__real_mmap64(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0));
        check(regs!=MAP_FAILED,"test shared MMIO allocation");
        child_=fork();check(child_>=0,"test MMIO helper fork");
        if(child_==0) {
            prctl(PR_SET_PDEATHSIG,SIGTERM);
            if(getppid()==1) _exit(1);
            for(;;) {
                if(regs[0xc0/4]) { regs[0x64/4]=regs[0x5c/4];regs[0xc0/4]=0;regs[0x74/4]=regs[0x74/4]+1; }
                if(regs[0x04/4]&(1u<<16)) { regs[0xb0/4]=0;regs[0x08/4]=0;regs[0x04/4]=0; }
                usleep(100);
            }
        }
    }
    ~DmaFixture() {
        kill(child_,SIGTERM);waitpid(child_,nullptr,0);
        __real_munmap(const_cast<quint32 *>(regs),4096);
    }
private:
    pid_t child_;
};
