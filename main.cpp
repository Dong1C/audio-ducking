// main.cpp — 主循环编排 (对应 Python: main.py)
#include "AudioScanner.h"
#include "Config.h"
#include "StateMachine.h"
#include "VolumeWriter.h"
#include "UI.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <thread>

using namespace duck;

int wmain() {
    // 控制台设为 UTF-8, 以便显示中/英混合及 Unicode 符号
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::cout << "============================================================\n";
    std::cout << " Audio Ducking (C++) — 动态比例闪避 启动\n";
    std::cout << " 运行时可调参数: 编辑 settings.json (热加载, 无需重启)\n";
    std::cout << " 系统托盘: 双击打开设置 / 右键切换启用或退出\n";
    std::cout << "============================================================\n";

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::cerr << "CoInitializeEx failed (hr=0x" << std::hex << hr << ")\n";
        return 1;
    }

    // UI 线程 (独立 Win32 消息循环; 不持有 COM apartment)
    std::thread uiThread([]() { UI::runMessageLoop(); });

    AudioScanner scanner;
    DuckerStateMachine sm;
    float currentVol = 1.0f;

    // 用稳态时钟作为时间源 (不依赖 wall-clock)
    auto t0 = std::chrono::steady_clock::now();
    auto nowSec = [&]() {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();
    };

    while (!UI::shouldExit.load()) {
        // ── 0. 暂停短路 (托盘 "禁用" 时空转, 不消耗 CPU) ──
        if (!UI::enabled.load()) {
            Sleep(50);
            continue;
        }

        // ── 1. 加载配置 (热加载) ──────────────────────
        Config cfg = Config::load();
        Sleep(static_cast<DWORD>(cfg.pollInterval * 1000.0f));

        // ── 2. 扫描音频 ─────────────────────────────────
        auto [sessions, peak] = scanner.scan(cfg.targetMusicApps);
        if (sessions.empty()) continue;

        // ── 3. 状态机推进 ───────────────────────────────
        StateStepResult r = sm.step(
            currentVol, peak, cfg.triggerThreshold, cfg.maxPeak,
            cfg.maxVol, cfg.minTargetVol,
            cfg.attackAlphaMin, cfg.attackAlphaMax, cfg.releaseAlpha,
            cfg.floorHoldSec, nowSec());
        currentVol = r.newVol;

        // ── 4. 写入音量 ─────────────────────────────────
        VolumeWriter::write(sessions, currentVol, cfg.maxVol, cfg.minTargetVol);

        // ── 5. 单行状态打印 (与 Python 版同格式) ───────
        std::printf(
            "\r[%-12s] Peak: %5.2f | Th: %.3f | MaxP: %.2f "
            "| Floor: %4.1f%% | Target: %5.1f%% | Vol: %5.1f%% | a: %.2f",
            r.status.c_str(), peak, cfg.triggerThreshold, cfg.maxPeak,
            cfg.minTargetVol * 100.0f, r.target * 100.0f, currentVol * 100.0f,
            r.alpha);
        std::fflush(stdout);
    }

    // 等待 UI 线程干净退出 (PostQuitMessage 已发出)
    uiThread.join();

    CoUninitialize();
    return 0;
}
