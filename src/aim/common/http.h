#pragma once

#include <string>

namespace aim {

struct FileDownload {
  std::string content;
  std::string etag;
  bool content_unchanged = false;
};

bool DownloadFile(const std::string& url, const std::string& etag, FileDownload* download);

void GlobalInitializeHttp();
void GlobalCleanupHttp();

}  // namespace aim
