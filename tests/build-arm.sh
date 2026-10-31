#!/bin/sh
set -eu
# Existing board SDK image: lidar-arm-runtime-sdk:20261030.
# Mount this repo at /src, DmaProxy at /proxy, HandleCore at /handle,
# the board-runtime-20261030 sysroot at /work, and a writable output at /out.
prefix=/opt/lidar-runtime
compile() {
 aarch64-linux-gnu-g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror \
    --sysroot=/work/sysroot -B/work/sysroot/usr/lib/aarch64-linux-gnu/ \
    -L/work/sysroot/usr/lib/aarch64-linux-gnu -L/work/sysroot/lib/aarch64-linux-gnu \
    -nostdinc++ -isystem "$prefix/include/c++/12.2.0" \
    -isystem "$prefix/include/c++/12.2.0/aarch64-linux-gnu" \
    -isystem "$prefix/include/c++/12.2.0/backward" \
    -DQT_NO_KEYWORDS -D_FILE_OFFSET_BITS=64 \
    -I/src/include -I/proxy -I/handle/include \
    -isystem "$prefix/include" -isystem "$prefix/include/QtCore" \
    "$@" \
    -L"$prefix/lib" -Wl,-rpath-link,"$prefix/lib" -Wl,-rpath,"$prefix/lib" \
    -lQt6Core -pthread \
    -Wl,--wrap=open64,--wrap=close,--wrap=ioctl,--wrap=mmap64,--wrap=munmap
}
compile /src/tests/dma_connection.cpp -Wl,--wrap=sysconf -o /out/cdl5_dma_connection_probe-arm64
/work/hostqt/libexec/moc -DQT_NO_KEYWORDS -I/src/include -I/handle/include -I/proxy \
    -I"$prefix/include" -I"$prefix/include/QtCore" \
    /src/include/Cdl5/AcquisitionHandle.h -o /out/moc_AcquisitionHandle.cpp
compile /src/tests/dma_capture.cpp /out/moc_AcquisitionHandle.cpp \
    -Wl,--wrap=read,--wrap=lseek64 -o /out/cdl5_dma_capture_probe-arm64
aarch64-linux-gnu-readelf -V /out/cdl5_dma_capture_probe-arm64
