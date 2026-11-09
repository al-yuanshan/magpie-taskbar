#pragma once
#include <windows.h>
#include <string>
#include "settings.h"

namespace magpie {
Settings LoadSettings(const std::wstring& path);
bool SaveSettings(const std::wstring& path, const Settings& settings);
bool ShowSettingsDialog(HWND parent, HINSTANCE instance, const std::wstring& path);
}
