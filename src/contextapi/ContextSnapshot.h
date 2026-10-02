#pragma once

// 本地上下文数据模型（docs/CONTEXT-API.md §2）。
//
// 约定：
//   * 纯数据结构 + JSON 投影，无业务逻辑、无 Qt Widgets 依赖；
//   * 字段缺失一律留空 / 0，并配 `available` 标记 —— **不伪造数据**；
//   * JSON 形状即对外协议，改动需同步 docs/CONTEXT-API.md。

#include "core/WorkState.h"

#include <QJsonObject>
#include <QString>

#include <cstdint>

namespace whalepet::contextapi {

// 对外协议版本（与 docs/CONTEXT-API.md 同步）
inline constexpr const char *kContextApiVersion = "1.0";

struct ContextSnapshot {
    QString apiVersion = QStringLiteral("1.0");
    QString appVersion;
    qint64 generatedAtMs = 0;

    // ---- 桌面环境（platform 采样；envAvailable == false 表示无数据）----
    bool envAvailable = false;
    QString appId;
    QString windowTitle;
    core::AppCategory category = core::AppCategory::Unknown;
    qint64 idleMs = 0;
    int inputEvents = 0;
    int appSwitches = 0;
    qint64 dwellMs = 0;
    bool systemPaused = false;

    // ---- 工作状态 ----
    core::WorkState workState = core::WorkState::Unknown;
    double workConfidence = 0.0;
    qint64 workSinceMs = 0;

    // ---- 桌宠养成状态（petAvailable == false 表示养成服务未接入）----
    bool petAvailable = false;
    int level = 0;
    int exp = 0;
    int mood = 0;
    int affinity = 0;
    int satiety = 0;
    int bondLevel = 0;
    qint64 companionMs = 0;

    // ---- 会话统计 ----
    qint64 startedAtMs = 0;
    qint64 samples = 0;
    qint64 interactions = 0;
    qint64 workStateChanges = 0;

    // nowMs <= 0 时以 generatedAtMs 为基准
    qint64 uptimeMs(qint64 nowMs = 0) const;

    // 分组投影（与 CONTEXT-API.md §3 的方法一一对应）
    QJsonObject toJson(qint64 nowMs = 0) const;
    QJsonObject envJson() const;
    QJsonObject workJson() const;
    QJsonObject petJson() const;
    QJsonObject sessionJson(qint64 nowMs = 0) const;
};

} // namespace whalepet::contextapi
