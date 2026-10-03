#pragma once

// RPG Maker MV/MZ 特殊场景（CG）检测：把 CDP 只读探测表达式的结果 JSON（§2.6.2.1）
// 归类为 GameSpecialScene，并按「连续 N 帧成立才置位 / 连续 M 帧不成立才复位」抗抖。
// 归属：docs/ROADMAP-ex1.md §2.6.2.1。
// 【只读 / 纯逻辑】本文件不接触进程；输入是已求值得到的 JSON 快照。

#include "core/GameState.h"

#include <QJsonObject>

#include <string>
#include <vector>

namespace whalepet::gamestate {

// 探测 JSON 约定（由内置/自定义只读表达式产出）：
//   { "scene": "Scene_Map", "video": false, "msg": false, "face": "",
//     "sw": 816, "sh": 624,
//     "pics": [ { "id":1, "n":"cg1", "op":255, "sx":100, "sy":100,
//                 "x":0, "y":0, "o":0, "bw":816, "bh":624 } ] }
// 其中 sw/sh 为屏幕像素尺寸；pics[].bw/bh 为该图片位图尺寸（用于覆盖率估算）。
struct RpgMakerSpecialSceneConfig {
    std::vector<std::string> sceneNames;     // 专用场景名单（如 Scene_CG / Scene_Gallery）
    double coverRatio = 0.6;                 // 全屏图片覆盖阈值
    double minOpacity = 200.0;               // 视为「不透明图片」的最低透明度
    int enterFrames = core::kGameSpecialSceneEnterFrames; // 连续 N 帧成立才置位
    int exitFrames = core::kGameSpecialSceneExitFrames;   // 连续 M 帧不成立才复位
};

class RpgMakerSpecialSceneDetector {
public:
    explicit RpgMakerSpecialSceneDetector(RpgMakerSpecialSceneConfig config = {});

    // 每帧调用：更新滞回状态并返回**当前生效**的特殊场景。
    core::GameSpecialScene update(const QJsonObject &probe);

    void reset();
    core::GameSpecialScene current() const { return m_current; }
    const RpgMakerSpecialSceneConfig &config() const { return m_config; }

    // 纯判据（不含滞回），便于逐条单测。
    // 优先级：影片 > 专用场景名单 > 全屏图片 > 对话演出 > 无。
    static core::GameSpecialScene classify(const QJsonObject &probe,
                                           const RpgMakerSpecialSceneConfig &config);

private:
    RpgMakerSpecialSceneConfig m_config;
    core::GameSpecialScene m_current = core::GameSpecialScene::None;
    core::GameSpecialScene m_candidate = core::GameSpecialScene::None;
    int m_candidateFrames = 0;
};

} // namespace whalepet::gamestate
