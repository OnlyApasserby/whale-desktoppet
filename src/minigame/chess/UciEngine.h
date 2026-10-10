#pragma once

// UCI 引擎封装：用 QProcess 启动**外部**国际象棋引擎（如 Stockfish），
// 通过 UCI 协议进行通信。
//
// 设计边界（与需求一致）：
//   * 本项目**不自带棋力**，只做「协议通道 + 进程生命周期」；引擎文件由用户自行准备
//     （放置目录见 README「国际象棋引擎」一节）。缺失 / 启动失败时优雅降级为「引擎不可用」，
//     不崩溃、不静默。
//   * 全部 I/O 在 Qt 事件循环内**异步**完成：调用方连接 ready() / bestMove() / failed() 信号，
//     不得阻塞等待（避免界面卡死）。
//   * 输出按行解析，只关心 uciok / readyok / bestmove，其余行经 logLine() 透出便于排查。

#include "core/Chess.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class QProcess;

namespace whalepet {

class UciEngine : public QObject {
    Q_OBJECT
public:
    explicit UciEngine(QObject *parent = nullptr);
    ~UciEngine() override;

    // 启动引擎并开始 UCI 握手（uci → uciok → isready → readyok）。
    // 失败时经 error 返回可读原因，返回 false。
    bool start(const QString &enginePath, QString *error = nullptr);
    void stop(); // 结束进程（析构 / 切换引擎时调用）
    bool isRunning() const;
    // UCI 握手是否完成（uciok + readyok）；只有 ready 后才能安全下发 position / go。
    bool isReady() const { return m_readyEmitted; }
    QString enginePath() const { return m_path; }

    // ---- UCI 指令（应在 ready() 之后调用；引擎未运行时安全忽略）----
    void setOption(const QString &name, const QString &value);
    void newGame();
    void setPosition(const QString &fen);

    // 让引擎开始思考：同时给出**搜索深度上限**与**思考时间上限**，两者先到者先停
    // （`go depth <n> movetime <ms>`）。弱档靠 depth 限强（最低档为 3），强档靠 movetime 兜底。
    // depth <= 0 表示不限深度；两者都 <= 0 时按 1000ms 处理（避免下发无约束的 go）。
    void goSearch(int depth, int moveTimeMs);

    // 纯函数：从一行 UCI 输出中提取 bestmove 的着法串。
    // "bestmove e2e4 ponder e7e5" → "e2e4"；"bestmove (none)" / 非法行 → 空串。
    static QString parseBestMove(const QString &line);

signals:
    void ready();                       // uciok + readyok 均已完成，可下发局面
    void bestMove(const QString &uci);  // 引擎给出的着法（UCI 串）
    void failed(const QString &reason); // 启动失败 / 进程异常退出
    void logLine(const QString &line);  // 引擎原始输出（调试用）

private:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessError();
    void onProcessFinished();
    void handleLine(const QString &line);
    void send(const QString &command);

    QProcess *m_process = nullptr;
    QByteArray m_buffer;
    QString m_path;
    bool m_uciOk = false;
    bool m_readyOk = false;
    bool m_readyEmitted = false; // ready() 每次 start 只发一次（避免 newGame 的 isready 触发递归）
    bool m_stopping = false;     // 主动 stop 时不再报 failed
};

// 默认引擎路径：
//   1) <程序目录>/engine（含一层子目录）下的可执行文件，优先名字含 "stockfish"；
//   2) 开发环境回退：沿程序目录向上查找 dummy/stockfish（本地测试用引擎）。
// 找不到返回空串。
QString defaultChessEnginePath();

} // namespace whalepet
