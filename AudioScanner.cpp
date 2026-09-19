// AudioScanner.cpp — WASAPI 会话枚举实现
#include "AudioScanner.h"

#include <algorithm>
#include <cctype>

namespace duck {

namespace {

std::string getProcessName(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};

    char path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    if (!QueryFullProcessImageNameA(h, 0, path, &size)) {
        CloseHandle(h);
        return {};
    }
    CloseHandle(h);

    std::string full(path);
    size_t pos = full.find_last_of("\\/");
    std::string name = (pos != std::string::npos) ? full.substr(pos + 1) : full;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

}  // namespace

AudioScanner::AudioScanner() {
    // 调用方负责 CoInitializeEx (在 main.cpp 中)
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                     IID_PPV_ARGS(&enumerator_));
}

AudioScanner::~AudioScanner() = default;

std::pair<std::vector<AudioSession>, float> AudioScanner::scan(
    const std::vector<std::string>& targetMusicApps) {

    std::vector<AudioSession> music;
    float maxOtherPeak = 0.0f;

    if (!enumerator_) return {music, maxOtherPeak};

    ComPtr<IMMDevice> device;
    if (FAILED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
        return {music, maxOtherPeak};
    }

    ComPtr<IAudioSessionManager2> sessionMgr;
    if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
                                 nullptr, &sessionMgr))) {
        return {music, maxOtherPeak};
    }

    ComPtr<IAudioSessionEnumerator> sessionEnum;
    if (FAILED(sessionMgr->GetSessionEnumerator(&sessionEnum))) {
        return {music, maxOtherPeak};
    }

    int count = 0;
    if (FAILED(sessionEnum->GetCount(&count))) return {music, maxOtherPeak};

    for (int i = 0; i < count; ++i) {
        ComPtr<IAudioSessionControl> ctl;
        if (FAILED(sessionEnum->GetSession(i, &ctl))) continue;

        ComPtr<IAudioSessionControl2> ctl2;
        if (FAILED(ctl.As(&ctl2))) continue;

        DWORD pid = 0;
        ctl2->GetProcessId(&pid);
        if (pid == 0) continue;  // 系统 session

        ComPtr<IAudioMeterInformation> meter;
        if (FAILED(ctl.As(&meter))) continue;

        float peak = 0.0f;
        if (FAILED(meter->GetPeakValue(&peak))) continue;

        std::string name = getProcessName(pid);
        if (name.empty()) continue;

        bool isMusic = std::any_of(targetMusicApps.begin(), targetMusicApps.end(),
            [&](const std::string& m) { return name.find(m) != std::string::npos; });

        if (isMusic) {
            ComPtr<ISimpleAudioVolume> vol;
            if (SUCCEEDED(ctl.As(&vol))) {
                AudioSession s;
                s.processId   = pid;
                s.processName = std::move(name);
                s.volume      = std::move(vol);
                s.meter       = std::move(meter);
                music.push_back(std::move(s));
            }
        } else if (peak > maxOtherPeak) {
            maxOtherPeak = peak;
        }
    }

    return {std::move(music), maxOtherPeak};
}

}  // namespace duck
