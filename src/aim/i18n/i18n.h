#pragma once

#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace aim {

class LocalStore;

enum class Language {
  kEnglish = 0,
  kChinese = 1,
};

void SetLanguage(Language language);
Language GetLanguage();

// English text is the lookup key; returns Chinese when language is zh, else English.
const char* Tr(const char* english);

template <class... Args>
std::string TrFormat(const char* english, Args&&... args) {
  auto stored = std::make_tuple(std::forward<Args>(args)...);
  return std::apply(
      [&](auto&... a) { return std::vformat(Tr(english), std::make_format_args(a...)); }, stored);
}

constexpr const char* kLanguageStoreKey = "Language";

void LoadLanguageFromStore(LocalStore& store);
void SaveLanguageToStore(LocalStore& store, Language language);

}  // namespace aim
