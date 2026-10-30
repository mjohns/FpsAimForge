#include "theme_editor_screen.h"

#include <format>
#include <optional>

#include "aim/common/field.h"
#include "aim/common/files.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/name_util.h"
#include "aim/common/util.h"
#include "aim/core/application.h"
#include "aim/core/camera.h"
#include "aim/core/settings_manager.h"
#include "aim/graphics/crosshair_renderer.h"
#include "aim/graphics/renderer.h"
#include "aim/i18n/i18n.h"

namespace aim {
namespace {

const float kBackgroundAlpha = 0.6;
constexpr const char* kSolidColorItem = "Solid color";
constexpr const char* kTextureItem = "Texture";

Room GetDefaultRoom() {
  Room r;
  r.mutable_simple_room()->set_height(130);
  r.mutable_simple_room()->set_width(150);
  *r.mutable_camera_position() = ToStoredVec3(0, -200, 0);
  return r;
}

class ThemeEditor {
 public:
  ThemeEditor(const std::string& theme_name,
              Theme current_theme,
              std::vector<std::string> theme_names,
              std::vector<std::string> texture_names,
              Application& app)
      : app_(app),
        current_theme_name_(theme_name),
        original_theme_name_(theme_name),
        current_theme_(current_theme),
        theme_names_(std::move(theme_names)),
        texture_names_(std::move(texture_names)) {
    // See if all the walls have the same texture/scale and set flag if so.
    std::string front_texture_name = current_theme_.front_appearance().texture().texture_name();
    float front_scale = current_theme_.front_appearance().texture().scale();
    bool all_textures_same = true;
    for (const WallAppearance& appearance : {
             current_theme_.front_appearance(),
             current_theme_.side_appearance(),
             current_theme_.roof_appearance(),
             current_theme_.floor_appearance(),
         }) {
      if (!appearance.has_texture()) {
        all_textures_same = false;
        break;
      }
      std::string texture_name = appearance.texture().texture_name();
      float scale = appearance.texture().scale();
      if (texture_name != front_texture_name || scale != front_scale) {
        all_textures_same = false;
        break;
      }
    }
    share_texture_and_scale_ = all_textures_same;
  }

  // Returns whether still editing.
  void Draw() {
    float char_x = ImGui::GetDefaultCharSizeX();
    ImGui::IdGuard cid("CurrentThemeEditor");
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Theme"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(char_x * 20);
    ImGui::InputText("##NameInput", &current_theme_name_);

    bool is_reference = current_theme_.has_reference();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Reference"));
    ImGui::SameLine();
    ImGui::Checkbox("##ReferenceCheck", &is_reference);
    ImGui::SameLine();
    // TODO: Improve help text to be more clear
    ImGui::HelpMarker(
        "Use settings from another theme. Useful when you using per scenario theme settings and "
        "you want to create a \"Default Static\" theme which you can change and have all static "
        "scenarios use the new theme.");

    ImGui::SpacedSeparator();

    if (is_reference) {
      std::string original_reference = current_theme_.reference();

      current_theme_.Clear();
      current_theme_.set_reference(original_reference);

      std::string* reference = current_theme_.mutable_reference();
      if (reference->size() == 0) {
        *reference = theme_names_[0];
      }
      ImGui::SimpleDropdown("ReferenceThemeSelector", reference, theme_names_, char_x * 20);
    } else {
      current_theme_.clear_reference();

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Targets"));
      ImGui::Indent();

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Color"));
      ImGui::SameLine();
      DrawStoredColorEditor("TargetColor", current_theme_.mutable_target_color());

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Ghost color"));
      ImGui::SameLine();
      DrawStoredColorEditor("GhostTargetColor", current_theme_.mutable_ghost_target_color());

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Center color"));
      ImGui::SameLine();
      DrawOptionalStoredColorEditor(
          "CenterTargetColor",
          PROTO_PTR_FIELD(StoredColor, Theme, &current_theme_, center_target_color));

      ImGui::Unindent();

      ImGui::SpacedSeparator();

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Crosshair"));
      ImGui::Indent();

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Color"));
      ImGui::SameLine();
      DrawStoredColorEditor("CrosshairColor", current_theme_.mutable_crosshair()->mutable_color());

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Outline color"));
      ImGui::SameLine();
      DrawStoredColorEditor("OutlineCrosshairColor",
                            current_theme_.mutable_crosshair()->mutable_outline_color());

      ImGui::Unindent();

      ImGui::SpacedSeparator();

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Health bar"));
      HealthBarAppearance& health_bar = *current_theme_.mutable_health_bar();
      ImGui::Indent();

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Health color"));
      ImGui::SameLine();
      DrawStoredColorEditor("HealthColor", health_bar.mutable_health_color());

      ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Health alpha")
                            .set_is_optional()
                            .set_step(0.05, 0.2)
                            .set_min(0)
                            .set_default(0.8)
                            .set_width(char_x * 9),
                        PROTO_FLOAT_FIELD(HealthBarAppearance, &health_bar, health_alpha));

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Background color"));
      ImGui::SameLine();
      DrawStoredColorEditor("HealthBackgroundColor", health_bar.mutable_background_color());

      ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Background alpha")
                            .set_is_optional()
                            .set_step(0.05, 0.2)
                            .set_min(0)
                            .set_default(0.8)
                            .set_width(char_x * 9),
                        PROTO_FLOAT_FIELD(HealthBarAppearance, &health_bar, background_alpha));
      ImGui::Unindent();

      ImGui::SpacedSeparator();

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Walls"));
      ImGui::Indent();

      if (current_theme_.front_appearance().has_texture()) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", Tr("Use same texture on all walls"));
        ImGui::SameLine();
        ImGui::Checkbox("##SameTextureCheck", &share_texture_and_scale_);
      } else {
        share_texture_and_scale_ = false;
      }

      DrawWallAppearanceEditor(
          "Front", current_theme_.mutable_front_appearance(), /*is_front=*/true);
      DrawWallAppearanceEditor("Sides", current_theme_.mutable_side_appearance());
      DrawWallAppearanceEditor("Floor", current_theme_.mutable_floor_appearance());
      DrawWallAppearanceEditor("Roof", current_theme_.mutable_roof_appearance());

      if (share_texture_and_scale_) {
        const WallAppearance& front = current_theme_.front_appearance();
        if (!front.has_texture()) {
          share_texture_and_scale_ = false;
        } else {
          for (WallAppearance* appearance : {
                   current_theme_.mutable_side_appearance(),
                   current_theme_.mutable_floor_appearance(),
                   current_theme_.mutable_roof_appearance(),
               }) {
            auto* tex = appearance->mutable_texture();
            if (front.texture().has_scale()) {
              tex->set_scale(front.texture().scale());
            } else {
              tex->clear_scale();
            }
            tex->set_texture_name(front.texture().texture_name());
          }
        }
      }
      ImGui::Unindent();
    }
  }

  Theme GetThemeToToRender() {
    if (current_theme_.reference().size() > 0) {
      return app_.settings_manager().GetTheme(current_theme_.reference());
    }
    return current_theme_;
  }

  bool Save(std::string* error_message) {
    bool theme_exists = app_.settings_manager().ThemeExists(current_theme_name_);
    bool is_new_theme = !app_.settings_manager().ThemeExists(original_theme_name_);
    bool is_rename = !is_new_theme && current_theme_name_ != original_theme_name_;
    if (is_new_theme || is_rename) {
      if (theme_exists) {
        *error_message = std::format("Theme \"{}\" already exists", current_theme_name_);
        return false;
      }
    }

    if (is_rename) {
      app_.settings_manager().RenameTheme(original_theme_name_, current_theme_name_);
    }

    if (!app_.settings_manager().SaveTheme(current_theme_name_, current_theme_)) {
      *error_message = std::format("Unable to save theme \"{}\"", current_theme_name_);
      return false;
    }
    return true;
  }

 private:
  void DrawStoredColorEditor(const std::string& id, StoredColor* stored_color) {
    float char_x = ImGui::GetDefaultCharSizeX();
    ImGui::InputStoredColor(id, stored_color, char_x);
  }

  void DrawOptionalStoredColorEditor(const std::string& id, PtrField<StoredColor> stored_color) {
    float char_x = ImGui::GetDefaultCharSizeX();
    ImGui::InputOptionalStoredColor(id, stored_color, char_x);
  }

  void DrawWallAppearanceEditor(const std::string& header,
                                WallAppearance* appearance,
                                bool is_front = false) {
    ImGui::IdGuard cid("WallAppearance" + header);

    float char_x = ImGui::GetDefaultCharSizeX();
    ImGui::SetNextItemWidth(char_x * 20);
    ImGui::Text(header);
    ImGui::Indent();
    std::string selected_type = kSolidColorItem;
    if (appearance->has_texture()) {
      selected_type = kTextureItem;
    }

    bool show_texture_options = is_front || !share_texture_and_scale_;

    if (show_texture_options) {
      std::vector<std::string> types = {kSolidColorItem, kTextureItem};
      ImGui::SimpleDropdown("WallTypeDropdown", &selected_type, types, char_x * 20);
    }

    if (selected_type == kSolidColorItem) {
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Color"));
      ImGui::SameLine();
      ImGui::InputStoredColor("##Color", appearance->mutable_color(), char_x);
    }
    if (selected_type == kTextureItem) {
      if (show_texture_options) {
        WallTexture* texture = appearance->mutable_texture();
        ImGui::SimpleDropdown(
            "TextureNameDropdown", texture->mutable_texture_name(), texture_names_, char_x * 20);

        ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Scale")
                              .set_step(0.05, 0.2)
                              .set_is_optional()
                              .set_min(0.05)
                              .set_default(1)
                              .set_width(char_x * 9),
                          PROTO_FLOAT_FIELD(WallTexture, texture, scale));
      }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Mix percent"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(char_x * 9);
    float mix_percent = appearance->mix_percent();
    ImGui::InputFloat("##MixPercent", &mix_percent, 0.02, 0.2, "%.6g");
    if (mix_percent > 0) {
      appearance->set_mix_percent(mix_percent);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Mix color"));
      ImGui::SameLine();
      DrawStoredColorEditor("MixColor", appearance->mutable_mix_color());
    } else {
      appearance->clear_mix_percent();
    }
    ImGui::Unindent();
  }

  Application& app_;
  std::string current_theme_name_;
  const std::string original_theme_name_;
  Theme current_theme_;
  const std::vector<std::string> theme_names_;
  const std::vector<std::string> texture_names_;

  bool share_texture_and_scale_ = false;
};

class ThemeEditorScreen : public UiScreen {
 public:
  explicit ThemeEditorScreen(ThemeEditorOptions options)
      : UiScreen(), default_room_(GetDefaultRoom()), target_manager_(default_room_) {
    texture_names_ = app_.settings_manager().ListTextures();
    LoadThemeList();

    projection_ = GetPerspectiveTransformation(app_.screen_info());
    CameraParams cameraParams(default_room_);
    Camera camera(cameraParams);
    look_at_ = camera.GetLookAt();

    Target t;
    t.radius = 8;
    t.wall_position = glm::vec2(20, 20);
    t.health_seconds = 3;
    t.AddTestDamage();

    Target g = t;
    g.is_ghost = true;
    g.wall_position = glm::vec2(20, -20);
    g.health_seconds = 3;
    g.AddTestDamage();

    target_manager_.AddTarget(t);
    target_manager_.AddTarget(g);

    if (options.selected_theme.size() > 0) {
      OpenExistingTheme(options.selected_theme);
    }
  }

 protected:
  void DrawTopBar() {
    float width = char_x_ * 13.6;
    float middle = app_.screen_info().width / 2.0;
    // ImGui::SetNextWindowBgAlpha();
    ImGui::SetNextWindowPos(ImVec2(middle - width / 2.0, char_x_ / 3.0));
    ImGui::SetNextWindowSize(ImVec2(width, -1));
    ImGui::Begin("TopBar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    if (ImGui::Button(std::format("{} Save", icons::kSave))) {
      std::string error_message = "Could not save theme";
      if (theme_editor_->Save(&error_message)) {
        LoadThemeList();
        BackToThemeList();
      } else {
        notification_popup_.NotifyOpen(error_message);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Back", icons::kArrowBack))) {
      BackToThemeList();
    }

    ImGui::End();
  }

  bool BeginMainWindow(const std::string& name) {
    float width = app_.screen_info().width * 0.6;
    float height = app_.screen_info().height * 0.8;

    ImGui::SetNextWindowPos(ImVec2((app_.screen_info().width - width) / 2.0,
                                   (app_.screen_info().height - height) / 2.0));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    return ImGui::Begin(
        name.c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
  }

  void DrawScreen() override {
    const ScreenInfo& screen = app_.screen_info();
    ImVec2 char_size = ImGui::CalcTextSize("A");
    char_x_ = char_size.x;

    if (!theme_editor_) {
      BeginMainWindow("ThemeList");
      notification_popup_.Draw();
      auto to_delete = delete_confirmation_dialog_.Draw("Delete");
      if (to_delete) {
        app_.settings_manager().DeleteTheme(*to_delete);
        LoadThemeList();
      }
      DrawThemeListEditor();

      ImGui::End();
      return;
    }

    DrawTopBar();
    // TopBar may have deleted theme if navigating back.
    if (!theme_editor_) {
      return;
    }

    float width = char_x_ * 40;
    float height = app_.screen_info().height * 0.95;
    ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
    ImGui::SetNextWindowPos(ImVec2(char_x_ * 0.3, (app_.screen_info().height - height) / 2.0));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("ThemeEditor",
                 nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    notification_popup_.Draw();

    theme_editor_->Draw();

    Crosshair crosshair;
    crosshair.add_layers()->mutable_dot()->set_outline_thickness(2);

    ImGui::End();

    ImGui::SetNextWindowPos(
        ImVec2(app_.screen_info().center.x - 30, app_.screen_info().center.y - 30));
    ImGui::SetNextWindowSize(ImVec2(60, 60));
    ImGui::SetNextWindowBgAlpha(0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin(
        "CrosshairWindow", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    app_.crosshair_renderer().Draw(
        crosshair, 25, theme_editor_->GetThemeToToRender(), app_.screen_info().center);
    ImGui::End();
    ImGui::PopStyleVar();
  }

  void DrawThemeListEditor() {
    ImGui::IdGuard cid("ThemeListEditor");
    ImGui::LoopId loop_id;
    if (ImGui::Button(std::format("{} Back", icons::kArrowBack))) {
      PopSelf();
    }
    ImGui::SpacedSeparator();
    if (ImGui::Button(std::format("{} Theme", icons::kAdd))) {
      OpenNewTheme();
    }
    ImGui::SpacedSeparator();
    ImGui::BeginChild("ThemesListContent");
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Themes"));
    ImGui::SameLine();
    auto folder = app_.file_system().GetUserDataPath("resources/themes");
    if (ImGui::Button(icons::kOpenInNew)) {
      OpenFolderInExplorer(folder);
    }
    ImGui::HelpTooltip(std::format("Open \"{}\"", folder.string()));
    ImGui::Indent();
    ImGui::Spacing();
    for (const std::string& name : theme_names_) {
      auto lid = loop_id.Get();
      if (ImGui::Button(name)) {
        OpenExistingTheme(name);
      }
      const char* popup_id = "ThemeItemMenu";
      if (ImGui::BeginPopupContextItem(popup_id)) {
        if (ImGui::Selectable(Tr("Copy"))) {
          OpenThemeCopy(name);
        }
        if (ImGui::Selectable(Tr("Edit"))) {
          OpenExistingTheme(name);
        }
        if (ImGui::Selectable(Tr("Delete"))) {
          delete_confirmation_dialog_.NotifyOpen(std::format("Delete \"{}\"?", name), name);
        }
        ImGui::EndPopup();
      }
      ImGui::OpenPopupOnItemClick(popup_id, ImGuiPopupFlags_MouseButtonRight);
      ImGui::SameLine();
      if (ImGui::MenuButton()) {
        ImGui::OpenPopup(popup_id);
      }
    }
    ImGui::Unindent();
    ImGui::EndChild();
  }

  void OpenExistingTheme(const std::string& name) {
    Theme current_theme = app_.settings_manager().GetThemeNoReferenceFollow(name);
    theme_editor_ =
        std::make_unique<ThemeEditor>(name, current_theme, theme_names_, texture_names_, app_);
  }

  void OpenThemeCopy(const std::string& name) {
    std::string new_name = MakeUniqueName(name + " Copy", theme_names_);
    Theme current_theme = app_.settings_manager().GetThemeNoReferenceFollow(name);
    theme_editor_ =
        std::make_unique<ThemeEditor>(new_name, current_theme, theme_names_, texture_names_, app_);
  }

  void OpenNewTheme() {
    std::string name = MakeUniqueName("New theme", theme_names_);
    Theme current_theme = app_.settings_manager().GetThemeNoReferenceFollow(name);
    theme_editor_ =
        std::make_unique<ThemeEditor>(name, current_theme, theme_names_, texture_names_, app_);
  }

  void BackToThemeList() {
    theme_editor_ = {};
  }

  void OnEvent(const SDL_Event& event, bool user_is_typing) override {}

  void Render() override {
    if (!theme_editor_) {
      UiScreen::Render();
      return;
    }

    HealthBarSettings health_bar;
    health_bar.set_show(true);
    health_bar.set_size(1.5);
    app_.renderer().RenderScenario(projection_,
                                   default_room_,
                                   ShotType::kTrackingProximity,
                                   theme_editor_->GetThemeToToRender(),
                                   health_bar,
                                   target_manager_.GetTargets(),
                                   look_at_);
  }

 private:
  void LoadThemeList() {
    theme_names_ = app_.settings_manager().ListThemes();
    std::sort(theme_names_.begin(),
              theme_names_.end(),
              [](const std::string& lhs, const std::string& rhs) { return lhs < rhs; });
  }

  Room default_room_;
  TargetManager target_manager_;
  glm::mat4 projection_;
  LookAtInfo look_at_;

  std::vector<std::string> theme_names_;
  std::vector<std::string> texture_names_;

  float char_x_;
  std::unique_ptr<ThemeEditor> theme_editor_;

  ImGui::NotificationPopup notification_popup_{"Notification"};
  ImGui::ConfirmationDialog<std::string> delete_confirmation_dialog_{"DeleteConfirmationDialog"};
};

}  // namespace

std::unique_ptr<UiScreen> CreateThemeEditorScreen(ThemeEditorOptions options) {
  return std::make_unique<ThemeEditorScreen>(options);
}

}  // namespace aim
