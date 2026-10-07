#include "process_lock.h"

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#else
#include <limits.h>
#include <unistd.h>
#endif

#if __APPLE__
#include <libproc.h>
#endif

#include <filesystem>
#include <optional>

#include "absl/strings/numbers.h"
#include "aim/common/files.h"
#include "aim/common/simple_types.h"
#include "aim/core/file_system.h"

namespace aim {
namespace {

class ProcessLockImpl : public ProcessLock {
 public:
  ProcessLockImpl() : pid_(GetPid()) {
    FileSystem fs;
    lock_file_path_ = fs.GetUserDataPath("aimforge_pid_lock.txt");
  }

  // Returns an existing process that holds the lock or empty if this process has succesfully
  // created the lock.
  std::optional<i64> CreateLockFile() override {
    auto existing_pid_with_lock = GetValidCurrentPidWithLock();
    if (existing_pid_with_lock) {
      return existing_pid_with_lock;
    }

    WriteStringToFile(lock_file_path_, std::format("{}", pid_));
    return {};
  }

  void ReleaseLockFile() override {
    std::filesystem::remove(lock_file_path_);
  }

 private:
  std::optional<i64> GetValidCurrentPidWithLock() {
    auto maybe_file_content = ReadFileContentAsString(lock_file_path_);
    if (!maybe_file_content) {
      // Could not find existing lock file
      return {};
    }

    i64 existing_pid = 0;
    if (!absl::SimpleAtoi(*maybe_file_content, &existing_pid)) {
      return {};
    }

    if (pid_ == existing_pid) {
      // Same process. Let it proceed.
      return {};
    }

    // Check if the pid is a valid FpsAimForge process.
    std::string process_name = GetProcessNameFromPid(existing_pid);
    if (process_name == "FpsAimForge.exe" || process_name == "FpsAimForge") {
      return existing_pid;
    }

    return {};
  }

  i64 pid_;
  std::filesystem::path lock_file_path_;
};

}  // namespace

std::unique_ptr<ProcessLock> CreateProcessLock() {
  return std::make_unique<ProcessLockImpl>();
}

i64 GetPid() {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return getpid();
#endif
}

std::string GetProcessNameFromPid(i64 pid) {
#ifdef _WIN32
  char processName[MAX_PATH] = "<unknown>";

  // Get a handle to the process
  HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);

  if (hProcess != NULL) {
    HMODULE hMod;
    DWORD cbNeeded;
    // Get the process's main module (the .exe)
    if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
      GetModuleBaseNameA(hProcess, hMod, processName, sizeof(processName));
    }
    CloseHandle(hProcess);
  }
  return std::string(processName);
#elif __APPLE__
  char pathBuffer[PROC_PIDPATHINFO_MAXSIZE];

  // proc_name retrieves the name of the process for the given PID
  // It populates the buffer and returns the length of the string
  int result = proc_name(pid, pathBuffer, sizeof(pathBuffer));

  if (result > 0) {
    return std::string(pathBuffer);
  }
#else
  char path[PATH_MAX];
  char dest[PATH_MAX];
  // Construct path to the 'exe' link
  snprintf(path, sizeof(path), "/proc/%lld/exe", static_cast<long long>(pid));

  // Read where the symbolic link points
  ssize_t bytes = readlink(path, dest, sizeof(dest) - 1);
  if (bytes != -1) {
    dest[bytes] = '\0';
    std::string fullPath(dest);
    // Return only the filename, not the full path
    return fullPath.substr(fullPath.find_last_of("/") + 1);
  }
#endif
  return "Unknown";
}

}  // namespace aim
