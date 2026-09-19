// AudioScanner.h — WASAPI 音频会话扫描器
// 对应 Python: audio_scanner.py
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>      // IAudioSessionManager2 / ISimpleAudioVolume
#include <endpointvolume.h>   // IAudioMeterInformation
#include <audioclient.h>      // ISimpleAudioVolume (some SDKs)
#include <wrl/client.h>

#include <string>
#include <utility>
#include <vector>

namespace duck {

using Microsoft::WRL::ComPtr;

struct AudioSession {
    DWORD processId = 0;
    std::string processName;  // 小写
    ComPtr<ISimpleAudioVolume> volume;
    ComPtr<IAudioMeterInformation> meter;
};

class AudioScanner {
public:
    AudioScanner();
    ~AudioScanner();

    AudioScanner(const AudioScanner&) = delete;
    AudioScanner& operator=(const AudioScanner&) = delete;

    // 扫描所有 session, 返回 (音乐 sessions, 非音乐进程最大 Peak)
    std::pair<std::vector<AudioSession>, float> scan(
        const std::vector<std::string>& targetMusicApps);

private:
    ComPtr<IMMDeviceEnumerator> enumerator_;
};

} // namespace duck
