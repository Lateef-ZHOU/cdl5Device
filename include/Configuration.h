#pragma once
#include <array>
#include <cstdint>

namespace Cdl5 {
// PL 0x26110401 的固定契约（Profile 7、10 kHz）：ADC0 按 6/15/30 m 三种门宽输出三路，各 240 门 × 240 频点；
// 每门以起点为斜距、等距排列。这里不存放连接或可编辑界面状态。
inline constexpr int Profile = 7;
inline constexpr int RateHz = 10000;
inline constexpr int Rows = 240;
inline constexpr int Columns = 240;
inline constexpr int Lanes = 6;
inline constexpr int Channels = 3;
inline constexpr double RangeStartM = 0.0; // 第 0 门斜距；各路第 k 门 = RangeStartM + k·门宽
inline constexpr std::array<int, Channels> GateSpacingM{6, 15, 30};
// FPGA 累积计数寄存器 16 位：65535 次触发折合的整秒数。
inline constexpr int MaxAccumSeconds = 65535 / RateHz;
inline constexpr std::uint32_t FrameFormat = 0x05040000u | (std::uint32_t(Lanes) << 8) | ((1u << Channels) - 1);
// 零风速参考频率（MHz），三路共用；超出本帧频带时该通道只出 CNR、风速缺测。
inline constexpr double ZeroWindReferenceMHz = 80.0;
} // namespace Cdl5
