#pragma once

// 天气类型判定（P8）：彩云天气 API 的中文天气描述 → 内部天气类型 → 立绘 / 台词槽位。
//
// 数据来源：彩云天气 `https://api.seniverse.com/v3/weather/now.json`，
// 响应中 `now.weather` 为**中文**描述（"晴" / "多云" / "阴" / "阵雨" / "雷阵雨" /
// "雨夹雪" / "小雪" / "雾" / "霾" / "浮尘" / "沙尘" / "晴间多云" …）。
// 因此判定按**子串优先级**进行，顺序即优先级（先雷后雪，先雪后雨）：
//
//   雷 > 雪 > 雨 > 雾/霾/尘 > 阴 > 云 > 晴 > 未知
//
// 隐私：仅把「用户填的城市 + key」发给彩云，不发送任何本机内容（docs/SETTINGS.md §4）。
// 零 Qt 依赖：可脱界面单测。

#include <cstddef>
#include <string>

namespace whalepet::core {

enum class WeatherKind {
    Unknown, // 空 / 无法识别 / 未配置（不联网）
    Sunny,
    Cloudy,
    Overcast,
    Rain,
    Snow,
    Fog,     // 雾 / 霾 / 浮尘 / 沙尘
    Thunder, // 含「雷」
    Hail     // 冰雹
};

// 天气类型的稳定 id（同时用作台词回答的 slot 名，见 PresetDialogue）
const char *weatherKindId(WeatherKind kind);
WeatherKind weatherKindFromId(const std::string &id);

// 彩云返回的中文天气描述 → 天气类型（子串优先级：雷 > 雪 > 雨 > 雾霾尘 > 阴 > 云 > 晴）
WeatherKind weatherKindFromCaiyun(const std::string &weatherZh);

// 天气类型 → 立绘。晴 / 多云 / 阴**没有专属天气资产**，复用 weather-rain-happy
// （好心情表现）；未知退回通用好奇表情。详见 docs/POSE-ASSETS.md §3。
const char *weatherKindPose(WeatherKind kind);

// 天气类型 + 月份 → 立绘（2026-10-04 立绘激活 22）：
//   盛夏（7/8/9 月）且为晴天 → daily-melt（热化）；其余月份 / 天气沿用 weatherKindPose。
// month 为自然月（1..12），非法值按「非盛夏」处理。
const char *weatherKindPoseForMonth(WeatherKind kind, int month);

} // namespace whalepet::core
