#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include "view/AssetsResource.h"
#include "view/PetWindow.h"

// qt-ui 资源同样内嵌在静态库 whalepet_view 中，静态库资源不会自动注册，
// 需在使用前显式初始化。Q_INIT_RESOURCE 宏**不能出现在命名空间内**（见 docs/traps-P1.md）。
static void whalepetInitQtUiResource()
{
    static bool initialized = false;
    if (!initialized) {
        Q_INIT_RESOURCE(qt_ui);
        initialized = true;
    }
}

// assets 资源（立绘 / 台词语料 / 应用图标）的初始化入口在 view/AssetsResource.h
// （全局作用域单一定义）。这里在**任何窗口创建之前**显式调一次，
// 只为让应用图标 :/icon/whalepet.ico 立即可用 —— qInitResources_assets()
// 自带幂等保护，与 PoseView / PoseLibrary 的调用重复无害。

namespace {

// 全局样式表 = default.qss（原样，禁止改写）+ project.qss（仅新增项目控件规则）
QString loadGlobalStyleSheet()
{
    whalepetInitQtUiResource();

    QString sheet;
    const char *files[] = {":/qt-ui/default.qss", ":/qt-ui/project.qss"};
    for (const char *path : files) {
        QFile file(QString::fromLatin1(path));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // 资源缺失时不崩溃，退回系统默认外观，但要写明原因（不静默）
            qWarning() << "[main] 样式表资源缺失:" << path;
            continue;
        }
        sheet += QString::fromUtf8(file.readAll());
        sheet += QLatin1Char('\n');
    }
    return sheet;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("WhalePet"));
    QApplication::setOrganizationName(QStringLiteral("WhalePet"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    // 托盘常驻：关闭窗口不应退出进程，退出统一走菜单/托盘。
    QApplication::setQuitOnLastWindowClosed(false);

    // 必须在 setStyleSheet 之前：Windows 原生样式会忽略部分 QSS 规则，
    // Fusion 让样式表取值确定可预测（接线要求，非外观设计）。
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app.setStyle(fusion);
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif

    app.setStyleSheet(loadGlobalStyleSheet());

    // 应用图标（assets/icon/whalepet.ico，随 assets.qrc 内嵌）：
    // 在创建任何窗口之前设置，对话框 / 面板都会继承。
    whalepetInitAssetsResource();
    app.setWindowIcon(QIcon(QStringLiteral(":/icon/whalepet.ico")));

    whalepet::PetWindow window;
    window.showPet();

    return app.exec();
}
