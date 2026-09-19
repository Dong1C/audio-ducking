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
#include <unordered_map>
#include <utility>
#include <vector>

namespace duck {

using Microsoft::WRL::ComPtr;

struct AudioSession {
    DWORD processId = 0;
    std::string processName;  // 小写
    ComPtr<ISimpleAudioVolume> volume;
    ComPtr<IAudioMeterInformation> meter;
    float originalVolume = 1.0f;   // 首次扫描时的音量
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

    // 把传入 sessions 中所有已记录原始音量的还原回去, 还原后清空缓存
    size_t restoreOriginalVolumes(const std::vector<AudioSession>& sessions);

    // 手动清空原始音量缓存 (不调用 restore)
    void clearVolumeCache();

private:
    ComPtr<IMMDeviceEnumerator> enumerator_;
    std::unordered_map<DWORD, float> originalVolumes_;  // pid → 首次音量
};

} // namespace duck
