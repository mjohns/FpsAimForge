#include <string>

#include "aim/common/http.h"

namespace aim {

bool DownloadFile(const std::string& url, const std::string& etag, FileDownload* download) {
  return false;
}

void GlobalInitializeHttp() {}

void GlobalCleanupHttp() {}

}  // namespace aim
