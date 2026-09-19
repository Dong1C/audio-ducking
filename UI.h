// UI.h — Win32 系统托盘 + 设置窗口 (独立线程运行)
// 跨线程状态: UI::enabled / UI::shouldExit (std::atomic, 无锁)
#pragma once

#include <atomic>

namespace duck {

struct UI {
    // 主循环读取: true = 闪避工作; false = 暂停 (Sleep 短路)
    static std::atomic<bool> enabled;

    // 主循环读取: true = 退出 (用户从托盘菜单选择"退出")
    static std::atomic<bool> shouldExit;

    // UI 线程入口: 注册窗口类 → 建隐藏主窗口 → 加托盘 → 进入消息循环
    // 返回 0 (PostQuitMessage 触发后返回)
    static int runMessageLoop();
};

} // namespace duck
