#pragma once

// P4 内容层：周口径日历工具（**零 Qt 依赖**）—— 周签到需要「本周」与「周内第几天」。
//
// 口径（与 docs/ROADMAP-P4.md §设计补充一致）：
//   * 一周从**周一**开始，dayIndex 0~6 = 周一~周日；
//   * weekKey = **本周周一的日期**，格式与 core::dayKey 一致（`年-月-日`，不补零）。
//     不用 ISO 周号（`2026-W40`）的原因：跨年周的归属易歧义，且本项目已有 dayKey 这一自然日口径，
//     周一日期与之同族、可直接字符串比较，无需额外的年份边界规则。
//
// 时间换算沿用 GrowthRules 的本地时区口径（`dayKey`）；换算时把时刻对齐到 12:00 再回退天数，
// 避免夏令时切换当天出现「减一天少/多 1 小时」导致的日期漂移。

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace whalepet::core {

inline std::tm localTimeOf(std::int64_t nowMs)
{
    const std::time_t t = static_cast<std::time_t>(nowMs / 1000);
    std::tm tmv{};
#if defined(_MSC_VER)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    return tmv;
}

inline std::string formatTm(const std::tm &tmv)
{
    char buf[32] = {0};
    std::snprintf(buf, sizeof(buf), "%d-%d-%d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return std::string(buf);
}

// 0 = 周一 … 6 = 周日
inline int dayIndexMondayFirst(std::int64_t nowMs)
{
    const std::tm tmv = localTimeOf(nowMs);
    // tm_wday: 0 = 周日；换算成「周一起」的索引
    return (tmv.tm_wday + 6) % 7;
}

// 把 nowMs 所在日期平移 dayOffset 天后的自然日 key
inline std::string dayKeyOffset(std::int64_t nowMs, int dayOffset)
{
    std::tm tmv = localTimeOf(nowMs);
    tmv.tm_mday += dayOffset;
    tmv.tm_hour = 12;
    tmv.tm_min = 0;
    tmv.tm_sec = 0;
    const std::time_t t = std::mktime(&tmv); // 归一化（mday 越界由 mktime 处理）
    if (t == static_cast<std::time_t>(-1)) {
        return formatTm(localTimeOf(nowMs));
    }
    std::tm out{};
#if defined(_MSC_VER)
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return formatTm(out);
}

// 本周周一日期（周签到的 weekKey）
inline std::string weekKey(std::int64_t nowMs)
{
    return dayKeyOffset(nowMs, -dayIndexMondayFirst(nowMs));
}

// 深夜间（22:00–06:00）判定：用于「深夜陪伴」成就
inline bool isLateNight(std::int64_t nowMs)
{
    const std::tm tmv = localTimeOf(nowMs);
    return tmv.tm_hour >= 22 || tmv.tm_hour < 6;
}

} // namespace whalepet::core
