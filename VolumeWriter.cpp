// VolumeWriter.cpp — 写入音量 (单 session 失败不影响整体)
#include "VolumeWriter.h"

#include <algorithm>

namespace duck {

void VolumeWriter::write(const std::vector<AudioSession>& sessions,
                          float volume, float maxVol, float minTargetVol) {
    float clamped = std::clamp(volume, minTargetVol, maxVol);
    for (const auto& s : sessions) {
        if (s.volume) {
            // GUID_NULL 表示立即生效
            s.volume->SetMasterVolume(clamped, nullptr);
        }
    }
}

}  // namespace duck
