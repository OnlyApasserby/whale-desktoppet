#pragma once

// pose 清单查询：kPoses 由 assets/poses/*.webp 自动生成（见 PoseNames.h）。

#include "core/PoseNames.h"

#include <cstring>

namespace whalepet::core {

inline bool poseExists(const char *key)
{
    if (key == nullptr || *key == '\0') {
        return false;
    }
    for (const PoseEntry &p : kPoses) {
        if (std::strcmp(p.key, key) == 0) {
            return true;
        }
    }
    return false;
}

// 返回资源文件基名；不存在返回 nullptr
inline const char *poseFile(const char *key)
{
    if (key == nullptr) {
        return nullptr;
    }
    for (const PoseEntry &p : kPoses) {
        if (std::strcmp(p.key, key) == 0) {
            return p.file;
        }
    }
    return nullptr;
}

} // namespace whalepet::core
