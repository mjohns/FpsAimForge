#pragma once

#include <future>
#include <string>

namespace aim {

struct FileDownload {
  std::string content;
  std::string etag;
  bool content_unchanged = false;
};

bool DownloadFile(const std::string& url, const std::string& etag, FileDownload* download);

struct FileDownloadResult {
  bool success = false;
  FileDownload download;
};

std::future<FileDownloadResult> DownloadFileAsync(const std::string& url, const std::string& etag);

void GlobalInitializeHttp();
void GlobalCleanupHttp();

}  // namespace aim
