#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "google/protobuf/message.h"

namespace aim {

std::optional<std::string> ReadFileContentAsString(const std::filesystem::path& path);

bool WriteStringToFile(const std::filesystem::path& path, const std::string& content);

std::string MessageToJson(const google::protobuf::Message& message);
bool WriteJsonMessageToFile(const std::filesystem::path& path,
                            const google::protobuf::Message& message);
bool WriteBinaryMessageToFile(const std::filesystem::path& path,
                              const google::protobuf::Message& message);

bool JsonToMessage(const std::string& json, google::protobuf::Message* message);
bool ReadJsonMessageFromFile(const std::filesystem::path& path, google::protobuf::Message* message);
bool ReadBinaryMessageFromFile(const std::filesystem::path& path,
                               google::protobuf::Message* message);

std::optional<std::filesystem::file_time_type> GetMostRecentUpdateTime(
    const std::filesystem::path& base_dir);

std::optional<std::filesystem::file_time_type> GetMostRecentUpdateTime(
    const std::vector<std::filesystem::path>& dirs);

std::optional<std::filesystem::file_time_type> GetMostRecentUpdateTime(
    const std::filesystem::path& dir1, const std::filesystem::path& dir2);

void OpenFileInExplorer(const std::filesystem::path& path);
void OpenFolderInExplorer(const std::filesystem::path& path);

void MoveFileToTrash(const std::filesystem::path& path);

bool CreateDirectories(const std::filesystem::path& path);

}  // namespace aim
