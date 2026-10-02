#pragma once

// 能力协议（capability protocol）：三层插件（内置 / DLL / 外部进程）**共用**的唯一接口。
//
// 设计来源：src/minigame/MiniGamePlugin.h 已验证的「元数据 + 宿主依赖注入 + 注册表 +
// 宿主零分支」模式（见 docs/MINIGAME-INTERFACE.md §2）。本文件把它抽象为与具体能力无关的协议，
// 使新增能力（感知 / 工作状态 / Context API / 第三方扩展）无需改动宿主与传输通道。
//
// 约定：
//   * 能力只按 id 被发现与调用，宿主与传输通道**不感知**插件来源层；
//   * **禁止异常穿越能力边界**：失败一律填 error（code / message）；
//   * 同步实现返回 true 并填充 out；异步实现返回 false 并接管 ctx 的 responder
//     （用 InvokeContext::takeResponder() 取走回调，完成后调用它）。
//
// 本文件不包含任何 Qt Widgets 头，可在 headless 单测中使用。

#include <QJsonObject>
#include <QList>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace whalepet::plugin {

// 插件来源层：决定同名能力的仲裁优先级（Builtin > Dll > Process），并写入诊断信息
enum class PluginOrigin { Builtin, Dll, Process };

// 数值越小优先级越高：Builtin(0) > Dll(1) > Process(2)
int pluginOriginPriority(PluginOrigin origin);
const char *pluginOriginId(PluginOrigin origin);

// ---------------------------------------------------------------------------
// JSON-RPC 2.0 标准错误码 + 本实现扩展码（禁止在别处再写字面量，见 docs/CONTEXT-API.md §3.1）
// ---------------------------------------------------------------------------
inline constexpr int kRpcErrorParseError = -32700;
inline constexpr int kRpcErrorInvalidRequest = -32600;
inline constexpr int kRpcErrorMethodNotFound = -32601;
inline constexpr int kRpcErrorInvalidParams = -32602;
inline constexpr int kRpcErrorInternal = -32603;
inline constexpr int kRpcErrorCapabilityFailed = -32001;      // 能力内部失败
inline constexpr int kRpcErrorCapabilityUnavailable = -32002; // 能力不可用（如外部进程已退出）
inline constexpr int kRpcErrorUnauthorized = -32003;          // 未授权（token 缺失或不匹配）

// 统一错误对象：{ code, message, data }；data 可为空对象
QJsonObject makeRpcError(int code, const QString &message, const QJsonObject &data = QJsonObject());

// 从错误对象中取字段（缺失时给出可读兜底，避免把空串写进协议）
int rpcErrorCode(const QJsonObject &error);
QString rpcErrorMessage(const QJsonObject &error);
QJsonObject rpcErrorData(const QJsonObject &error);

// ---------------------------------------------------------------------------
// InvokeContext：一次能力调用的上下文
//
// 同步能力可忽略本对象；异步能力必须 takeResponder() 取走回调并妥善保存，
// 完成后调用 `cb(response)`（response 形状见 respond/emit 的说明）。
// ---------------------------------------------------------------------------
class InvokeContext {
public:
    // response 形状：{"ok": true, "result": <object>} 或 {"ok": false, "error": <error object>}
    using Responder = std::function<void(const QJsonObject &)>;

    explicit InvokeContext(Responder responder = Responder());
    ~InvokeContext() = default;

    InvokeContext(const InvokeContext &) = delete;
    InvokeContext &operator=(const InvokeContext &) = delete;

    void setResponder(Responder responder);
    // 取走回调（异步能力用）。取走后本对象不再持有回调；answered() 仍如实反映是否已作答。
    Responder takeResponder();
    // 是否仍持有回调：分发方据此区分「异步已受理」与「能力违反契约（既不答复也不取走回调）」
    bool hasResponder() const { return static_cast<bool>(m_responder); }

    // 同步作答：成功 / 失败。二者都只接受第一次调用（后续调用被忽略，避免重复响应）。
    void respond(const QJsonObject &result);
    void fail(int code, const QString &message, const QJsonObject &data = QJsonObject());

    bool answered() const { return m_answered; }
    const QJsonObject &response() const { return m_response; }

private:
    // 注意：不能命名为 emit —— `emit` 是 Qt 的关键字宏（展开为空），
    // 用作成员函数名会让 `InvokeContext::emit(...)` 退化成非法的 `InvokeContext::(...)`。
    void deliver(QJsonObject response);

    Responder m_responder;
    QJsonObject m_response;
    bool m_answered = false;
};

// ---------------------------------------------------------------------------
// CapabilityDescriptor：能力元数据（驱动能力清单展示、菜单与参数提示）
// ---------------------------------------------------------------------------
struct CapabilityDescriptor {
    QString id;                  // 稳定标识："minigame.minesweeper" / "context.snapshot" / "ext.foo.bar"
    QString version = QStringLiteral("1.0"); // 语义化版本（协商用）
    QString displayName;
    QString description;
    PluginOrigin origin = PluginOrigin::Builtin;
    bool readOnly = true;        // context.* 只读；工具类可写
    QString paramsSchema;        // 轻量参数描述（JSON 字符串）；本期不做完整 JSON Schema

    bool isValid() const { return !id.isEmpty(); }
};

// ---------------------------------------------------------------------------
// ICapability：能力的最小接口
// ---------------------------------------------------------------------------
class ICapability {
public:
    virtual ~ICapability() = default;

    virtual CapabilityDescriptor descriptor() const = 0;

    // 返回 true：同步完成，out 已填充。
    // 返回 false：已受理（异步），结果经 ctx 的 responder 回投。
    // 失败：填 error（含 code / message）——同步路径返回 true 并填 error 同样合法。
    virtual bool invoke(const QJsonObject &in, InvokeContext &ctx, QJsonObject &out,
                        QJsonObject &error) = 0;
};

// 便捷基类：同步能力只需实现 call()（成功填 out 返回 true；失败填 error 返回 false）
class SimpleCapability : public ICapability {
public:
    explicit SimpleCapability(CapabilityDescriptor descriptor);

    CapabilityDescriptor descriptor() const override;
    bool invoke(const QJsonObject &in, InvokeContext &ctx, QJsonObject &out,
                QJsonObject &error) override;

protected:
    virtual bool call(const QJsonObject &in, QJsonObject &out, QJsonObject &error) = 0;

private:
    CapabilityDescriptor m_descriptor;
};

// ---------------------------------------------------------------------------
// CapabilityRegistry：能力注册表（三层插件共用；宿主与通道只按 id 查找）
// ---------------------------------------------------------------------------
class CapabilityRegistry {
public:
    CapabilityRegistry() = default;
    ~CapabilityRegistry() = default;

    CapabilityRegistry(const CapabilityRegistry &) = delete;
    CapabilityRegistry &operator=(const CapabilityRegistry &) = delete;

    // 注册能力（接管所有权）。
    //   - id 为空 / 空指针 → 忽略并返回 false；
    //   - 同 id 冲突 → 按 Builtin > Dll > Process 保留优先级更高者，被丢弃者记日志（不静默）；
    //   - 返回 true 表示能力已可被调用。
    bool add(std::unique_ptr<ICapability> capability);

    int count() const;
    bool contains(const QString &id) const;
    ICapability *find(const QString &id) const;

    // 能力清单（按 id 升序，输出稳定，便于测试与展示）
    QList<CapabilityDescriptor> descriptors() const;

    // 可用性开关：外部进程插件退出 / 动态库卸载时置 false。
    // 能力仍留在表中（清单可见），但调用返回 kRpcErrorCapabilityUnavailable。
    void setAvailable(const QString &id, bool available);
    bool isAvailable(const QString &id) const;

    // 统一调用入口。
    //   返回 true：已完成（成功见 out，失败见 error）。
    //   返回 false：已受理（异步），结果经 ctx 的 responder 回投。
    bool invoke(const QString &id, const QJsonObject &params, InvokeContext &ctx, QJsonObject &out,
                QJsonObject &error);

private:
    struct Entry {
        std::unique_ptr<ICapability> capability;
        bool available = true;
    };

    Entry *findEntry(const QString &id);

    std::vector<Entry> m_entries; // 保持注册顺序（清单输出前另做排序）
};

} // namespace whalepet::plugin
