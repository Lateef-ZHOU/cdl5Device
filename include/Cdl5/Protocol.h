#pragma once
#include <bit>
#include <limits>
#include <optional>
#include <QByteArray>
#include <QByteArrayView>
#include <QtEndian>

namespace Cdl5 {
inline constexpr qsizetype RecordBytes = 32;
inline constexpr qsizetype ChunkBytes = 1024;
inline constexpr quint32 FrameMagic = 0x324c4443;
inline constexpr quint32 CommandMagic = 0x354c4443; // CDL5, little endian
inline constexpr int MaxAccumSeconds = 13; // 5 kHz * 13 fits the 16-bit accumulation register
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

inline quint32 word(const char *p) { return qFromLittleEndian<quint32>(p); }
inline double power(const char *p) { return static_cast<double>(std::bit_cast<float>(word(p))); }

// The same 8-byte command goes through UDP and the bridge unchanged. Zero cancels.
inline QByteArray command(quint32 seconds)
{
    QByteArray bytes(8, Qt::Uninitialized);
    qToLittleEndian(CommandMagic, bytes.data());
    qToLittleEndian(seconds, bytes.data() + 4);
    return bytes;
}
inline std::optional<quint32> decodeCommand(QByteArrayView bytes)
{
    if (bytes.size() != 8 || word(bytes.data()) != CommandMagic)
        return {};
    const auto seconds = word(bytes.data() + 4);
    if (seconds > MaxAccumSeconds) return {};
    return seconds;
}
}
