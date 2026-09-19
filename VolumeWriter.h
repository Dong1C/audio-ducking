// VolumeWriter.h — 写入音量 (含安全钳制)
// 对应 Python: volume_writer.py
#pragma once

#include "AudioScanner.h"

#include <vector>

namespace duck {

class VolumeWriter {
public:
    // 将 volume 写入所有 music sessions (钳制到 [minTargetVol, maxVol])
    static void write(const std::vector<AudioSession>& sessions,
                      float volume, float maxVol, float minTargetVol);
};

} // namespace duck
