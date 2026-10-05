#include "view/AssetsResource.h"

#include <QtGlobal>

// 立绘以 Qt 资源方式内嵌到 whalepet_view 静态库中。
// 静态库的资源不会自动注册，需在使用前显式初始化。
//
// 注意：Q_INIT_RESOURCE 宏**不能出现在任何命名空间内**（包括匿名命名空间），
// 否则宏内声明的 qInitResources_<name> 会被 C++ 名称修饰成带命名空间的符号，
// 与 rcc 在全局作用域生成的符号不匹配，链接期报 LNK2019（见 docs/pitfalls/）。
// 因此本定义**必须**位于全局作用域。
//
// 幂等：qInitResources_assets() 自带保护，重复调用安全；static bool 只是
// 避免重复进入。assets 资源必须在任何窗口创建之前可用（应用图标即取自
// :/icon/whalepet.ico），故 main() 会显式先调一次。
void whalepetInitAssetsResource()
{
    static bool initialized = false;
    if (!initialized) {
        Q_INIT_RESOURCE(assets);
        initialized = true;
    }
}
