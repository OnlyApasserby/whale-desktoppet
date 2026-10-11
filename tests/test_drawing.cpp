// P11 绘画模块单测（docs/DRAWING.md）：
//   A. core/DrawingPalette   —— 完全随机 RGB 的口径、钳制、十六进制、感知亮度
//   B. core/AbstractArt      —— 抽象画笔触范围 / 确定性 / 批次与上限
//   C. core/LineArt          —— 拓扑固定 + **拼接关系**（头插入躯干、鳍与躯干角点、
//                               尾切躯干右边界、眼在头内、水柱接头顶）+ 风格一致性 + 可复现
//   D. core/CanvasBoard      —— 笔触模型语义（开笔 / 加点 / 收笔 / 撤销 / 清空 / 边界 / 包围盒）
//   E. 工作台多页化           —— WorkbenchPage 贡献点经 UiContributionHost 通用分发成页签
//   F. 绘画模块插件与页面     —— 三个贡献点、懒建与幂等、画布自绘路径、取色回填与最近色列表
//   G. 屏幕取色覆盖层         —— 注入替身后：取色 / 确认 / 取消 / ESC 全链路（不触碰真实屏幕）

#include "core/AbstractArt.h"
#include "core/CanvasBoard.h"
#include "core/DrawingPalette.h"
#include "core/LineArt.h"
#include "plugin/PluginRegistry.h"
#include "platform/Win32ScreenColorPicker.h"
#include "view/ui/AbstractArtView.h"
#include "view/ui/DrawingBoardView.h"
#include "view/ui/DrawingUiPlugin.h"
#include "view/ui/LineArtView.h"
#include "view/ui/ScreenColorPickerOverlay.h"
#include "view/ui/UiContributionHost.h"
#include "view/ui/WorkBenchPanel.h"

#include <QApplication>
#include <QLabel>
#include <QSignalSpy>
#include <QTabWidget>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace whalepet;
using namespace whalepet::core;
using namespace whalepet::plugin;

namespace {

constexpr double kEps = 1e-4;

const LineArtShape *findPart(const LineArtPlan &plan, LineArtPart part)
{
    for (const LineArtShape &shape : plan.shapes) {
        if (shape.part == part) {
            return &shape;
        }
    }
    return nullptr;
}

} // namespace

class TestDrawing : public QObject {
    Q_OBJECT

private slots:
    // -----------------------------------------------------------------------
    // A. 配色
    // -----------------------------------------------------------------------
    void randomRgbCoversFullRangeAndAlphaWindow()
    {
        SystemRandom rng(20261011u);
        int minChannel[3] = { 255, 255, 255 };
        int maxChannel[3] = { 0, 0, 0 };
        for (int i = 0; i < 200; ++i) {
            const RgbColor color = randomRgb(rng);
            QVERIFY(color.r >= 0 && color.r <= 255);
            QVERIFY(color.g >= 0 && color.g <= 255);
            QVERIFY(color.b >= 0 && color.b <= 255);
            QVERIFY(color.a >= kMinStrokeAlpha && color.a <= kMaxStrokeAlpha);

            const int channels[3] = { color.r, color.g, color.b };
            for (int c = 0; c < 3; ++c) {
                minChannel[c] = std::min(minChannel[c], channels[c]);
                maxChannel[c] = std::max(maxChannel[c], channels[c]);
            }
        }
        // 「完全随机 RGB」的可观测性：每个通道都真正铺开了区间（不是被约束在窄带里）
        for (int c = 0; c < 3; ++c) {
            QVERIFY(minChannel[c] < 64);
            QVERIFY(maxChannel[c] > 191);
        }
    }

    void randomRgbIsDeterministicForSameSeed()
    {
        SystemRandom first(7u);
        SystemRandom second(7u);
        for (int i = 0; i < 20; ++i) {
            QVERIFY(randomRgb(first) == randomRgb(second));
        }
    }

    void clampedColorAndHexAndLuminance()
    {
        RgbColor overflow;
        overflow.r = 300;
        overflow.g = -5;
        overflow.b = 128;
        overflow.a = 999;
        const RgbColor safe = clampedColor(overflow);
        QCOMPARE(safe.r, 255);
        QCOMPARE(safe.g, 0);
        QCOMPARE(safe.b, 128);
        QCOMPARE(safe.a, 255);

        RgbColor color;
        color.r = 0x0A;
        color.g = 0xB1;
        color.b = 0xC3;
        color.a = 12; // alpha 不参与十六进制
        QCOMPARE(QString::fromStdString(toHexString(color)), QStringLiteral("#0AB1C3"));

        QCOMPARE(perceivedLuminance(RgbColor{ 0, 0, 0, 255 }), 0.0);
        QCOMPARE(perceivedLuminance(RgbColor{ 255, 255, 255, 255 }), 1.0);
    }

    // -----------------------------------------------------------------------
    // B. 抽象画
    // -----------------------------------------------------------------------
    void abstractStrokeStaysInsideCanvasAndInRange()
    {
        AbstractArtGenerator generator(480, 360);
        SystemRandom rng(99u);
        for (int i = 0; i < 300; ++i) {
            const AbstractStroke stroke = generator.next(rng);
            QVERIFY(stroke.w > 0.0f && stroke.h > 0.0f);
            QVERIFY(stroke.x - stroke.w * 0.5f >= -kEps);
            QVERIFY(stroke.x + stroke.w * 0.5f <= 1.0f + kEps);
            QVERIFY(stroke.y - stroke.h * 0.5f >= -kEps);
            QVERIFY(stroke.y + stroke.h * 0.5f <= 1.0f + kEps);
            QVERIFY(stroke.lineWidth >= kAbstractMinLineWidth);
            QVERIFY(stroke.lineWidth <= kAbstractMaxLineWidth);
            QVERIFY(stroke.rotation >= 0.0f && stroke.rotation <= 360.0f);
            const int shapeIndex = static_cast<int>(stroke.shape);
            QVERIFY(shapeIndex >= 0 && shapeIndex < static_cast<int>(AbstractShape::Count));
            if (stroke.shape == AbstractShape::Polygon) {
                QVERIFY(stroke.sides >= 5 && stroke.sides <= 7);
            }
        }
    }

    void abstractSequenceIsReproducible()
    {
        AbstractArtGenerator generator(480, 360);
        ScriptedRandom first({ 0.1, 0.9, 0.35, 0.62, 0.04, 0.77, 0.5, 0.2 });
        ScriptedRandom second({ 0.1, 0.9, 0.35, 0.62, 0.04, 0.77, 0.5, 0.2 });
        for (int i = 0; i < 12; ++i) {
            const AbstractStroke a = generator.next(first);
            const AbstractStroke b = generator.next(second);
            QCOMPARE(static_cast<int>(a.shape), static_cast<int>(b.shape));
            QCOMPARE(a.color.r, b.color.r);
            QCOMPARE(a.color.g, b.color.g);
            QCOMPARE(a.color.b, b.color.b);
            QCOMPARE(a.lineWidth, b.lineWidth);
            QVERIFY(std::fabs(a.x - b.x) < kEps);
            QVERIFY(std::fabs(a.y - b.y) < kEps);
        }
    }

    void abstractBatchAndCapAreSane()
    {
        QVERIFY(kAbstractBatchSize >= 1);
        QVERIFY(kAbstractStrokeCap > kAbstractBatchSize * 10);
    }

    // -----------------------------------------------------------------------
    // C. 线条画
    // -----------------------------------------------------------------------
    void lineArtTopologyAndJointsHold()
    {
        LineArtStyle style;
        style.outline = RgbColor{ 255, 255, 255, 255 };
        style.accent = RgbColor{ 176, 176, 176, 255 };
        style.strokeWidth = 2;

        // 多组种子都满足拼接（构造性保证，不靠容差调参）
        for (quint32 seed = 1; seed <= 32; ++seed) {
            SystemRandom rng(seed);
            const LineArtPlan plan = makeLineArt(rng, style, seed);

            QCOMPARE(static_cast<int>(plan.shapes.size()), static_cast<int>(LineArtPart::Count));
            for (int i = 0; i < static_cast<int>(LineArtPart::Count); ++i) {
                const LineArtShape &shape = plan.shapes.at(static_cast<std::size_t>(i));
                QCOMPARE(static_cast<int>(shape.part), i); // 拓扑顺序固定
                QCOMPARE(static_cast<int>(shape.primitive),
                         static_cast<int>(primitiveOfPart(shape.part)));
                // 全部几何落在归一化画布内
                QVERIFY(shape.x - shape.w * 0.5f >= -kEps);
                QVERIFY(shape.x + shape.w * 0.5f <= 1.0f + kEps);
                QVERIFY(shape.y - shape.h * 0.5f >= -kEps);
                QVERIFY(shape.y + shape.h * 0.5f <= 1.0f + kEps);
            }

            const LineArtShape *tail = findPart(plan, LineArtPart::Tail);
            const LineArtShape *body = findPart(plan, LineArtPart::Body);
            const LineArtShape *head = findPart(plan, LineArtPart::Head);
            const LineArtShape *finLeft = findPart(plan, LineArtPart::FinLeft);
            const LineArtShape *finRight = findPart(plan, LineArtPart::FinRight);
            const LineArtShape *eyeLeft = findPart(plan, LineArtPart::EyeLeft);
            const LineArtShape *eyeRight = findPart(plan, LineArtPart::EyeRight);
            const LineArtShape *sprout = findPart(plan, LineArtPart::SproutLine);
            QVERIFY(tail != nullptr && body != nullptr && head != nullptr && finLeft != nullptr
                    && finRight != nullptr && eyeLeft != nullptr && eyeRight != nullptr
                    && sprout != nullptr);

            const double bodyLeft = body->x - body->w * 0.5;
            const double bodyRight = body->x + body->w * 0.5;
            const double bodyTop = body->y - body->h * 0.5;
            const double bodyBottom = body->y + body->h * 0.5;
            const double headRadius = head->w * 0.5;
            const double headBottom = head->y + headRadius;
            const double headTop = head->y - headRadius;

            // 拼接 1：头圆底部**插入**躯干上边（重叠 > 0，且重叠深度 = 约定值）
            QVERIFY(headBottom > bodyTop);
            QVERIFY(std::fabs((headBottom - bodyTop) - 0.04) < 1e-3);

            // 拼接 2：尾鳍左顶点**精确**切在躯干右边界，且与躯干同高
            QVERIFY(std::fabs((tail->x - tail->w * 0.5) - bodyRight) < kEps);
            QVERIFY(std::fabs(tail->y - body->y) < kEps);

            // 拼接 3：左鳍顶点 = 躯干左上角；右鳍底顶点 = 躯干右下角
            QVERIFY(std::fabs(finLeft->x - bodyLeft) < kEps);
            QVERIFY(std::fabs((finLeft->y - finLeft->h * 0.5) - bodyTop) < kEps);
            QVERIFY(std::fabs(finRight->x - bodyRight) < kEps);
            QVERIFY(std::fabs((finRight->y + finRight->h * 0.5) - bodyBottom) < kEps);

            // 拼接 4：双眼完全落在头圆内
            const double eyeRadius = eyeLeft->w * 0.5;
            const double leftDistance =
                std::hypot(eyeLeft->x - head->x, eyeLeft->y - head->y) + eyeRadius;
            const double rightDistance =
                std::hypot(eyeRight->x - head->x, eyeRight->y - head->y) + eyeRadius;
            QVERIFY(leftDistance <= headRadius + kEps);
            QVERIFY(rightDistance <= headRadius + kEps);
            QVERIFY(eyeLeft->x < head->x);
            QVERIFY(eyeRight->x > head->x);

            // 拼接 5：水柱下端接在头顶、竖直向上
            float x1 = 0.0f;
            float y1 = 0.0f;
            float x2 = 0.0f;
            float y2 = 0.0f;
            lineEndpoints(*sprout, &x1, &y1, &x2, &y2);
            const double lowerY = std::max(y1, y2);
            QVERIFY(std::fabs(lowerY - headTop) < 1e-3);
            QVERIFY(std::fabs(sprout->x - head->x) < 1e-3);
        }
    }

    void lineArtStyleIsSharedAndReproducible()
    {
        LineArtStyle style;
        style.outline = RgbColor{ 255, 255, 255, 255 };
        style.accent = RgbColor{ 176, 176, 176, 255 };
        style.strokeWidth = 3;

        SystemRandom first(4242u);
        SystemRandom second(4242u);
        const LineArtPlan a = makeLineArt(first, style, 4242u);
        const LineArtPlan b = makeLineArt(second, style, 4242u);
        QCOMPARE(static_cast<int>(a.shapes.size()), static_cast<int>(b.shapes.size()));
        for (std::size_t i = 0; i < a.shapes.size(); ++i) {
            QCOMPARE(static_cast<int>(a.shapes[i].part), static_cast<int>(b.shapes[i].part));
            QVERIFY(std::fabs(a.shapes[i].x - b.shapes[i].x) < kEps);
            QVERIFY(std::fabs(a.shapes[i].y - b.shapes[i].y) < kEps);
            QVERIFY(std::fabs(a.shapes[i].w - b.shapes[i].w) < kEps);
            QVERIFY(std::fabs(a.shapes[i].h - b.shapes[i].h) < kEps);
        }

        // 风格一致性：整张图共用同一份 style（含线宽），形状只用「主体 / 次要」两族色
        QVERIFY(a.style == style);

        // 不同种子 → 姿态不同（但拓扑 / 图元集合不变）
        SystemRandom other(4243u);
        const LineArtPlan c = makeLineArt(other, style, 4243u);
        bool differs = false;
        for (std::size_t i = 0; i < a.shapes.size(); ++i) {
            if (std::fabs(a.shapes[i].x - c.shapes[i].x) > 1e-6
                || std::fabs(a.shapes[i].w - c.shapes[i].w) > 1e-6) {
                differs = true;
                break;
            }
        }
        QVERIFY(differs);
        QCOMPARE(a.shapes.size(), c.shapes.size());
    }

    void lineArtJitterStaysWithinDeclaredAmplitude()
    {
        LineArtStyle style;
        LineArtPlan plan;
        for (quint32 seed = 1; seed <= 64; ++seed) {
            SystemRandom rng(seed);
            plan = makeLineArt(rng, style, seed);
            const LineArtShape *body = findPart(plan, LineArtPart::Body);
            QVERIFY(body != nullptr);
            // 躯干抖动幅度声明在 kLineArtBodyJitter* / kLineArtBodySizeJitter
            QVERIFY(std::fabs(body->x - 0.52f) <= kLineArtBodyJitterX + 1e-3f);
            QVERIFY(std::fabs(body->y - 0.60f) <= kLineArtBodyJitterY + 1e-3f);
            QVERIFY(std::fabs(body->w - 0.44f) <= kLineArtBodySizeJitter + 1e-3f);
        }
    }

    // -----------------------------------------------------------------------
    // D. 画板模型
    // -----------------------------------------------------------------------
    void canvasModelStrokeLifecycle()
    {
        CanvasModel model(200, 120);
        QVERIFY(!model.drawing());
        model.extend({ 10.0f, 10.0f }); // 未落笔：忽略
        QCOMPARE(model.strokes().size(), std::size_t(0));

        model.begin({ 10.0f, 10.0f }, RgbColor{ 0, 0, 0, 255 }, 4, false);
        QVERIFY(model.drawing());
        model.extend({ 20.0f, 20.0f });
        model.extend({ 30.0f, 25.0f });
        QCOMPARE(model.activeStroke().points.size(), std::size_t(3));
        model.end();
        QVERIFY(!model.drawing());
        QCOMPARE(model.strokes().size(), std::size_t(1));
        QCOMPARE(model.strokes().front().points.size(), std::size_t(3));

        // 收笔后再收笔：不产生第二笔
        model.end();
        QCOMPARE(model.strokes().size(), std::size_t(1));

        // 单点笔触（点一下就抬笔）也要入库
        model.begin({ 5.0f, 5.0f }, RgbColor{ 255, 0, 0, 255 }, 2, false);
        model.end();
        QCOMPARE(model.strokes().size(), std::size_t(2));
        QCOMPARE(model.strokes().back().points.size(), std::size_t(1));

        // begin 时若已有进行中的笔，先自动收笔（不丢数据）
        model.begin({ 1.0f, 1.0f }, RgbColor{ 0, 0, 255, 255 }, 6, false);
        model.extend({ 2.0f, 2.0f });
        model.begin({ 7.0f, 7.0f }, RgbColor{ 0, 255, 0, 255 }, 6, false);
        QCOMPARE(model.strokes().size(), std::size_t(3)); // 上一笔被自动收笔，进入历史
        QCOMPARE(model.strokes().back().points.size(), std::size_t(2)); // 自动收笔保留全部点
        QVERIFY(model.drawing()); // 新的一笔正在进行
        model.end();
        QCOMPARE(model.strokes().size(), std::size_t(4));
        QVERIFY(!model.drawing());
    }

    void canvasModelClampsAndDedupes()
    {
        CanvasModel model(100, 50);
        model.begin({ -20.0f, 999.0f }, RgbColor{ 0, 0, 0, 255 }, 0, false);
        QCOMPARE(model.activeStroke().points.back().x, 0.0f);
        QCOMPARE(model.activeStroke().points.back().y, 49.0f);
        QCOMPARE(model.activeStroke().width, kCanvasMinBrushWidth); // 宽度钳制

        model.extend({ -20.0f, 999.0f }); // 同像素：不入库
        QCOMPARE(model.activeStroke().points.size(), std::size_t(1));
        model.extend({ 500.0f, 500.0f });
        QCOMPARE(model.activeStroke().points.size(), std::size_t(2));
        QCOMPARE(model.activeStroke().points.back().x, 99.0f);
        QCOMPARE(model.activeStroke().points.back().y, 49.0f);

        CanvasModel wide(100, 50);
        wide.begin({ 1.0f, 1.0f }, RgbColor{ 0, 0, 0, 255 }, 999, true);
        QCOMPARE(wide.activeStroke().width, kCanvasMaxBrushWidth);
        QVERIFY(wide.activeStroke().eraser);
    }

    void canvasModelUndoAndClear()
    {
        CanvasModel model(100, 100);
        QVERIFY(!model.undo()); // 空历史

        model.begin({ 10.0f, 10.0f }, RgbColor{ 0, 0, 0, 255 }, 3, false);
        QVERIFY(!model.undo()); // 进行中的一笔不受撤销影响
        QVERIFY(model.drawing());

        model.end();
        model.begin({ 20.0f, 20.0f }, RgbColor{ 0, 0, 0, 255 }, 3, false);
        model.end();
        QCOMPARE(model.strokes().size(), std::size_t(2));
        QVERIFY(model.undo());
        QCOMPARE(model.strokes().size(), std::size_t(1));
        QVERIFY(model.undo());
        QCOMPARE(model.strokes().size(), std::size_t(0));
        QVERIFY(!model.undo());

        model.begin({ 1.0f, 1.0f }, RgbColor{ 0, 0, 0, 255 }, 3, false);
        model.clear();
        QVERIFY(!model.drawing());
        QCOMPARE(model.strokes().size(), std::size_t(0));
    }

    void canvasBoundsIncludeBrushRadius()
    {
        CanvasStroke dot;
        dot.points.push_back({ 50.0f, 40.0f });
        dot.width = 10;
        const CanvasBounds bounds = CanvasModel::boundsOf(dot);
        QVERIFY(bounds.valid);
        QCOMPARE(bounds.minX, 45.0f);
        QCOMPARE(bounds.maxX, 55.0f);
        QCOMPARE(bounds.minY, 35.0f);
        QCOMPARE(bounds.maxY, 45.0f);

        const CanvasBounds emptyBounds = CanvasModel::boundsOf(CanvasStroke());
        QVERIFY(!emptyBounds.valid);
    }

    // -----------------------------------------------------------------------
    // E. 工作台多页化（通用分发）
    // -----------------------------------------------------------------------
    void workbenchPanelIsTabContainerAndFillsPages()
    {
        int fillCalls = 0;
        ui::WorkBenchPanel panel(nullptr, [&fillCalls](QTabWidget *tabs) {
            ++fillCalls;
            tabs->addTab(new QLabel(QStringLiteral("贡献页"), tabs), QStringLiteral("贡献页"));
        });
        QVERIFY(panel.tabs() != nullptr);
        QCOMPARE(fillCalls, 1);
        QCOMPARE(panel.tabs()->count(), 2); // 第 1 页 = 内置「干活」页
        QCOMPARE(panel.tabs()->tabText(0), QStringLiteral("干活"));
        QCOMPARE(panel.tabs()->tabText(1), QStringLiteral("贡献页"));
    }

    void workbenchPagesComeFromContributions()
    {
        QWidget host;
        ui::UiContributionHost uiHost(&host);
        PluginRegistry registry;

        ui::DrawingUiPlugin *plugin = nullptr;
        {
            auto owned = std::make_unique<ui::DrawingUiPlugin>();
            plugin = owned.get();
            QVERIFY(registry.add(std::move(owned)));
        }

        // 宿主侧通用分发：把 WorkbenchPage 贡献点挂成页签（不认具体页面）
        PluginRegistry &registryRef = registry;
        ui::WorkBenchPanel panel(nullptr, [&uiHost, &registryRef](QTabWidget *tabs) {
            uiHost.appendToTabs(registryRef, tabs, plugin::ContributionKind::WorkbenchPage);
        });

        QCOMPARE(panel.tabs()->count(), 4); // 干活 + 抽象画 + 线条画 + 画板
        QCOMPARE(panel.tabs()->tabText(0), QStringLiteral("干活"));
        QCOMPARE(panel.tabs()->tabText(1), QStringLiteral("抽象画"));
        QCOMPARE(panel.tabs()->tabText(2), QStringLiteral("线条画"));
        QCOMPARE(panel.tabs()->tabText(3), QStringLiteral("画板"));
        QVERIFY(plugin->abstractPage() != nullptr);
        QVERIFY(plugin->linePage() != nullptr);
        QVERIFY(plugin->boardPage() != nullptr);
        QCOMPARE(panel.tabs()->widget(3), static_cast<QWidget *>(plugin->boardPage()));
    }

    void settingsTabDistributionStillWorks()
    {
        // P11 把 appendToTabs 泛化为「按 kind 分发」：默认参数必须保持 SettingsTab 原行为
        QWidget host;
        ui::UiContributionHost uiHost(&host);
        PluginRegistry registry;

        class FakePlugin : public plugin::IPlugin {
        public:
            plugin::PluginInfo info() const override
            {
                plugin::PluginInfo info;
                info.id = QStringLiteral("test.settingsTab");
                return info;
            }
            void registerCapabilities(plugin::CapabilityRegistry &) override {}
            QList<plugin::PluginContribution> contributions(const plugin::IPluginUiHost &) const override
            {
                plugin::PluginContribution contribution;
                contribution.pluginId = QStringLiteral("test.settingsTab");
                contribution.id = QStringLiteral("ui.settings.page");
                contribution.kind = plugin::ContributionKind::SettingsTab;
                contribution.label = QStringLiteral("设置页");
                contribution.createView = [](QWidget *parent) -> QWidget * {
                    return new QWidget(parent);
                };
                return { contribution };
            }
        };
        QVERIFY(registry.add(std::make_unique<FakePlugin>()));

        QTabWidget tabs;
        QCOMPARE(uiHost.appendToTabs(registry, &tabs), 1);
        QCOMPARE(tabs.tabText(0), QStringLiteral("设置页"));
    }

    void drawingPluginDeclaresThreeWorkbenchPages()
    {
        ui::DrawingUiPlugin plugin;
        QWidget host;
        ui::UiContributionHost uiHost(&host);

        const QList<plugin::PluginContribution> list = plugin.contributions(uiHost);
        QCOMPARE(list.size(), 3);
        const QStringList expectedLabels = { QStringLiteral("抽象画"), QStringLiteral("线条画"),
                                             QStringLiteral("画板") };
        for (int i = 0; i < list.size(); ++i) {
            QCOMPARE(static_cast<int>(list.at(i).kind),
                     static_cast<int>(plugin::ContributionKind::WorkbenchPage));
            QCOMPARE(list.at(i).label, expectedLabels.at(i));
            QVERIFY(list.at(i).createView != nullptr);
            QCOMPARE(list.at(i).pluginId, QStringLiteral("builtin.drawing"));
        }
        QVERIFY(list.at(0).order < list.at(1).order);
        QVERIFY(list.at(1).order < list.at(2).order);

        // 懒建 + 幂等：重复取页返回同一实例；未取过时为空
        QVERIFY(plugin.abstractPage() == nullptr);
        QWidget *first = list.at(0).createView(&host);
        QWidget *again = list.at(0).createView(&host);
        QVERIFY(first != nullptr);
        QCOMPARE(first, again);
        QCOMPARE(first, static_cast<QWidget *>(plugin.abstractPage()));
    }

    // -----------------------------------------------------------------------
    // F. 页面与画布
    // -----------------------------------------------------------------------
    void abstractCanvasKeepsDrawingAndStops()
    {
        ui::AbstractArtCanvas canvas;
        QCOMPARE(canvas.image().size(), QSize(ui::AbstractArtCanvas::kCanvasWidth,
                                              ui::AbstractArtCanvas::kCanvasHeight));
        QVERIFY(canvas.running()); // 打开即「实时画布」
        QTRY_VERIFY_WITH_TIMEOUT(canvas.strokeCount() > 0, 3000);

        canvas.setRunning(false);
        QVERIFY(!canvas.running());
        const int frozen = canvas.strokeCount();
        QTest::qWait(120);
        QCOMPARE(canvas.strokeCount(), frozen); // 暂停后不再新增笔触

        // 「视觉变化」可观测：画布上出现非白像素
        const QImage image = canvas.image();
        bool changed = false;
        for (int y = 0; y < image.height() && !changed; y += 3) {
            for (int x = 0; x < image.width(); x += 3) {
                if (image.pixelColor(x, y) != QColor(255, 255, 255)) {
                    changed = true;
                    break;
                }
            }
        }
        QVERIFY(changed);

        canvas.resetCanvas();
        QCOMPARE(canvas.strokeCount(), 0);
    }

    void lineArtCanvasRegeneratesAndAcceptsInjectedPlan()
    {
        ui::LineArtCanvas canvas;
        const quint32 firstSeed = canvas.seed();
        QVERIFY(firstSeed != 0u);
        QCOMPARE(static_cast<int>(canvas.plan().shapes.size()),
                 static_cast<int>(LineArtPart::Count));

        canvas.regenerate();
        QVERIFY(canvas.seed() != 0u);
        QCOMPARE(static_cast<int>(canvas.plan().shapes.size()),
                 static_cast<int>(LineArtPart::Count));

        SystemRandom rng(11u);
        LineArtStyle style;
        style.outline = RgbColor{ 255, 255, 255, 255 };
        style.accent = RgbColor{ 176, 176, 176, 255 };
        const LineArtPlan injected = makeLineArt(rng, style, 11u);
        QSignalSpy spy(&canvas, &ui::LineArtCanvas::regenerated);
        canvas.setPlan(injected);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(canvas.seed(), 11u);
    }

    void drawingBoardHandlesBrushHistoryAndRecentColors()
    {
        ui::DrawingBoardView view;
        ui::DrawingBoardCanvas *canvas = view.canvas();
        QVERIFY(canvas != nullptr);
        QCOMPARE(canvas->image().size(), QSize(ui::DrawingBoardCanvas::kCanvasWidth,
                                               ui::DrawingBoardCanvas::kCanvasHeight));

        // 画布初始为纯白（用户口径）
        QCOMPARE(canvas->image().pixelColor(0, 0), QColor(255, 255, 255));
        QVERIFY(!canvas->undo()); // 空历史

        core::RgbColor picked;
        picked.r = 12;
        picked.g = 200;
        picked.b = 90;
        picked.a = 255;
        view.applyPickedColor(picked);
        QCOMPARE(canvas->brushColor(), picked);
        QCOMPARE(view.recentColors().size(), 1);

        view.applyPickedColor(picked); // 去重：同一颜色不重复入列
        QCOMPARE(view.recentColors().size(), 1);

        core::RgbColor another = picked;
        another.r = 240;
        view.applyPickedColor(another);
        QCOMPARE(view.recentColors().size(), 2);
        QCOMPARE(view.recentColors().front(), another); // 最新在前

        for (int i = 0; i < 10; ++i) {
            core::RgbColor color;
            color.r = 10 + i * 20;
            color.g = 30 + i * 5;
            color.b = i * 17;
            color.a = 255;
            view.applyPickedColor(color);
        }
        QCOMPARE(view.recentColors().size(), ui::kRecentColorCount); // 只保留最近 5 条
    }

    // -----------------------------------------------------------------------
    // G. 屏幕取色覆盖层（注入替身：不触碰真实屏幕）
    // -----------------------------------------------------------------------
    void pickerConfirmFlowReturnsPickedColor()
    {
        ui::ScreenColorPickerOverlay overlay;
        // 注入替身：物理光标固定在 (100, 200)，采样固定返回 (12, 34, 56)
        ui::ScreenColorPickerOverlay::CursorSource source;
        source.physicalCursor = [](int *x, int *y) {
            *x = 100;
            *y = 200;
            return true;
        };
        source.logicalCursor = [] { return QPoint(100, 200); };
        source.sampler = [](int, int) {
            platform::ScreenPixel pixel;
            pixel.valid = true;
            pixel.r = 12;
            pixel.g = 34;
            pixel.b = 56;
            return pixel;
        };
        overlay.setCursorSource(source);

        QSignalSpy pickedSpy(&overlay, &ui::ScreenColorPickerOverlay::colorPicked);
        QSignalSpy canceledSpy(&overlay, &ui::ScreenColorPickerOverlay::pickCanceled);

        QVERIFY(!overlay.picking());
        overlay.start();
        QVERIFY(overlay.picking());
        QVERIFY(overlay.colorValid());
        QVERIFY(!overlay.usingSystemMagnifier()); // 注入替身：不创建原生放大镜
        QCOMPARE(overlay.currentColor().r, 12);
        QCOMPARE(overlay.currentColor().g, 34);
        QCOMPARE(overlay.currentColor().b, 56);

        QTest::mouseClick(&overlay, Qt::LeftButton);
        QCOMPARE(pickedSpy.count(), 1);
        QCOMPARE(canceledSpy.count(), 0);
        QVERIFY(!overlay.picking());
        const QColor emitted = pickedSpy.at(0).at(0).value<QColor>();
        QCOMPARE(emitted, QColor(12, 34, 56));

        // 重复 start 幂等；确认后再次 start 仍可继续取色
        overlay.start();
        QVERIFY(overlay.picking());
        overlay.start();
        QVERIFY(overlay.picking());
        overlay.cancel();
    }

    void pickerCancelByRightButtonAndEscape()
    {
        ui::ScreenColorPickerOverlay overlay;
        ui::ScreenColorPickerOverlay::CursorSource source;
        source.physicalCursor = [](int *x, int *y) {
            *x = 10;
            *y = 10;
            return true;
        };
        source.logicalCursor = [] { return QPoint(10, 10); };
        source.sampler = [](int, int) {
            platform::ScreenPixel pixel;
            pixel.valid = true;
            return pixel;
        };
        overlay.setCursorSource(source);

        QSignalSpy pickedSpy(&overlay, &ui::ScreenColorPickerOverlay::colorPicked);
        QSignalSpy canceledSpy(&overlay, &ui::ScreenColorPickerOverlay::pickCanceled);

        overlay.start();
        QTest::mouseClick(&overlay, Qt::RightButton);
        QCOMPARE(canceledSpy.count(), 1);
        QCOMPARE(pickedSpy.count(), 0);
        QVERIFY(!overlay.picking());

        overlay.start();
        QTest::keyClick(&overlay, Qt::Key_Escape);
        QCOMPARE(canceledSpy.count(), 2);
        QVERIFY(!overlay.picking());

        overlay.cancel(); // 未在取色中：不重复发信号
        QCOMPARE(canceledSpy.count(), 2);
    }

    void pickerKeepsLastColorWhenSamplingFails()
    {
        ui::ScreenColorPickerOverlay overlay;
        int calls = 0;
        ui::ScreenColorPickerOverlay::CursorSource source;
        source.physicalCursor = [](int *x, int *y) {
            *x = 1;
            *y = 2;
            return true;
        };
        source.logicalCursor = [] { return QPoint(1, 2); };
        source.sampler = [&calls](int, int) {
            platform::ScreenPixel pixel;
            ++calls;
            if (calls == 1) {
                pixel.valid = true;
                pixel.r = 200;
                pixel.g = 100;
                pixel.b = 50;
                return pixel;
            }
            pixel.valid = false;
            pixel.error = "模拟取屏失败";
            return pixel;
        };
        overlay.setCursorSource(source);

        overlay.start();
        QVERIFY(overlay.colorValid());
        QCOMPARE(overlay.currentColor().r, 200);
        QCOMPARE(overlay.statusText(), QString());

        // 下一次轮询采样失败：**不伪造颜色** —— 保留上一有效值，并把失败原因写进状态
        QTRY_VERIFY_WITH_TIMEOUT(!overlay.statusText().isEmpty(), 3000);
        QVERIFY(!overlay.colorValid());
        QCOMPARE(overlay.currentColor().r, 200);
        QVERIFY(!overlay.statusText().isEmpty());

        overlay.cancel();
        QVERIFY(!overlay.picking());
    }

    void platformHelpersArePure()
    {
        QVERIFY(platform::rectContainsPoint(10, 10, 20, 20, 10, 10));
        QVERIFY(platform::rectContainsPoint(10, 10, 20, 20, 29, 29));
        QVERIFY(!platform::rectContainsPoint(10, 10, 20, 20, 30, 30));
        QVERIFY(!platform::rectContainsPoint(10, 10, 20, 20, 9, 10));

#ifdef Q_OS_WIN
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        QVERIFY(platform::virtualDesktopBounds(&x, &y, &width, &height));
        QVERIFY(width > 0);
        QVERIFY(height > 0);
#endif
    }
};

// 与其余绘图 / 控件测试一致：无显示环境缺省 offscreen
int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    TestDrawing testCase;
    return QTest::qExec(&testCase, argc, argv);
}

#include "test_drawing.moc"
