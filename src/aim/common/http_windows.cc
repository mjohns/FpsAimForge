#include <windows.h>
#include <winhttp.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "aim/common/http.h"
#include "aim/common/log.h"

#pragma comment(lib, "winhttp.lib")

namespace aim {
namespace {

// Helper to convert UTF-8 std::string to UTF-16 std::wstring for WinHTTP APIs.
std::wstring Utf8ToWstring(const std::string& str) {
  if (str.empty()) return std::wstring();
  int size_needed =
      MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
  std::wstring wstr(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &wstr[0], size_needed);
  return wstr;
}

// Helper to convert UTF-16 std::wstring to UTF-8 std::string.
std::string WstringToUtf8(const std::wstring& wstr) {
  if (wstr.empty()) return std::string();
  int size_needed = WideCharToMultiByte(
      CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
  std::string str(size_needed, 0);
  WideCharToMultiByte(CP_UTF8,
                      0,
                      wstr.c_str(),
                      static_cast<int>(wstr.size()),
                      &str[0],
                      size_needed,
                      nullptr,
                      nullptr);
  return str;
}

// Global WinHTTP session handle shared across threads.
HINTERNET g_winhttp_session = nullptr;

}  // namespace

bool DownloadFile(const std::string& url, const std::string& etag, FileDownload* download) {
  URL_COMPONENTS url_comp = {0};
  url_comp.dwStructSize = sizeof(url_comp);

  wchar_t hostName[256] = {0};
  wchar_t urlPath[1024] = {0};

  url_comp.lpszHostName = hostName;
  url_comp.dwHostNameLength = sizeof(hostName) / sizeof(wchar_t);
  url_comp.lpszUrlPath = urlPath;
  url_comp.dwUrlPathLength = sizeof(urlPath) / sizeof(wchar_t);

  std::wstring wurl = Utf8ToWstring(url);
  if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.length()), 0, &url_comp)) {
    return false;
  }

  HINTERNET hSession = WinHttpOpen(L"WinHTTP-Downloader/1.0",
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS,
                                   0);
  if (!hSession) {
    return false;
  }
  auto session_cleanup = absl::MakeCleanup([=]() { WinHttpCloseHandle(hSession); });

  HINTERNET hConnect = WinHttpConnect(hSession, url_comp.lpszHostName, url_comp.nPort, 0);
  if (!hConnect) {
    return false;
  }
  auto connect_cleanup = absl::MakeCleanup([=]() { WinHttpCloseHandle(hConnect); });

  DWORD requestFlags = (url_comp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                                          L"GET",
                                          url_comp.lpszUrlPath,
                                          NULL,
                                          WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          requestFlags);

  if (!hRequest) {
    return false;
  }
  auto request_cleanup = absl::MakeCleanup([=]() { WinHttpCloseHandle(hRequest); });

  bool success = false;

  if (!WinHttpSendRequest(
          hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    return false;
  }

  if (!WinHttpReceiveResponse(hRequest, NULL)) {
    return false;
  }

  // Check for HTTP 200 OK
  DWORD statusCode = 0;
  DWORD statusCodeSize = sizeof(statusCode);
  if (!WinHttpQueryHeaders(hRequest,
                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX,
                           &statusCode,
                           &statusCodeSize,
                           WINHTTP_NO_HEADER_INDEX)) {
    Logger::get()->warn("Failed to query status code for url: {}", url);
    return false;
  }

  if (statusCode == 304) {
    download->content_unchanged = true;
    return true;
  }

  if (statusCode != 200) {
    Logger::get()->warn("Unable to download file url: {}, http_code={}", url, statusCode);
    return false;
  }

  // Pre-allocate memory using Content-Length header if available
  DWORD contentLength = 0;
  DWORD clSize = sizeof(contentLength);
  if (WinHttpQueryHeaders(hRequest,
                          WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX,
                          &contentLength,
                          &clSize,
                          WINHTTP_NO_HEADER_INDEX)) {
    download->content.reserve(contentLength);
  }

  DWORD bytesAvailable = 0;
  DWORD bytesRead = 0;

  while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
    size_t oldSize = download->content.size();
    download->content.resize(oldSize + bytesAvailable);

    if (!WinHttpReadData(
            hRequest, download->content.data() + oldSize, bytesAvailable, &bytesRead)) {
      success = false;
      download->content.clear();
      return false;
    }

    // Shrink buffer if fewer bytes were read than queried
    if (bytesRead < bytesAvailable) {
      download->content.resize(oldSize + bytesRead);
    }
  }

  return true;
}

void GlobalInitializeHttp() {
  // if (g_winhttp_session == nullptr) {
  //   g_winhttp_session = WinHttpOpen(L"FpsAimForge",
  //                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
  //                                   WINHTTP_NO_PROXY_NAME,
  //                                   WINHTTP_NO_PROXY_BYPASS,
  //                                   0);
  // }
}

void GlobalCleanupHttp() {
  // if (g_winhttp_session) {
  //   WinHttpCloseHandle(g_winhttp_session);
  //   g_winhttp_session = nullptr;
  // }
}

}  // namespace aim
