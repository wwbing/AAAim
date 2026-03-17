#pragma once

#include <chrono>
#include <string>

namespace aim {

bool FileExists(const std::string& path);
std::string ResolveModelPath();
std::string BuildTuningCsvPath();
bool EnsureLogsDirectory();
bool KeyPressedEdge(int vk, bool& prev_down);
void ConfigureConsoleUtf8();
void EnableDpiAwareness();
void PreciseSleepUntil(const std::chrono::steady_clock::time_point& target_time);

} // namespace aim

