#include "view/PoseImageLoader.h"

#include "common/PetVisuals.h"

#include <QImage>
#include <QImageReader>
#include <QSize>

namespace whalepet {

const QStringList &PoseImageLoader::allowedSuffixes()
{
    // 见头文件说明：仅 WebP，且构建期已保证 qwebp 插件存在。
    static const QStringList kSuffixes{QStringLiteral("webp")};
    return kSuffixes;
}

bool PoseImageLoader::suffixAllowed(const QString &path)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) {
        return false;
    }
    return allowedSuffixes().contains(path.mid(dot + 1).toLower());
}

bool PoseImageLoader::qtCanDecodeSuffix(const QString &suffix)
{
    if (suffix.isEmpty()) {
        return false;
    }
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    const QByteArray wanted = suffix.toLatin1();
    for (const QByteArray &format : formats) {
        if (format.compare(wanted, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QPixmap PoseImageLoader::loadChecked(const QString &path, PoseLoadError *error, QString *detail)
{
    if (error != nullptr) {
        *error = PoseLoadError::None;
    }
    if (detail != nullptr) {
        detail->clear();
    }

    const auto fail = [error, detail](PoseLoadError reason, const QString &text) {
        if (error != nullptr) {
            *error = reason;
        }
        if (detail != nullptr) {
            *detail = text;
        }
        return QPixmap();
    };

    if (path.isEmpty()) {
        return fail(PoseLoadError::UnknownKey, QStringLiteral("资源路径为空"));
    }

    // 闸门 1：格式白名单（在触碰文件之前先拒绝未授权格式）
    if (!suffixAllowed(path)) {
        return fail(PoseLoadError::FormatRejected,
                    QStringLiteral("后缀不在白名单内（允许: %1）: %2")
                        .arg(allowedSuffixes().join(QLatin1Char(',')))
                        .arg(path));
    }

    QImageReader reader(path);
    // 以文件内容判定格式，不轻信后缀：后缀只是「授权」，不是「正确性」的依据
    reader.setDecideFormatFromContent(true);
    if (!reader.canRead()) {
        return fail(PoseLoadError::Unreadable,
                    QStringLiteral("Qt 打不开该资源（未编入 qrc / 文件缺失 / 解码器插件不可用）: %1")
                        .arg(path));
    }

    // 闸门 2：尺寸。size() 只读文件头、不分配像素缓冲，
    // 因此超大图在**解码之前**即被拒绝，不会先吃掉内存再判定。
    const QSize declared = reader.size();
    if (!declared.isValid() || declared.width() != kPosePixels
        || declared.height() != kPosePixels) {
        return fail(PoseLoadError::SizeRejected,
                    QStringLiteral("尺寸必须为 %1x%1，实际 %2x%3: %4")
                        .arg(kPosePixels)
                        .arg(declared.width())
                        .arg(declared.height())
                        .arg(path));
    }

    // 闸门 3：解码
    const QImage image = reader.read();
    if (image.isNull()) {
        return fail(PoseLoadError::DecodeFailed,
                    QStringLiteral("解码失败（数据损坏或该格式不被 Qt 支持）: %1").arg(path));
    }

    // 二次校验：个别格式的 size() 与实际解码结果可能不一致，实测结果为准
    if (image.width() != kPosePixels || image.height() != kPosePixels) {
        return fail(PoseLoadError::SizeRejected,
                    QStringLiteral("解码后尺寸必须为 %1x%1，实际 %2x%3: %4")
                        .arg(kPosePixels)
                        .arg(image.width())
                        .arg(image.height())
                        .arg(path));
    }

    return QPixmap::fromImage(image);
}

QString PoseImageLoader::describe(PoseLoadError error)
{
    switch (error) {
    case PoseLoadError::None:
        return QStringLiteral("正常");
    case PoseLoadError::UnknownKey:
        return QStringLiteral("pose key 未登记");
    case PoseLoadError::FormatRejected:
        return QStringLiteral("格式不在白名单");
    case PoseLoadError::Unreadable:
        return QStringLiteral("资源不可读");
    case PoseLoadError::DecodeFailed:
        return QStringLiteral("解码失败");
    case PoseLoadError::SizeRejected:
        return QStringLiteral("尺寸非 256x256");
    }
    return QStringLiteral("未知原因");
}

} // namespace whalepet
