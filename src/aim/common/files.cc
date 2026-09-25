#include "files.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include "aim/common/log.h"
#include "aim/common/system.h"
#include "google/protobuf/json/json.h"
#include "google/protobuf/util/json_util.h"

namespace aim {

std::optional<std::string> ReadFileContentAsString(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    return {};
  }
  std::ifstream file(path);
  if (!file.is_open()) {
    return {};
  }
  std::string content =
      std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  file.close();
  return content;
}

bool WriteStringToFile(const std::filesystem::path& path, const std::string& content) {
  std::ofstream outfile(path);
  if (!outfile.is_open()) {
    Logger::get()->warn("Unable to write content to file: {}", path.string());
    return false;
  }
  outfile << content << std::endl;
  outfile.close();
  return true;
}

std::string MessageToJson(const google::protobuf::Message& message) {
  std::string json_string;
  google::protobuf::json::PrintOptions opts;
  opts.add_whitespace = true;
  opts.unquote_int64_if_possible = true;
  auto status = google::protobuf::util::MessageToJsonString(message, &json_string, opts);
  if (!status.ok()) {
    Logger::get()->error("Unable to serialize message to json: {}", status.message());
    return "";
  }
  return json_string;
}

bool WriteJsonMessageToFile(const std::filesystem::path& path,
                            const google::protobuf::Message& message) {
  std::string json_string = MessageToJson(message);
  return WriteStringToFile(path, json_string);
}

bool WriteBinaryMessageToFile(const std::filesystem::path& path,
                              const google::protobuf::Message& message) {
  std::ofstream outfile(path, std::ios::out | std::ios::trunc | std::ios::binary);
  if (!outfile.is_open()) {
    Logger::get()->warn("Unable to open file for writing: {}", path.string());
    return false;
  }
  if (!message.SerializeToOstream(&outfile)) {
    Logger::get()->warn("Unable to write content to file: {}", path.string());
    return false;
  }

  return true;
}

bool JsonToMessage(const std::string& json, google::protobuf::Message* message) {
  google::protobuf::json::ParseOptions opts;
  opts.ignore_unknown_fields = true;
  opts.case_insensitive_enum_parsing = true;
  auto status = google::protobuf::util::JsonStringToMessage(json, message, opts);
  if (!status.ok()) {
    Logger::get()->warn("Unable to parse proto json ({}): {}", status.message(), json);
  }
  return status.ok();
}

bool ReadJsonMessageFromFile(const std::filesystem::path& path,
                             google::protobuf::Message* message) {
  auto maybe_content = ReadFileContentAsString(path);
  if (!maybe_content.has_value()) {
    Logger::get()->warn("Unable to read proto json: {}", path.string());
    return false;
  }
  return JsonToMessage(*maybe_content, message);
}

bool ReadBinaryMessageFromFile(const std::filesystem::path& path,
                               google::protobuf::Message* message) {
  if (!std::filesystem::exists(path)) {
    return false;
  }
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  if (!message->ParseFromIstream(&file)) {
    return false;
  }
  file.close();
  return true;
}

std::optional<std::filesystem::file_time_type> GetMostRecentUpdateTime(
    const std::filesystem::path& base_dir) {
  if (!std::filesystem::exists(base_dir)) {
    return {};
  }
  std::optional<std::filesystem::file_time_type> most_recent_update_time;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(base_dir)) {
    if (std::filesystem::is_regular_file(entry)) {
      auto update_time = std::filesystem::last_write_time(entry.path());
      if (!most_recent_update_time.has_value() || update_time > *most_recent_update_time) {
        most_recent_update_time = update_time;
      }
    }
  }
  return most_recent_update_time;
}

std::optional<std::filesystem::file_time_type> GetMostRecentUpdateTime(
    const std::vector<std::filesystem::path>& dirs) {
  std::optional<std::filesystem::file_time_type> last_update_time;
  for (const auto& dir : dirs) {
    auto this_update_time = GetMostRecentUpdateTime(dir);
    if (!this_update_time.has_value()) {
      continue;
    }
    bool is_more_recent = !last_update_time.has_value() || *this_update_time > *last_update_time;
    if (is_more_recent) {
      last_update_time = this_update_time;
    }
  }
  return last_update_time;
}

std::optional<std::filesystem::file_time_type> GetMostRecentUpdateTime(
    const std::filesystem::path& dir1, const std::filesystem::path& dir2) {
  std::vector<std::filesystem::path> dirs = {dir1, dir2};
  return GetMostRecentUpdateTime(dirs);
}

void OpenFileInExplorer(const std::filesystem::path& path) {
#ifdef _WIN32
  std::wstring arguments = L"/select, \"" + path.wstring() + L"\"";
  ShellExecuteW(NULL, L"open", L"explorer", arguments.c_str(), NULL, SW_SHOWNORMAL);
#endif
}

void OpenFolderInExplorer(const std::filesystem::path& fs_path) {
#ifdef _WIN32
  ShellExecuteW(NULL, L"open", fs_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif __APPLE__
  RunPosixSystemCommand("open", {fs_path.string()});
#else
  RunPosixSystemCommand("xdg-open", {fs_path.string()});
#endif
}

void MoveFileToTrash(const std::filesystem::path& fs_path) {
  std::string path = fs_path.string();

#ifdef _WIN32
  // Path must be double-null terminated for SHFileOperation
  path.append(1, '\0');

  SHFILEOPSTRUCTA file_op = {0};
  file_op.wFunc = FO_DELETE;
  file_op.pFrom = path.c_str();
  file_op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;  // FOF_ALLOWUNDO makes it go to trash
  SHFileOperationA(&file_op);
#elif __APPLE__
  // TODO: Support actual trash on apple
  std::filesystem::remove(fs_path);
#else
  if (!RunPosixSystemCommand("gio", {"trash", path})) {
    // Fall back to just trying a real delete.
    std::filesystem::remove(fs_path);
  }
#endif
}

bool CreateDirectories(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    // Already exists. No need to create.
    return true;
  }
  return std::filesystem::create_directories(path, ec);
}

}  // namespace aim
