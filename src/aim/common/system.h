#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

#include <string>
#include <vector>

namespace aim {

bool RunPosixSystemCommand(const std::string& command, const std::vector<std::string>& args);

bool OpenUrlInBrowser(const std::string& url);

}  // namespace aim
