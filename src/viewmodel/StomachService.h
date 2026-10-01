#pragma once

// StomachService：「胃袋」——拖拽投喂的落盘与定时清空。
//
// 职责（与桌宠界面解耦，仅依赖 Qt6::Core，可 headless 单测）：
//   - 路径：安装目录下的 stomach 文件夹（<applicationDirPath>/stomach），
//     与 DATA-MODEL 的 data/ 平级，但**独立于数据库**。
//   - 入胃 ingest()：把拖入的文件/文件夹移动到 stomach/。同盘 rename（零拷贝），
//     跨盘/失败回退为「递归复制 + 删除」；重名自动追加 " (n)"，绝不覆盖。
//   - 清空 emptyToTrash()：stomach/ 内所有顶层条目（文件与文件夹）移入系统回收站。
//   - 周期检查 start()：默认每 kCheckIntervalMs（5 分钟）执行一次 emptyToTrash()。
//
// 说明：需求要求「安装目录下的 stomach」为唯一落点，故不做用户目录降级；
// 安装目录不可写时仅告警（ensureStomachDir 返回 false），不静默改写别处。

#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

namespace whalepet::viewmodel {

class StomachService : public QObject {
    Q_OBJECT
public:
    explicit StomachService(QObject *parent = nullptr);
    ~StomachService() override;

    // 安装目录下的 stomach 文件夹绝对路径（不保证已存在）
    QString stomachPath() const;

    // 确保 stomach 目录存在；返回是否可用
    bool ensureStomachDir();

    // 把一批本地路径（文件/文件夹）移入 stomach；返回成功入胃的条目数
    int ingest(const QStringList &paths);

    // 把 stomach 内所有顶层条目移入系统回收站；返回成功清空的条目数
    int emptyToTrash();

    // 周期检查（默认 5 分钟）；intervalMs 仅供测试注入
    void start(int intervalMs = kCheckIntervalMs);
    void stop();
    bool running() const;

    // 检查间隔：需求固定为 5 分钟
    static constexpr int kCheckIntervalMs = 5 * 60 * 1000;

signals:
    void ingested(int count); // 成功入胃的条目数
    void trashed(int count);  // 成功移入回收站的条目数

private:
    int trashEntries(const QStringList &paths);

    QTimer *m_timer = nullptr;
    QString m_stomachPath;
};

} // namespace whalepet::viewmodel
