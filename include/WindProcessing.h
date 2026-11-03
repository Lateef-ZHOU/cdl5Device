#pragma once
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
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

using ReferenceMHz = std::array<std::optional<double>, 3>;
inline ReferenceMHz readReferenceMHz()
{
    QSettings settings(QDir(QCoreApplication::applicationDirPath()).filePath("cdl5-wind.ini"), QSettings::IniFormat);
    ReferenceMHz references{};
    for (int channel = 0; channel < 3; ++channel) {
        bool ok = false;
        const double f = settings.value(QString("wind/ADC%1").arg(channel)).toDouble(&ok);
        if (ok && std::isfinite(f) && f > 0.0) references[channel] = f;
    }
    return references;
}

// 500 MSPS、512 点 FFT，波长 1550 nm。频移向上取正，与交付上位机的径向风换算一致。
inline constexpr double BinMHz = 500.0 / 512.0;
inline std::pair<RadialWind::Result, bool> radial(const QVector<double> &spectrum, int rows, int columns,
                                int firstBin, std::optional<double> reference)
{
    constexpr double metersPerSecondPerMHz = 0.775;
    const double low = firstBin * BinMHz, high = (firstBin + columns - 1) * BinMHz;
    const bool calibrated = reference && *reference >= low && *reference <= high;
    const double offset = calibrated ? (low - *reference) * metersPerSecondPerMHz
                                     : std::numeric_limits<double>::quiet_NaN();
    return {RadialWind::Calc(spectrum, rows, columns, BinMHz * metersPerSecondPerMHz, offset), calibrated};
}
} // namespace Cdl5
