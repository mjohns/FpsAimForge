#include "system.h"

#ifdef _WIN32
// #include <Windows.h>
// #include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace aim {
namespace {

#ifdef _WIN32
std::wstring Utf8ToWString(const std::string& str) {
  if (str.empty()) return std::wstring();
  int size_needed =
      MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), NULL, 0);
  std::wstring wstrTo(size_needed, 0);
  MultiByteToWideChar(
      CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &wstrTo[0], size_needed);
  return wstrTo;
}
#endif

}  // namespace

bool RunPosixSystemCommand(const std::string& command, const std::vector<std::string>& args) {
#ifndef _WIN32

  pid_t pid;

  std::vector<char*> c_args;
  c_args.reserve(args.size() + 2);
  c_args.push_back(const_cast<char*>(command.c_str()));

  // Add the rest of the arguments
  for (const auto& arg : args) {
    c_args.push_back(const_cast<char*>(arg.c_str()));
  }
  // Must be null-terminated
  c_args.push_back(nullptr);

  int status = posix_spawnp(&pid, command.c_str(), nullptr, nullptr, c_args.data(), environ);
  if (status != 0) {
    return false;
  }

  int exit_status;
  if (waitpid(pid, &exit_status, 0) == -1) {
    return false;
  }

  // WIFEXITED checks if the process terminated normally (e.g., wasn't killed by a signal)
  // WEXITSTATUS extracts the actual exit code (0 usually indicates success)
  return WIFEXITED(exit_status) && (WEXITSTATUS(exit_status) == 0);
#else
  return false;
#endif
}

bool OpenUrlInBrowser(const std::string& url) {
#ifdef _WIN32
  std::wstring wUrl = Utf8ToWString(url);
  HINSTANCE result = ShellExecuteW(NULL, L"open", wUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);

  // ShellExecute returns a value greater than 32 if it succeeds
  return reinterpret_cast<INT_PTR>(result) > 32;
#elif __APPLE__
  return RunPosixSystemCommand("open", {url});
#else
  return RunPosixSystemCommand("xdg-open", {url});
#endif
}

}  // namespace aim
