#pragma once

// 立绘图片的**唯一**加载与校验入口（docs/POSE-ASSETS.md 阶段 B「路径 A」）。
//
// 三条硬约束（任一不满足即拒绝加载，**不静默降级、不缩放、不转码**）：
//   1. **格式**：文件后缀必须在 allowedSuffixes() 白名单内，**且**该后缀对应的格式
//      确实能被 **Qt 自身的解码器**读取（含 Qt 官方 imageformats 插件，如 WebP）。
//      本项目**不引入任何额外格式解析**——没有 libwebp / stb_image / 自写解码器，
//      也不调用任何第三方图像库；解码完全交给 Qt。
//   2. **尺寸**：必须**严格等于** kPosePixels × kPosePixels（256×256，见
//      common/PetVisuals.h）。不合规直接拒绝，既不缩放到合规尺寸，也不裁剪。
//   3. **解码**：一律经 QImageReader 显式读取，失败按 PoseLoadError 分类回报，
//      绝不吞异常、绝不「返回空图当作成功」。
//
// 尺寸闸门前置到**解码之前**：QImageReader::size() 只读文件头、不分配像素缓冲，
// 因此超大图在分配内存之前即被拒绝——这是内存不足 / OOM 崩溃的闸门。
//
// 零 Qt 之外的依赖；调用方全部在 GUI 线程（QPixmap 要求），故不涉及跨线程图像传递。

#include <QPixmap>
#include <QString>
#include <QStringList>

namespace whalepet {

// 立绘加载失败原因（可观测：运行期日志与单测断言共用同一套分类，
// 目的是区分「未编入 qrc」「格式未授权」「数据损坏」「尺寸不合规」）
enum class PoseLoadError {
    None = 0,
    UnknownKey,     // pose key 未登记在 core::kPoses，拼不出资源路径
    FormatRejected, // 后缀不在白名单内（未授权格式）
    Unreadable,     // 资源打不开：未编入 qrc、外部文件缺失、解码器/插件不可用
    DecodeFailed,   // Qt 解码器解码失败：数据损坏或该格式不被支持
    SizeRejected,   // 尺寸不等于 kPosePixels × kPosePixels
};

class PoseImageLoader {
public:
    // 授权的文件后缀（小写、不含点）。
    //
    // 当前**仅 WebP**：93 张立绘全部为 webp（VP8X），且构建期已强制校验
    // imageformats/qwebp 插件存在（cmake/QtDependencies.cmake，缺失即 FATAL_ERROR）。
    // 新增格式必须先在此登记，并确认该格式能被 Qt 原生解码 —— 白名单是
    // 「授权」闸门，Qt 能力是「可行性」闸门，两者同时成立才允许加载。
    static const QStringList &allowedSuffixes();

    // 路径后缀是否在白名单内（大小写不敏感）；无后缀一律拒绝
    static bool suffixAllowed(const QString &path);

    // 该后缀对应的格式当前是否**确实**能被 Qt 原生解码
    // （读 QImageReader::supportedImageFormats()，含已加载的官方插件）。
    // 白名单与 Qt 能力必须同时成立。
    static bool qtCanDecodeSuffix(const QString &suffix);

    // 严格加载。成功：返回非空 QPixmap，*error = None。
    // 失败：返回空 QPixmap，并通过 *error / *detail 回报原因（detail 为中文可读文案，
    // 两个指针均可为 nullptr）。
    static QPixmap loadChecked(const QString &path, PoseLoadError *error, QString *detail);

    // 失败原因的稳定文案。运行期日志与单测断言都引用这里，避免各处硬编码字符串。
    static QString describe(PoseLoadError error);
};

} // namespace whalepet
