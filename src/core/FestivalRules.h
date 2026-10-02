#pragma once

// 节日换装规则：**零 Qt 依赖**、纯函数、可脱界面单测。
// 移植来源：`referances/dsh-whale-musume/assets/whale-moe-core.js` 的 `festivalKey()`。
//
// 覆盖范围（**5 个**，与参考项目一致；立绘均已存在于 assets/poses，**不新增美术资源**）：
//   | 节日 | 日期口径 | pose key | 资源 |
//   |---|---|---|---|
//   | 春节 | 农历新年（表驱动，逐年补充） | `festival-spring` | dsh-whale-state-festival-spring.webp |
//   | 中秋 | 农历八月十五（表驱动，逐年补充） | `festival-mid-autumn` | dsh-whale-state-festival-mid-autumn.webp |
//   | 万圣节 | 公历 10-31 | `festival-halloween` | dsh-whale-state-festival-halloween.webp |
//   | 圣诞节 | 公历 12-25 | `festival-christmas` | dsh-whale-state-festival-christmas.webp |
//   | 情人节 | 公历 02-14 | `valentine`（资产名**不带** festival- 前缀） | dsh-whale-state-valentine.webp |
//
// 口径说明：
//   * 以**本地时区**的自然日为准（与 `core::dayKey` 同族，见 Calendar.h）；
//   * 农历节日无内置换算，沿用参考项目的**小表**（`kFestivalDays`）：表内没有的年份
//     只是「当年不换装」，不会误判为其它节日；需要时按同一格式补一行即可。
//
// 消费方：`PetStateMachine::contextPose()` —— **只在静息态**换装（见 docs/STATE-MACHINE.md §5.1）。

#include "core/Calendar.h"

#include <cstdio>
#include <ctime>
#include <string>

namespace whalepet::core {

// 农历节日的公历日期表（key 为补零的 `年-月-日`，与参考项目 festivalKey 表逐条一致）。
struct FestivalEntry {
    const char *dateKey;
    const char *pose;
};

inline constexpr FestivalEntry kFestivalDays[] = {
    {"2026-02-17", "festival-spring"},
    {"2027-02-06", "festival-spring"},
    {"2026-09-25", "festival-mid-autumn"},
    {"2027-09-15", "festival-mid-autumn"},
};

// 本地自然日 key（补零）：`2026-12-25`
inline std::string festivalDateKey(std::int64_t nowMs)
{
    const std::tm tmv = localTimeOf(nowMs);
    char buf[16] = {0};
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return std::string(buf);
}

// 返回**当日**（本地时区）对应的节日立绘 pose key；非节日返回 nullptr。
inline const char *festivalPoseOf(std::int64_t nowMs)
{
    const std::tm tmv = localTimeOf(nowMs);
    const int month = tmv.tm_mon + 1;
    const int day = tmv.tm_mday;

    const std::string key = festivalDateKey(nowMs);
    for (const FestivalEntry &entry : kFestivalDays) {
        if (key == entry.dateKey) {
            return entry.pose;
        }
    }
    if (month == 10 && day == 31) {
        return "festival-halloween";
    }
    if (month == 12 && day == 25) {
        return "festival-christmas";
    }
    if (month == 2 && day == 14) {
        return "valentine";
    }
    return nullptr;
}

} // namespace whalepet::core
