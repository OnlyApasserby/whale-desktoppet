#include "viewmodel/RecycleBinService.h"

#include <QDebug>
#include <QRandomGenerator>
#include <QTimer>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <shellapi.h>
#endif

namespace whalepet::viewmodel {

namespace {

constexpr int kStateUnknown = 0;
constexpr int kStateEmpty = 1;
constexpr int kStateNonEmpty = 2;

} // namespace

RecycleBinService::RecycleBinService(QObject *parent)
    : QObject(parent)
{
}

RecycleBinService::~RecycleBinService() = default;

RecycleBinInfo RecycleBinService::query() const
{
    RecycleBinInfo info;
#if defined(Q_OS_WIN)
    // pszRootPath == nullptr：汇总所有驱动器的回收站
    SHQUERYRBINFO rb{};
    rb.cbSize = sizeof(rb);
    if (SHQueryRecycleBinW(nullptr, &rb) == S_OK) {
        info.available = true;
        info.itemCount = static_cast<int>(rb.i64NumItems);
        info.sizeBytes = static_cast<qint64>(rb.i64Size);
    }
#else
    // 非 Windows：无对应 API，保持「未知」——不伪造数据
    info.available = false;
#endif
    return info;
}

void RecycleBinService::setIntervalRange(int minMs, int maxMs)
{
    if (minMs <= 0 || maxMs <= 0 || minMs > maxMs) {
        return; // 非法输入忽略，保持原区间
    }
    m_minMs = minMs;
    m_maxMs = maxMs;
}

void RecycleBinService::start()
{
    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::CoarseTimer);
        m_timer->setSingleShot(true); // 每轮结束后重新随机调度
        connect(m_timer, &QTimer::timeout, this, [this]() {
            poll();
            scheduleNext();
        });
    }
    m_state = kStateUnknown; // 重新开始：允许对当前非空状态提醒一次
    poll();                  // 启动即检查一次（检测既已存在的非空回收站）
    scheduleNext();
}

void RecycleBinService::stop()
{
    if (m_timer != nullptr) {
        m_timer->stop();
    }
}

bool RecycleBinService::running() const
{
    return m_timer != nullptr && m_timer->isActive();
}

void RecycleBinService::scheduleNext()
{
    if (m_timer == nullptr) {
        return;
    }
    const int span = m_maxMs - m_minMs;
    const int interval = (span > 0) ? m_minMs + QRandomGenerator::global()->bounded(span + 1)
                                    : m_minMs;
    m_timer->start(interval);
}

void RecycleBinService::poll()
{
    m_last = query();
    emit checked(m_last.available, m_last.itemCount, m_last.sizeBytes);

    if (!m_last.available) {
        return; // 查询失败 / 平台不支持：保持未知，不改变边沿状态、不提醒
    }

    if (m_last.isEmpty()) {
        m_state = kStateEmpty; // 清空后重新武装：下次非空会再次提醒
        return;
    }

    if (m_state != kStateNonEmpty) {
        m_state = kStateNonEmpty;
        emit recycleBinNotEmpty(m_last.itemCount, m_last.sizeBytes);
    }
}

bool RecycleBinService::checkNow()
{
    m_last = query();
    emit checked(m_last.available, m_last.itemCount, m_last.sizeBytes);
    if (!m_last.available) {
        return false; // 查询失败：按「无法确认非空」处理
    }
    if (m_last.isEmpty()) {
        m_state = kStateEmpty;
        return false;
    }
    m_state = kStateNonEmpty;
    emit recycleBinNotEmpty(m_last.itemCount, m_last.sizeBytes); // 手动检查无条件提醒
    return true;
}

} // namespace whalepet::viewmodel
