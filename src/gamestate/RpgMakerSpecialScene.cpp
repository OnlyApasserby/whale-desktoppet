#include "gamestate/RpgMakerSpecialScene.h"

#include <QJsonArray>
#include <QString>

namespace whalepet::gamestate {

using core::GameSpecialScene;

namespace {

bool sceneNameMatches(const QString &name, const std::vector<std::string> &names)
{
    if (name.isEmpty()) {
        return false;
    }
    for (const std::string &candidate : names) {
        if (name == QString::fromStdString(candidate)) {
            return true;
        }
    }
    return false;
}

// 单张图片的覆盖率： (bw*sx/100) * (bh*sy/100) / (sw*sh)
double pictureCoverRatio(const QJsonObject &pic, double screenW, double screenH)
{
    if (screenW <= 0.0 || screenH <= 0.0) {
        return 0.0;
    }
    const double bw = pic.value(QStringLiteral("bw")).toDouble(0.0);
    const double bh = pic.value(QStringLiteral("bh")).toDouble(0.0);
    const double sx = pic.value(QStringLiteral("sx")).toDouble(100.0) / 100.0;
    const double sy = pic.value(QStringLiteral("sy")).toDouble(100.0) / 100.0;
    if (bw <= 0.0 || bh <= 0.0) {
        return 0.0;
    }
    return (bw * sx) * (bh * sy) / (screenW * screenH);
}

} // namespace

RpgMakerSpecialSceneDetector::RpgMakerSpecialSceneDetector(RpgMakerSpecialSceneConfig config)
    : m_config(std::move(config))
{
}

GameSpecialScene RpgMakerSpecialSceneDetector::classify(const QJsonObject &probe,
                                                       const RpgMakerSpecialSceneConfig &config)
{
    if (probe.value(QStringLiteral("video")).toBool(false)) {
        return GameSpecialScene::Video;
    }

    const QString sceneName = probe.value(QStringLiteral("scene")).toString();
    if (sceneNameMatches(sceneName, config.sceneNames)) {
        return GameSpecialScene::Scene;
    }

    const double screenW = probe.value(QStringLiteral("sw")).toDouble(0.0);
    const double screenH = probe.value(QStringLiteral("sh")).toDouble(0.0);
    const QJsonArray pics = probe.value(QStringLiteral("pics")).toArray();
    for (const QJsonValue &item : pics) {
        const QJsonObject pic = item.toObject();
        if (pic.value(QStringLiteral("op")).toDouble(0.0) < config.minOpacity) {
            continue;
        }
        if (pictureCoverRatio(pic, screenW, screenH) >= config.coverRatio) {
            return GameSpecialScene::Picture;
        }
    }

    // 对话演出（Face / Bust）：消息窗忙且有立绘名。
    if (probe.value(QStringLiteral("msg")).toBool(false)
        && !probe.value(QStringLiteral("face")).toString().isEmpty()) {
        return GameSpecialScene::Dialogue;
    }

    return GameSpecialScene::None;
}

GameSpecialScene RpgMakerSpecialSceneDetector::update(const QJsonObject &probe)
{
    const GameSpecialScene raw = classify(probe, m_config);
    if (raw == m_current) {
        m_candidate = raw;
        m_candidateFrames = 0;
        return m_current;
    }
    if (raw == m_candidate) {
        ++m_candidateFrames;
    } else {
        m_candidate = raw;
        m_candidateFrames = 1;
    }
    const int need = (raw == GameSpecialScene::None) ? m_config.exitFrames : m_config.enterFrames;
    if (m_candidateFrames >= need) {
        m_current = raw;
        m_candidateFrames = 0;
    }
    return m_current;
}

void RpgMakerSpecialSceneDetector::reset()
{
    m_current = GameSpecialScene::None;
    m_candidate = GameSpecialScene::None;
    m_candidateFrames = 0;
}

} // namespace whalepet::gamestate
