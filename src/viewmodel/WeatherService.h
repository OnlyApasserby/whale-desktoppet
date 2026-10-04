#pragma once

// WeatherService（P8）：彩云天气 API 接入 —— 只做「天气类型判定」，不做任何 UI。
//
// 归属：docs/SETTINGS.md §4、docs/DIALOGUE.md §4。
//
// 接口（彩云天气 v3，个人免费 key）：
//   GET https://api.seniverse.com/v3/weather/now.json
//       ?key=<key>&location=<城市名或经纬度>&language=zh-Hans&unit=c
//   响应：{"location":{"name":"上海"},"now":{"weather":"多云","temperature":"25"}}
//   判定：core::weatherKindFromCaiyun(now.weather)（docs 见 core/WeatherRules.h）。
//
// 隐私与降级（硬约束）：
//   * **key 或 location 为空 = 完全不联网**（不发任何请求，kind 恒 Unknown）；
//   * 只向彩云发送「key + 城市名 / 经纬度」，不发送任何本机内容；
//   * 失败静默退避（60 分钟），绝不弹窗、绝不影响桌宠本体与工作态。
//
// 缓存口径：成功结果 30 分钟内视为新鲜（stale() 为假），期间 refresh() 直接返回。

#include "core/WeatherRules.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace whalepet::viewmodel {

class WeatherService : public QObject {
    Q_OBJECT
public:
    static constexpr int kCacheTtlMs = 30 * 60 * 1000;     // 成功结果新鲜期
    static constexpr int kRetryBackoffMs = 60 * 60 * 1000; // 失败退避
    static constexpr int kRequestTimeoutMs = 10000;        // 单次请求超时

    explicit WeatherService(QObject *parent = nullptr);
    ~WeatherService() override;

    // 配置（空 key 或 空 location → 禁用，不联网）
    void setConfig(const QString &key, const QString &location);
    QString key() const { return m_key; }
    QString location() const { return m_location; }
    bool configured() const;

    // 请求最新天气（未配置 / 退避中 / 已有在途请求 → 直接返回；不排队）
    void refresh();
    bool refreshing() const { return m_reply != nullptr; }

    // 最近一次**成功**判定的结果
    bool hasResult() const { return m_hasResult; }
    core::WeatherKind kind() const { return m_kind; }
    QString description() const { return m_description; } // 彩云原始中文（诊断 / 面板展示）
    QString placeName() const { return m_placeName; }
    qint64 lastUpdatedMs() const { return m_updatedAtMs; }

    // 结果是否已过期（过期后仍可用，由调用方决定是否硬聊天气）
    bool stale() const;

    QString lastError() const { return m_lastError; }
    // 距离下次允许请求的剩余毫秒（0 = 现在就可请求）；供诊断与单测
    qint64 backoffRemainingMs() const;

signals:
    void updated(core::WeatherKind kind, const QString &description);
    void failed(const QString &error);

private:
    void abortInFlight();
    void onReplyFinished(QNetworkReply *reply);

    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_reply = nullptr;

    QString m_key;
    QString m_location;

    bool m_hasResult = false;
    core::WeatherKind m_kind = core::WeatherKind::Unknown;
    QString m_description;
    QString m_placeName;
    qint64 m_updatedAtMs = 0;

    qint64 m_blockedUntilMs = 0; // 失败退避截止时刻
    QString m_lastError;
};

} // namespace whalepet::viewmodel
