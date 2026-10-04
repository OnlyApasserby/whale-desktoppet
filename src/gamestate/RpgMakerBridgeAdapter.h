#pragma once

// RPG Maker RGSS（及 MV/MZ 未开调试端口时的回退）桥接适配器。
// 归属：docs/ROADMAP-ex1.md §2.6.3（方案 B 主路径 / 方案 A 回退）。
// 【红线】不注入、不 hook：只读取**用户自备的只读脚本**写出的状态快照
//         （本地文件 or 回环 socket），脚本侧自行保证只读。
// 快照为 JSON：{ "available": true, "hp": 123, "gold": 456, "level": 7,
//               "posX": 12.5, "mapName": "Fort", "specialScene": 0 }

#include "gamestate/GameProfile.h"
#include "gamestate/IGameStateAdapter.h"
#include "gamestate/RpgMakerSpecialScene.h"

#include <QJsonObject>
#include <QString>

#include <functional>

namespace whalepet::gamestate {

// 桥接快照的**资源上限**（SECURITY-REVIEW.md §极端边界测试建议 3）：
// 桥接数据由**用户自备脚本**产出，属不可信输入，故大小与总读取时长都必须封顶——
// 否则一个写坏（或恶意）的脚本就能让桌宠无限累积内存或永久阻塞在 socket 读上。
inline constexpr qint64 kBridgeMaxSnapshotBytes = 1024 * 1024; // 单份快照上限（1 MiB）
inline constexpr int kBridgeSocketReadTimeoutMs = 1500;         // 单次 socket 读等待上限
inline constexpr int kBridgeSocketTotalTimeoutMs = 4000;       // 一次快照的**总**读取时长上限

class RpgMakerBridgeAdapter final : public IGameStateAdapter {
public:
    // 注入式快照读取（测试/自定义通道）；默认按 profile.bridge 的 kind 选择 file/socket。
    using SnapshotProvider = std::function<bool(QString *json, QString *error)>;

    RpgMakerBridgeAdapter();
    ~RpgMakerBridgeAdapter() override;

    static bool engineMatches(const std::string &engine);
    static bool supports(const GameProfile &profile); // 含有效 bridge 配置

    bool attach(const GameProfile &profile, QString *error) override;
    void detach() override;
    bool attached() const override;
    bool read(core::GameSample *out, QString *error) override;

    // 测试用：覆盖快照来源（attach 后设置也生效）。
    void setSnapshotProvider(SnapshotProvider provider);

    RpgMakerSpecialSceneDetector &specialSceneDetector() { return m_sceneDetector; }

private:
    bool readSnapshot(QString *json, QString *error) const;

    GameProfile m_profile;
    bool m_attached = false;
    SnapshotProvider m_provider;
    RpgMakerSpecialSceneDetector m_sceneDetector;
    bool m_applyProbe = false; // 快照含 "probe" 时启用滞回检测
};

} // namespace whalepet::gamestate
