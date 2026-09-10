#include "search_selector.h"

#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/search.h"
#include "aim/core/application.h"
#include "aim/core/scenario_manager.h"
#include "imgui.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

namespace aim {

std::optional<std::string> SearchSelector(const std::string& search_text,
                                          const std::vector<std::string>& items,
                                          SearchSelectorOptions options) {
  SearchQuery query = GetSearchQuery(search_text);

  std::optional<std::string> selected_item;
  int num_matches = 0;
  for (int i = 0; i < items.size(); ++i) {
    if (num_matches >= options.max_results) {
      break;
    }
    ImGui::IdGuard id("SearchSelector", i);
    const std::string& item = items[i];
    if (StringMatchesSearch(item, query.search_words, options.empty_search_text_matches_all)) {
      std::string actual_name = item;
      if (query.name_info.HasDynamicSuffix()) {
        NameInfo actual_info = query.name_info;
        actual_info.base_name = actual_name;
        actual_name = actual_info.GetFullName();
      }
      if (options.additional_predicate) {
        bool matches = options.additional_predicate(actual_name);
        if (!matches) {
          continue;
        }
      }

      num_matches++;
      if (ImGui::Button(actual_name.c_str())) {
        selected_item = actual_name;
      }
    }
  }

  return selected_item;
}

void ScenarioSearchInput(Application& app,
                         std::string* scenario_name,
                         SearchSelectorOptions options) {
  // Never show too many results here.
  options.max_results = std::min(options.max_results, 20);

  ImGui::InputText("##ScenarioSearchInput", scenario_name);
  auto matching_scenario = app.scenario_manager().GetScenario(*scenario_name);
  if (matching_scenario) {
    return;
  }
  if (scenario_name->size() > 0) {
    ImGui::SameLine();
    ImGui::Text("%s", icons::kWarning);
    ImGui::HelpTooltip("No matching scenarios");
  }
  auto scenario_names = app.scenario_manager().scenario_names();
  auto selected_scenario = SearchSelector(*scenario_name, *scenario_names, options);
  if (selected_scenario) {
    *scenario_name = *selected_scenario;
  }
}

}  // namespace aim
