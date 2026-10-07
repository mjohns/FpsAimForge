#include <curl/curl.h>

#include <string>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/ascii.h"
#include "aim/common/http.h"
#include "aim/common/log.h"

namespace aim {
namespace {

// Callback to accumulate incoming body chunks into a std::string
size_t WriteStringCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total_size = size * nmemb;
  std::string* str = static_cast<std::string*>(userp);
  str->append(static_cast<char*>(contents), total_size);
  return total_size;
}

// Callback to parse incoming headers and extract the server's ETag
size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
  size_t total_size = size * nitems;
  std::string header(buffer, total_size);
  std::string* etag_out = static_cast<std::string*>(userdata);

  // Case-insensitive check for ETag header line
  std::string prefix = "etag: ";
  std::string lower_header = absl::AsciiStrToLower(header);

  if (lower_header.find(prefix) == 0) {
    // Extract value and trim trailing \r\n
    std::string value = header.substr(6);
    while (!value.empty() &&
           (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) {
      value.pop_back();
    }
    *etag_out = value;
  }
  return total_size;
}

}  // namespace

bool DownloadFile(const std::string& url, const std::string& etag, FileDownload* download) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    return false;
  }
  auto curl_cleanup = absl::MakeCleanup([=]() { curl_easy_cleanup(curl); });

  struct curl_slist* headers = nullptr;

  // Send If-None-Match header if we already have a stored ETag
  if (!etag.empty()) {
    std::string etag_header = std::format("If-None-Match: \"{}\"", etag);
    headers = curl_slist_append(headers, etag_header.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download->content);

  std::string new_etag;
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &new_etag);

  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  CURLcode res = curl_easy_perform(curl);

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  // Clean up libcurl resources
  if (headers) {
    curl_slist_free_all(headers);
  }

  if (res != CURLE_OK) {
    Logger::get()->warn(
        "Unable to download file url: {}, curl_error={}", url, curl_easy_strerror(res));
    return false;
  }

  if (http_code == 304) {
    // File has not changed
    download->content_unchanged = true;
    return true;
  }
  if (http_code != 200) {
    Logger::get()->warn("Unable to download file url: {}, http_code={}", url, http_code);
    return false;
  }

  download->etag = new_etag;
  return true;
}

void GlobalInitializeHttp() {
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

void GlobalCleanupHttp() {
  curl_global_cleanup();
}

}  // namespace aim
