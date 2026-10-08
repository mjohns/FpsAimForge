#include "http.h"

#include <thread>

namespace aim {
namespace {

FileDownloadResult DownloadFileInternal(const std::string& url, const std::string& etag) {
  FileDownloadResult result;
  result.success = DownloadFile(url, etag, &result.download);
  return result;
}

}  // namespace

std::future<FileDownloadResult> DownloadFileAsync(const std::string& url, const std::string& etag) {
  return std::async(std::launch::async, DownloadFileInternal, url, etag);
}

}  // namespace aim
