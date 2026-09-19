# CMakeLists.txt — Audio Ducking (C++ 原生 Windows)
# 构建依赖: Windows SDK (WASAPI / COM), nlohmann/json (FetchContent)
cmake_minimum_required(VERSION 3.20)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
    "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("Audio_Ducking" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ───── nlohmann/json (header-only, FetchContent 自动下载) ─────
include(FetchContent)
FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(json)

# ───── 源文件 ─────
add_executable(Audio_Ducking
  main.cpp
  AudioScanner.cpp
  Config.cpp
  StateMachine.cpp
  VolumeWriter.cpp
  UI.cpp
)

target_include_directories(Audio_Ducking PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Audio_Ducking PRIVATE
  nlohmann_json::nlohmann_json
  Ole32      # CoInitializeEx / CoCreateInstance / CoUninitialize
  Mmdevapi   # WASAPI (IMMDeviceEnumerator / IAudioSessionManager2 / ...)
  user32     # CreateWindowEx / WndProc / GetMessage / DispatchMessage
  comctl32   # InitCommonControlsEx (STATIC / EDIT / BUTTON 基类)
  shell32    # Shell_NotifyIcon (托盘图标)
  gdi32      # LoadIcon / LoadCursor (托盘图标 + 光标)
)

# MSVC: 源文件按 UTF-8 解释 (中文字符串 + Unicode 状态符号)
if (MSVC)
  target_compile_options(Audio_Ducking PRIVATE /utf-8)
endif()

# 把 settings.json 复制到构建目录 (exe 同级)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/settings.json
               ${CMAKE_CURRENT_BINARY_DIR}/settings.json COPYONLY)
