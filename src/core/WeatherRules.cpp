#include "core/WeatherRules.h"

namespace whalepet::core {

namespace {

bool contains(const std::string &haystack, const char *needle)
{
    return haystack.find(needle) != std::string::npos;
}

} // namespace

const char *weatherKindId(WeatherKind kind)
{
    switch (kind) {
    case WeatherKind::Unknown:
        return "unknown";
    case WeatherKind::Sunny:
        return "sunny";
    case WeatherKind::Cloudy:
        return "cloudy";
    case WeatherKind::Overcast:
        return "overcast";
    case WeatherKind::Rain:
        return "rain";
    case WeatherKind::Snow:
        return "snow";
    case WeatherKind::Fog:
        return "fog";
    case WeatherKind::Thunder:
        return "thunder";
    case WeatherKind::Hail:
        return "hail";
    }
    return "unknown";
}

WeatherKind weatherKindFromId(const std::string &id)
{
    for (int i = 0; i <= static_cast<int>(WeatherKind::Hail); ++i) {
        const WeatherKind kind = static_cast<WeatherKind>(i);
        if (id == weatherKindId(kind)) {
            return kind;
        }
    }
    return WeatherKind::Unknown;
}

WeatherKind weatherKindFromCaiyun(const std::string &weatherZh)
{
    // 判定顺序即优先级（不可调换）：
    //   雷阵雨必须先于雨；雨夹雪先判为雪；浮尘 / 沙尘 / 霾归入 Fog；
    //   「阴」先于「云」，避免「阴转多云」被误判为多云。
    if (weatherZh.empty()) {
        return WeatherKind::Unknown;
    }
    if (contains(weatherZh, "雷")) {
        return WeatherKind::Thunder;
    }
    if (contains(weatherZh, "雹")) {
        return WeatherKind::Hail;
    }
    if (contains(weatherZh, "雪")) {
        return WeatherKind::Snow;
    }
    if (contains(weatherZh, "雨")) {
        return WeatherKind::Rain;
    }
    if (contains(weatherZh, "雾") || contains(weatherZh, "霾") || contains(weatherZh, "尘")) {
        return WeatherKind::Fog;
    }
    if (contains(weatherZh, "阴")) {
        return WeatherKind::Overcast;
    }
    if (contains(weatherZh, "云")) {
        return WeatherKind::Cloudy;
    }
    if (contains(weatherZh, "晴")) {
        return WeatherKind::Sunny;
    }
    return WeatherKind::Unknown;
}

const char *weatherKindPose(WeatherKind kind)
{
    switch (kind) {
    case WeatherKind::Rain:
        return "weather-umbrella";
    case WeatherKind::Snow:
        return "weather-snow";
    case WeatherKind::Thunder:
        return "weather-thunder";
    case WeatherKind::Fog:
    case WeatherKind::Hail:
        return "weather-cold";
    case WeatherKind::Sunny:
    case WeatherKind::Cloudy:
    case WeatherKind::Overcast:
        // 无专属天气资产：复用「好心情」那张（见 WeatherRules.h 注释）
        return "weather-rain-happy";
    case WeatherKind::Unknown:
        // 未配置 / 请求失败 / 无法识别：不硬聊天气，退回通用好奇表情
        return "curious";
    }
    return "curious";
}

const char *weatherKindPoseForMonth(WeatherKind kind, int month)
{
    // 盛夏（7/8/9 月）晴天 → 热化立绘 daily-melt（2026-10-04 立绘激活 22）
    if (kind == WeatherKind::Sunny && month >= 7 && month <= 9) {
        return "daily-melt";
    }
    return weatherKindPose(kind);
}

} // namespace whalepet::core
