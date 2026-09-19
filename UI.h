// UI.h — Win32 系统托盘 + 设置窗口 (独立线程运行)
// 跨线程状态: UI::enabled / UI::shouldExit (std::atomic, 无锁)
//             UI::liveStatusText (std::mutex 保护)
#pragma once

#include <atomic>
#include <mutex>
#include <string>

namespace duck {

struct UI {
    // 主循环读取: true = 闪避工作; false = 暂停 (Sleep 短路)
    static std::atomic<bool> enabled;

    // 主循环读取: true = 退出
    static std::atomic<bool> shouldExit;

    // 跨线程状态栏文本: 主循环写, UI 线程读
    static std::mutex liveStatusMutex;
    static std::wstring liveStatusText;

    // 主循环调用: 把最新的状态行写入共享缓冲
    static void setLiveStatus(const std::wstring& w);
    // UI 线程调用: 取出当前状态行
    static std::wstring getLiveStatus();

    // UI 线程入口
    static int runMessageLoop();
};

} // namespace duck
