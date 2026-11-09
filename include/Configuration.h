#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace Cdl5 {
// Fixed FPGA contracts; no connection or editable UI state is stored here.
struct RangeChannel {
    int spacingM;
    int validSamples;
};

struct Configuration {
    int profile;
    int rateHz;
    int rows;
    int columns;
    int lanes;
    int channels;
    int rangeMaximum;
    bool supportsUniform;
    std::array<RangeChannel, 3> rangeChannels{};

    constexpr bool adc0Multires() const { return rangeChannels.front().spacingM > 0; }

    constexpr int maxAccumSeconds() const { return 65535 / rateHz; }
    constexpr std::uint32_t frameFormat() const
    { return 0x05040000u | (std::uint32_t(lanes) << 8) | ((1u << channels) - 1); }
};
inline constexpr Configuration FourK{9, 4000, 500, 128, 5, 3, 34500, false};
inline constexpr Configuration FiveK{8, 5000, 450, 128, 5, 3, 27000, true};
inline constexpr Configuration TenK{7, 10000, 240, 240, 6, 3, 14400, true};
// Desktop selection for PL 0x26110401. Its wire header is shared with the legacy TenK profile.
inline constexpr Configuration TenKAdc0{7, 10000, 240, 240, 6, 3, 7200, false,
                                         {{{6, 20}, {15, 50}, {30, 100}}}};
inline constexpr std::array Configurations{FourK, FiveK, TenK};
// 零风速参考频率（MHz），所有规格与通道共用；超出本帧频带时该通道只出 CNR、风速缺测。
inline constexpr double ZeroWindReferenceMHz = 80.0;

constexpr std::optional<Configuration> configuration(int profile)
{
    for (const auto &config : Configurations)
        if (config.profile == profile) return config;
    return {};
}
} // namespace Cdl5
