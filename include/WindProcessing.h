#pragma once
#include <limits>
#include <utility>
#include <RadialWind.h>
#include <WindProfile.h>
#include "Collector.h"

namespace Cdl5 {
// 三个 Profile 均以每门起点为斜距；P7/P8 的 range_mode=1 使用统一 60 m。
inline QVector<double> gateRanges(int profile, bool uniform, int rows)
{
    const auto config = configuration(profile);
    if (!config || rows != config->rows || (uniform && !config->supportsUniform)) return {};
    QVector<double> ranges(rows);
    for (int i = 0; i < rows; ++i)
        ranges[i] = uniform ? i * 60 : i < 200 ? i * 15
                  : i < 350 ? 3000 + (i - 200) * 60 : 12000 + (i - 350) * 150;
    return ranges;
}

inline QVector<double> channelRanges(Configuration config, int channel, bool uniform)
{
    if (!config.adc0Multires()) return gateRanges(config.profile, uniform, config.rows);
    QVector<double> ranges(config.rows);
    const int step = config.rangeChannels[channel].spacingM;
    for (int gate = 0; gate < config.rows; ++gate)
        ranges[gate] = gate * step;
    return ranges;
}

inline QString channelTag(Configuration config, int channel)
{
    if (config.adc0Multires())
        return QStringLiteral("ADC0_%1m").arg(config.rangeChannels[channel].spacingM);
    return QStringLiteral("ADC%1").arg(channel);
}

// 500 MSPS、512 点 FFT，波长 1550 nm。频移向上取正，与交付上位机的径向风换算一致。
inline constexpr int SampleRateHz = 500000000;
inline constexpr int FftLength = 512;
inline constexpr double BinMHz = SampleRateHz / 1000000.0 / FftLength;
// 参考频率落在本帧频带内才换算风速，否则风速为 NaN、CNR 照常；第二项表示是否已换算。
inline std::pair<RadialWind::Result, bool> radial(const QVector<double> &spectrum, int rows, int columns,
                                int firstBin, double referenceMHz)
{
    constexpr double metersPerSecondPerMHz = 0.775;
    const double low = firstBin * BinMHz, high = (firstBin + columns - 1) * BinMHz;
    const bool calibrated = referenceMHz >= low && referenceMHz <= high;
    const double offset = calibrated ? (low - referenceMHz) * metersPerSecondPerMHz
                                     : std::numeric_limits<double>::quiet_NaN();
    return {RadialWind::Calc(spectrum, rows, columns, BinMHz * metersPerSecondPerMHz, offset), calibrated};
}
} // namespace Cdl5
