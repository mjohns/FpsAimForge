#include "ai_assistant_screen.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "absl/time/time.h"
#include "aim/analysis/nim_client.h"
#include "aim/common/files.h"
#include "aim/common/mat_icons.h"
#include "aim/common/name_util.h"
#include "aim/common/resource_name.h"
#include "aim/common/util.h"
#include "aim/core/bundle_manager.h"
#include "aim/core/playlist_manager.h"
#include "aim/core/scenario_manager.h"
#include "aim/core/stats_manager.h"
#include "aim/proto/playlist.pb.h"
#include "aim/proto/scenario.pb.h"
#include "aim/ui/ui_screen.h"
#include "imgui.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

namespace aim {
namespace {

struct ChatMessage {
  bool user = false;
  std::string text;
};

std::string ExtractLineValue(const std::string& text, const std::string& prefix) {
  const size_t pos = text.find(prefix);
  if (pos == std::string::npos) return {};
  const size_t start = pos + prefix.size();
  const size_t end = text.find('\n', start);
  return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string ExtractJsonObject(const std::string& text, const std::string& prefix) {
  const size_t pos = text.find(prefix);
  if (pos == std::string::npos) return {};
  size_t start = pos + prefix.size();
  while (start < text.size() && (text[start] == ' ' || text[start] == '\n' || text[start] == '\r')) {
    ++start;
  }
  if (start >= text.size() || text[start] != '{') return {};

  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (size_t i = start; i < text.size(); ++i) {
    const char c = text[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
    } else if (c == '{') {
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0) return text.substr(start, i - start + 1);
    }
  }
  return {};
}

class AiAssistantScreen : public UiScreen {
 public:
  AiAssistantScreen() {
    current_session_ = 0;
    LoadMemory();
    if (messages_.empty()) {
      messages_.push_back({false,
                           "I have access to your scenarios, playlists, statistics, settings, "
                           "replay/analysis features, and writable bundles. I can also create "
                           "scenarios and playlists. Try: build me a 20-minute tracking playlist "
                           "from my weakest scenarios."});
      SaveMemory();
    }
  }

  // Rotate sessions: shift existing files up by one slot, making slot 0 free.
  void RotateSessions() {
    std::error_code ec;
    // Drop the oldest if we're at the limit
    auto oldest = SessionPath(kMaxSessions - 1);
    std::filesystem::remove(oldest, ec);
    // Shift session 3→4, 2→3, 1→2, 0→1
    for (int i = kMaxSessions - 1; i > 0; --i) {
      auto from = SessionPath(i - 1);
      auto to   = SessionPath(i);
      if (std::filesystem::exists(from, ec)) {
        std::filesystem::rename(from, to, ec);
      }
    }
  }

  // Return a one-line preview of a session (first AI message, truncated).
  std::string SessionPreview(int index) const {
    const auto path = SessionPath(index);
    std::ifstream f(path, std::ios::binary);
    if (!f) return "(empty)";
    std::string magic;
    std::getline(f, magic);
    if (!magic.empty() && magic.back() == '\r') magic.pop_back();
    if (magic != "FPSAIMFORGE_AI_CHAT_V1") return "(invalid)";
    // Read messages until we find an AI message
    while (f.good()) {
      char role = 0;
      while (f.good()) {
        f.get(role);
        if (!f) return "(empty)";
        if (role == 'U' || role == 'A') break;
        if (role != '\n' && role != '\r') { std::string d; std::getline(f, d); }
      }
      if (!f || (role != 'U' && role != 'A')) break;
      if (f.peek() != ' ') break;
      f.get();
      std::string size_text; std::getline(f, size_text);
      if (!size_text.empty() && size_text.back() == '\r') size_text.pop_back();
      if (size_text.empty()) break;
      size_t size = 0;
      try { size = std::stoull(size_text); } catch (...) { break; }
      if (size == 0 || size > 512U * 1024U) break;
      std::string text(size, '\0');
      f.read(text.data(), static_cast<std::streamsize>(size));
      if (static_cast<size_t>(f.gcount()) != size) break;
      while (f.peek() == '\n' || f.peek() == '\r') f.get();
      if (role == 'U') {
        // Return the user's first message as preview
        if (text.size() > 60) text = text.substr(0, 57) + "...";
        return text;
      }
    }
    return "(empty)";
  }

 protected:
  void DrawScreen() override {
    if (app_.BeginFullscreenWindow("AI Coach")) {
      DrawContent();
    }
    ImGui::End();
  }

  void OnTick() override {
    if (request_) {
      bool done = false;
      {
        std::lock_guard lock(request_->mutex);
        done = request_->done;
      }
      if (done) {
        std::lock_guard lock(request_->mutex);
        if (request_->success) {
          const std::string response = request_->response;
          messages_.push_back({false, response});
          ExecuteAction(response);
          SaveMemory();
          scroll_to_bottom_ = true;
        } else {
          messages_.push_back({false, "AI request failed: " + request_->error});
          SaveMemory();
          scroll_to_bottom_ = true;
        }
        request_.reset();
      }
    }
    UiScreen::OnTick();
  }

 private:

  std::filesystem::path SessionPath(int index) const {
    if (index == 0) return app_.file_system().GetUserDataPath("ai_coach_memory.bin");
    return app_.file_system().GetUserDataPath(
        std::format("ai_coach_memory_{}.bin", index));
  }
  std::filesystem::path MemoryPath() const { return SessionPath(current_session_); }

  void LoadMemory() {
    const auto path = MemoryPath();
    std::ifstream input(path, std::ios::binary);
    if (!input) return;
    std::string magic;
    std::getline(input, magic);
    // Strip any trailing \r so the check works on both Windows and Linux.
    if (!magic.empty() && magic.back() == '\r') magic.pop_back();
    if (magic != "FPSAIMFORGE_AI_CHAT_V1") return;
    constexpr std::size_t kMaxMemoryFileBytes = 2U * 1024U * 1024U;
    std::error_code file_size_ec;
    const auto file_size = std::filesystem::file_size(path, file_size_ec);
    if (file_size_ec || file_size > kMaxMemoryFileBytes) {
      return;
    }
    while (input.good()) {
      // Skip blank / separator lines between messages.
      char role = 0;
      while (input.good()) {
        input.get(role);
        if (!input) goto done;
        if (role == 'U' || role == 'A') break;
        // Not a role byte — skip until end of line and try again.
        if (role != '\n' && role != '\r') {
          std::string dummy;
          std::getline(input, dummy);
        }
      }
      if (!input || (role != 'U' && role != 'A')) break;
      if (input.peek() != ' ') break;
      input.get();
      std::string size_text;
      std::getline(input, size_text);
      if (!size_text.empty() && size_text.back() == '\r') size_text.pop_back();
      if (size_text.empty()) break;
      size_t size = 0;
      try {
        size = std::stoull(size_text);
      } catch (...) {
        break;
      }
      if (size == 0 || size > 512U * 1024U) break;
      std::string text(size, '\0');
      input.read(text.data(), static_cast<std::streamsize>(size));
      if (static_cast<size_t>(input.gcount()) != size) break;
      // Consume the trailing newline separator written by SaveMemory.
      while (input.peek() == '\n' || input.peek() == '\r') input.get();
      messages_.push_back({role == 'U', std::move(text)});
    }
    done:;
  }

  void SaveMemory() {
    const auto path = MemoryPath();
    CreateDirectories(path.parent_path());
    const auto temp = path.string() + ".tmp";
    std::ofstream output(temp, std::ios::binary | std::ios::trunc);
    if (!output) return;
    output << "FPSAIMFORGE_AI_CHAT_V1\n";
    for (const auto& message : messages_) {
      output << (message.user ? 'U' : 'A') << ' ' << message.text.size() << '\n';
      output.write(message.text.data(), static_cast<std::streamsize>(message.text.size()));
      output << '\n';
    }
    output.close();
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
      std::filesystem::remove(path, ec);
      std::filesystem::rename(temp, path, ec);
    }
    if (ec) {
      std::filesystem::remove(temp, ec);
    }
  }

  void DrawContent() {
    const float char_x = ImGui::GetFontSize();

    // ── Header row ──────────────────────────────────────────────────────────
    {
      auto header_font = app_.font_manager().UseMedium();
      ImGui::Text("AI Coach");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("NVIDIA NIM");
    ImGui::SameLine();

    // Chat menu
    if (ImGui::Button("Chat")) {
      ImGui::OpenPopup("AiCoachChatMenu");
    }
    if (ImGui::BeginPopup("AiCoachChatMenu")) {
      if (ImGui::MenuItem("New chat")) {
        // Save current session, rotate files, start fresh in slot 0.
        SaveMemory();
        RotateSessions();
        current_session_ = 0;
        messages_.clear();
        messages_.push_back({false, "New chat started. Your local app statistics and capabilities are available to me."});
        SaveMemory();
        scroll_to_bottom_ = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Memory info")) {
        ImGui::OpenPopup("AiCoachMemory");
      }
      if (ImGui::MenuItem("Clear this chat")) {
        messages_.clear();
        SaveMemory();
        messages_.push_back({false, "Chat cleared."});
        SaveMemory();
        scroll_to_bottom_ = true;
      }
      ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("AiCoachMemory")) {
      const auto memory_path = SessionPath(0);
      ImGui::TextWrapped("Chat sessions are stored locally:");
      ImGui::TextWrapped("%s", memory_path.parent_path().string().c_str());
      ImGui::Separator();
      ImGui::TextWrapped("Up to %d sessions are kept. Live app data is re-read each message.", kMaxSessions);
      if (ImGui::Button("Open folder")) {
        OpenFolderInExplorer(memory_path.parent_path());
      }
      ImGui::EndPopup();
    }

    // Sessions button
    ImGui::SameLine();
    if (ImGui::Button("Sessions")) {
      ImGui::OpenPopup("AiCoachSessions");
    }
    if (ImGui::BeginPopup("AiCoachSessions")) {
      ImGui::TextDisabled("Recent sessions (newest first)");
      ImGui::Separator();
      for (int i = 0; i < kMaxSessions; ++i) {
        std::error_code ec;
        if (i > 0 && !std::filesystem::exists(SessionPath(i), ec)) break;
        const std::string preview = SessionPreview(i);
        const bool is_current = (i == current_session_);
        std::string label = std::format("{} {}: {}",
            is_current ? ">" : " ", i == 0 ? "Latest" : std::format("Session -{}", i), preview);
        if (ImGui::Selectable(label.c_str(), is_current) && !is_current) {
          SaveMemory();
          current_session_ = i;
          messages_.clear();
          LoadMemory();
          if (messages_.empty()) {
            messages_.push_back({false, "This session is empty."});
          }
          scroll_to_bottom_ = true;
        }
      }
      ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%s", current_session_ == 0 ? "Latest" : std::format("Session -{}", current_session_).c_str());
    ImGui::Separator();

    if (!IsNimConfigured()) {
      ImGui::TextWrapped(
          "NVIDIA NIM is not configured. Set NVIDIA_NIM_API_KEY and restart FpsAimForge.");
      ImGui::Spacing();
      ImGui::TextWrapped("%s", GetNimConfigurationHint().c_str());
      return;
    }

    bool scroll_to_bottom = false;
    if (ImGui::BeginChild("ChatHistory", ImVec2(0, -char_x * 9), true)) {
      for (size_t i = 0; i < messages_.size(); ++i) {
        const auto& message = messages_[i];
        ImGui::PushID(static_cast<int>(i));
        if (message.user) {
          ImGui::PushStyleColor(ImGuiCol_Text,
              ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
          ImGui::TextWrapped("You: %s", message.text.c_str());
          ImGui::PopStyleColor();
        } else {
          ImGui::PushStyleColor(ImGuiCol_Text,
              ImGui::GetStyle().Colors[ImGuiCol_Text]);
          ImGui::TextDisabled("AI");
          ImGui::SameLine();
          ImGui::TextWrapped("%s", message.text.c_str());
          ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        ImGui::PopID();
      }
      if (request_) {
        ImGui::TextDisabled("AI is thinking...");
        scroll_to_bottom = true;
      }
      // Auto-scroll to bottom when new messages arrive.
      if (scroll_to_bottom_ || scroll_to_bottom) {
        ImGui::SetScrollHereY(1.0f);
        scroll_to_bottom_ = false;
      }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    // Reserve space for Send + Back buttons on the right.
    float btn_width = char_x * 7;
    float input_width = ImGui::GetContentRegionAvail().x - btn_width * 2 - ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetNextItemWidth(input_width);
    // No EnterReturnsTrue / CtrlEnterForNewLine — Enter adds a newline naturally.
    // Send via button or Ctrl+Enter shortcut checked below.
    ImGui::InputTextMultiline(
        "##AiInput", &input_, ImVec2(input_width, char_x * 5));
    const bool ctrl_enter = ImGui::IsItemFocused() &&
                            ImGui::IsKeyPressed(ImGuiKey_Enter, false) &&
                            ImGui::GetIO().KeyCtrl;
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::BeginDisabled(request_ != nullptr || input_.empty());
    if (ImGui::Button("Send", ImVec2(btn_width, char_x * 2.5f)) || (ctrl_enter && !input_.empty())) {
      SendMessage();
      scroll_to_bottom_ = true;
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Back", ImVec2(btn_width, char_x * 2.5f))) {
      PopSelf();
    }
    ImGui::EndGroup();
  }

  std::string BuildAppContext(const std::string& user_text) {
    std::string context;
    context += "FpsAimForge live application context. Do not claim capabilities not listed here.\n";
    context += "Writable bundles: ";
    for (const auto& bundle : app_.bundle_manager().GetWritableBundleNames()) {
      context += bundle + ", ";
    }
    context += "\nCurrent scenario: " + app_.scenario_manager().GetCurrentScenarioName() + "\n";
    context += "Current playlist: " + app_.playlist_manager().current_playlist_name() + "\n";

    auto scenario_names = app_.scenario_manager().scenario_names();
    context += std::format("Scenarios ({}):\n", scenario_names ? scenario_names->size() : 0);
    if (scenario_names) {
      for (const auto& name : *scenario_names) {
        auto aggregate = app_.stats_manager().GetAggregateStats(name);
        const double high_cm =
            aggregate.high_score_stats.cm_per_360 > 0
                ? aggregate.high_score_stats.cm_per_360
                : aggregate.high_score_stats.mm_per_360 / 10.0;
        context += std::format("  {} | runs={} | high_score={:.3f} | last_score={:.3f} | high_cm360={:.3f}\n",
                               name,
                               aggregate.total_runs,
                               aggregate.high_score_stats.score,
                               aggregate.last_run_stats.score,
                               high_cm);
        if (user_text.find(name) != std::string::npos) {
          auto full_stats = app_.stats_manager().GetStats(name);
          context += std::format("    RAW STATS FOR REQUESTED SCENARIO ({} rows):\n", full_stats.size());
          const size_t start = full_stats.size() > 50 ? full_stats.size() - 50 : 0;
          for (size_t i = start; i < full_stats.size(); ++i) {
            const auto& row = full_stats[i];
            const double cm =
                row.cm_per_360 > 0 ? row.cm_per_360 : row.mm_per_360 / 10.0;
            const std::string date =
                absl::FormatTime("%Y-%m-%d %H:%M:%S UTC",
                                 absl::FromTimeT(row.epoch_seconds),
                                 absl::UTCTimeZone());
            const double shots = row.info.num_shots();
            const double hits = row.info.num_hits();
            const double accuracy = shots > 0 ? 100.0 * hits / shots : 0.0;
            context += std::format(
                "      run={} date={} score={:.3f} cm360={:.3f} hits={:.1f} shots={:.1f} accuracy={:.2f}%\n",
                row.stats_id, date, row.score, cm, hits, shots, accuracy);
          }
        }
      }
    }

    auto playlist_names = app_.playlist_manager().playlist_names();
    context += std::format("Playlists ({}):\n", playlist_names ? playlist_names->size() : 0);
    if (playlist_names) {
      for (const auto& name : *playlist_names) {
        auto playlist = app_.playlist_manager().GetPlaylist(name);
        if (!playlist) continue;
        context += std::format("  {} | items={}", name, playlist->items().size());
        for (const auto& item : playlist->items()) {
          context += std::format(" [{} x{}]", item.scenario(), item.num_plays());
        }
        context += "\n";
      }
    }

    const std::string current = app_.scenario_manager().GetCurrentScenarioName();
    if (!current.empty()) {
      auto stats = app_.stats_manager().GetStats(current);
      context += std::format("Full raw stats for current scenario {} ({} runs):\n", current, stats.size());
      const size_t start = stats.size() > 50 ? stats.size() - 50 : 0;
      for (size_t i = start; i < stats.size(); ++i) {
        const auto& row = stats[i];
        const double cm =
            row.cm_per_360 > 0 ? row.cm_per_360 : row.mm_per_360 / 10.0;
        const std::string date =
            absl::FormatTime("%Y-%m-%d %H:%M:%S UTC",
                             absl::FromTimeT(row.epoch_seconds),
                             absl::UTCTimeZone());
        const double shots = row.info.num_shots();
        const double hits = row.info.num_hits();
        const double accuracy = shots > 0 ? 100.0 * hits / shots : 0.0;
        context += std::format(
            "  run={} date={} score={:.3f} cm360={:.3f} hits={:.1f} shots={:.1f} accuracy={:.2f}%\n",
            row.stats_id, date, row.score, cm, hits, shots, accuracy);
      }
    }

    context +=
        "Available app capabilities: start/resume scenarios, inspect scenario definitions, inspect "
        "and edit playlists, inspect all recorded statistics, replay review and visual AI analysis, "
        "scenario analysis, themes/settings/crosshair configuration, bundle save/load, and local "
        "history. This assistant may create scenarios and playlists through validated protobuf JSON.\n";
    return context;
  }

  std::string BuildSystemPrompt() {
    return
        "You are the built-in FpsAimForge AI Coach and application assistant. You have live "
        "application context supplied with every message. Answer directly and accurately. Use "
        "statistics to identify weaknesses and recommend training. Never invent stats, scenarios, "
        "or app capabilities. You may propose actions.\n\n"
        "ACTION PROTOCOL: For a creation/change request, output a normal explanation first, then "
        "one action block. Supported actions are CREATE_SCENARIO, CREATE_PLAYLIST, "
        "ADD_SCENARIO_TO_PLAYLIST, SET_CURRENT_SCENARIO, SET_CURRENT_PLAYLIST, and RUN_CURRENT. "
        "For CREATE_SCENARIO use exactly:\n"
        "ACTION:CREATE_SCENARIO\nNAME:<full bundle/name>\nJSON:<valid ScenarioDef protobuf JSON object>\n"
        "For CREATE_PLAYLIST use exactly:\n"
        "ACTION:CREATE_PLAYLIST\nNAME:<full bundle/name>\nJSON:<valid PlaylistDef protobuf JSON object>\n"
        "For ADD_SCENARIO_TO_PLAYLIST use:\nACTION:ADD_SCENARIO_TO_PLAYLIST\nPLAYLIST:<full name>\nSCENARIO:<full name>\n"
        "For SET_CURRENT_SCENARIO use SCENARIO:<full name>. For SET_CURRENT_PLAYLIST use PLAYLIST:<full name>. "
        "For RUN_CURRENT output ACTION:RUN_CURRENT. Never emit DELETE actions. Creation actions must use a "
        "writable bundle listed in context. Keep generated scenario JSON conservative and valid. Prefer "
        "simple static, linear, strafe, or reference scenarios when the user does not specify a complex type. "
        "If the user asks for a playlist from existing scenarios, use those exact scenario names.\n\n"
        "ScenarioDef JSON follows the application's protobuf field names, for example durationSeconds, "
        "shotType, targetDef, room, staticDef, linearDef, strafeDef, scoreTargets, and description. "
        "PlaylistDef JSON uses description and items, where each item has scenario and numPlays.";
  }

  void SendMessage() {
    if (input_.empty() || request_) return;
    const std::string user_text = input_;
    input_.clear();
    messages_.push_back({true, user_text});
    SaveMemory();

    std::string conversation;
    const size_t begin = messages_.size() > 12 ? messages_.size() - 12 : 0;
    for (size_t i = begin; i < messages_.size(); ++i) {
      conversation += messages_[i].user ? "USER: " : "ASSISTANT: ";
      conversation += messages_[i].text + "\n";
    }
    conversation += "\nLIVE APP CONTEXT:\n" + BuildAppContext(user_text);
    request_ = StartNimChat(BuildSystemPrompt(), conversation);
  }

  void ExecuteAction(const std::string& response) {
    const std::string action = ExtractLineValue(response, "ACTION:");
    if (action.empty()) return;

    if (action == "CREATE_SCENARIO") {
      const std::string name = ExtractLineValue(response, "NAME:");
      const std::string json = ExtractJsonObject(response, "JSON:");
      ScenarioDef def;
      if (name.empty() || json.empty() || !JsonToMessage(json, &def)) {
        messages_.push_back({false, "I could not validate the generated ScenarioDef, so nothing was changed."});
        return;
      }
      std::string target_name = name;
      if (target_name.find(':') == std::string::npos) {
        target_name = app_.bundle_manager().GetDefaultWritableBundleName() + ":" + target_name;
      }
      const std::string scenario_bundle = GetNameInfo(target_name).GetBundleName();
      if (scenario_bundle.empty() || app_.bundle_manager().IsBundleReadonly(scenario_bundle)) {
        messages_.push_back({false, "That scenario targets a readonly or invalid bundle. Nothing was changed."});
        return;
      }
      if (GetNameInfo(target_name).HasDynamicSuffix()) {
        messages_.push_back({false, "That scenario name is reserved for an automatic variation."});
        return;
      }
      if (def.duration_seconds() <= 0) def.set_duration_seconds(45);
      const std::string saved = app_.scenario_manager().SaveScenarioWithUniqueName(target_name, def);
      if (saved.empty()) {
        messages_.push_back({false, "The scenario could not be saved."});
        return;
      }
      app_.bundle_manager().SaveDirtyBundles();
      app_.scenario_manager().SetCurrentScenario(saved);
      messages_.push_back({false, "Created scenario: " + saved});
      return;
    }

    if (action == "CREATE_PLAYLIST") {
      const std::string name = ExtractLineValue(response, "NAME:");
      const std::string json = ExtractJsonObject(response, "JSON:");
      PlaylistDef def;
      if (name.empty() || json.empty() || !JsonToMessage(json, &def)) {
        messages_.push_back({false, "I could not validate the generated PlaylistDef, so nothing was changed."});
        return;
      }
      for (auto& item : *def.mutable_items()) {
        if (item.num_plays() <= 0) item.set_num_plays(1);
      }
      std::string target_name = name;
      if (target_name.find(':') == std::string::npos) {
        target_name = app_.bundle_manager().GetDefaultWritableBundleName() + ":" + target_name;
      }
      const std::string playlist_bundle = GetNameInfo(target_name).GetBundleName();
      if (playlist_bundle.empty() || app_.bundle_manager().IsBundleReadonly(playlist_bundle)) {
        messages_.push_back({false, "That playlist targets a readonly or invalid bundle. Nothing was changed."});
        return;
      }
      if (app_.playlist_manager().GetPlaylist(target_name).has_value()) {
        messages_.push_back({false, "That playlist already exists. I will not overwrite it automatically."});
        return;
      }
      app_.playlist_manager().UpdatePlaylist(target_name, def);
      app_.bundle_manager().SaveDirtyBundles();
      app_.playlist_manager().SetCurrentPlaylist(target_name);
      messages_.push_back({false, "Created playlist: " + target_name});
      return;
    }

    if (action == "ADD_SCENARIO_TO_PLAYLIST") {
      const std::string playlist = ExtractLineValue(response, "PLAYLIST:");
      const std::string scenario = ExtractLineValue(response, "SCENARIO:");
      if (playlist.empty() || scenario.empty()) {
        messages_.push_back({false, "The add-to-playlist action was incomplete, so nothing changed."});
        return;
      }
      app_.playlist_manager().AddScenarioToPlaylist(playlist, scenario);
      app_.bundle_manager().SaveDirtyBundles();
      messages_.push_back({false, std::format("Added {} to {}.", scenario, playlist)});
      return;
    }

    if (action == "SET_CURRENT_SCENARIO") {
      const std::string scenario = ExtractLineValue(response, "SCENARIO:");
      if (!scenario.empty() && app_.scenario_manager().SetCurrentScenario(scenario)) {
        messages_.push_back({false, "Current scenario set to " + scenario});
      }
      return;
    }

    if (action == "SET_CURRENT_PLAYLIST") {
      const std::string playlist = ExtractLineValue(response, "PLAYLIST:");
      if (!playlist.empty()) {
        app_.playlist_manager().SetCurrentPlaylist(playlist);
        messages_.push_back({false, "Current playlist set to " + playlist});
      }
      return;
    }

    if (action == "RUN_CURRENT") {
      app_.state().scenario_run_option = ScenarioRunOption::START_CURRENT;
      ReturnHome();
      return;
    }
  }

  std::vector<ChatMessage> messages_;
  std::string input_;
  std::shared_ptr<NimAnalysisState> request_;
  bool scroll_to_bottom_ = true;
  int current_session_ = 0;         // 0 = most recent
  static constexpr int kMaxSessions = 5;
};

}  // namespace

std::shared_ptr<Screen> CreateAiAssistantScreen() {
  return std::make_shared<AiAssistantScreen>();
}

}  // namespace aim