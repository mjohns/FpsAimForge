#include "draw_data.h"

#include <cassert>

#include "SDL3/SDL.h"  // IWYU pragma: keep
#include "absl/algorithm/container.h"
#include "aim/common/geometry.h"
#include "aim/common/simple_types.h"
#include "aim/common/util.h"
#include "aim/core/camera.h"
#include "aim/core/target.h"
#include "aim/graphics/textures.h"
#include "aim/proto/settings.pb.h"
#include "aim/proto/theme.pb.h"
#include "glm/gtc/matrix_transform.hpp"  // IWYU pragma: keep
#include "glm/mat4x4.hpp"                // IWYU pragma: keep
                                         //
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/vector_angle.hpp"

namespace aim {
namespace {

constexpr const float kMaxDistance = 1500.0f;
constexpr const float kCenterSphereSizeMultiplier = 0.2f;

glm::mat4 RotateBasicShapeTowardsCamera(const LookAtInfo& look_at,
                                        const glm::vec3& position,
                                        const glm::mat4& transform,
                                        bool rotate_z) {
  if (look_at.position == position) {
    return transform;
  }
  glm::vec3 to_camera = look_at.position - position;
  if (!rotate_z) {
    to_camera.z = 0;
  }
  // The quad/circle types are defined in the x/z plane facing in the negative y direction.
  if (glm::length(to_camera) < 0.001) {
    return transform;
  }
  to_camera = glm::normalize(to_camera);
  glm::vec3 front(0, -1, 0);
  if (to_camera == front) {
    return transform;
  }
  glm::vec3 rotate_axis = glm::normalize(glm::cross(front, to_camera));
  float angle = glm::orientedAngle(front, to_camera, rotate_axis);
  return glm::rotate(transform, angle, rotate_axis);
}

glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, float mix_percent) {
  return a + (mix_percent * (b - a));
}

glm::vec3 GetSolidColor(const WallAppearance& appearance) {
  if (!appearance.has_mix_percent()) {
    return ToVec3(appearance.color());
  }
  return Lerp(ToVec3(appearance.color()), ToVec3(appearance.mix_color()), appearance.mix_percent());
}

// Turns the scenario scene description into normalized and sorted draw data that can be easily
// rendered.
class DrawDataBuilder {
 public:
  explicit DrawDataBuilder(TextureManager* texture_manager) : texture_manager_(texture_manager) {}

  void GetDrawDataForScenario(const glm::mat4& view_projection,
                              const Room& room,
                              bool draw_center,
                              const Theme& theme,
                              const HealthBarSettings& health_bar,
                              const std::vector<Target>& targets,
                              const LookAtInfo& look_at,
                              DrawData* draw_data) {
    draw_data->solid_quads.reserve(6 + targets.size() * 2);
    draw_data->solid_spheres.reserve(targets.size() * 2);
    draw_data->solid_circles.reserve(targets.size() * 2);
    draw_data->solid_cylinders.reserve(targets.size());
    AddDrawRoom(view_projection, theme, room, draw_data);
    AddDrawTargets(view_projection, look_at, theme, health_bar, targets, draw_center, draw_data);

    // Make sure all textured walls are grouped by Texture* so that the samplers can be bound
    // minimally and don't need to be switched back and forth.
    absl::c_sort(draw_data->texture_walls,
                 [](const TextureWallDrawData& lhs, const TextureWallDrawData& rhs) {
                   return lhs.texture < rhs.texture;
                 });
  }

 private:
  void AddDrawRoom(const glm::mat4& view_projection,
                   const Theme& theme,
                   const Room& room,
                   DrawData* draw_data) {
    if (room.has_simple_room()) {
      AddDrawSimpleRoom(view_projection, theme, room.simple_room(), draw_data);
    }
    if (room.has_cylinder_room()) {
      AddDrawCylinderRoom(view_projection, theme, room.cylinder_room(), draw_data);
    }
    if (room.has_barrel_room()) {
      AddDrawBarrelRoom(view_projection, theme, room.barrel_room(), draw_data);
    }
  }

  void AddDrawSimpleRoom(const glm::mat4& view_projection,
                         const Theme& theme,
                         const SimpleRoom& room,
                         DrawData* draw_data) {
    float height = room.height();
    float width = room.width();

    float depth = room.has_depth() ? room.depth() : kMaxDistance;
    bool not_cylinder = false;

    {
      // Front wall
      glm::mat4 model(1.f);
      model = glm::scale(model, glm::vec3(width, 1.0f, height));
      AddDrawWall(view_projection * model,
                  {width, height},
                  theme.front_appearance(),
                  not_cylinder,
                  draw_data);
    }

    if (room.has_depth()) {
      // Back wall
      glm::mat4 model(1.f);
      model = glm::translate(model, glm::vec3(0, -1 * depth, 0));
      model = glm::rotate(model, glm::radians(180.0f), glm::vec3(0, 0, 1));
      model = glm::scale(model, glm::vec3(width, 1.0f, height));
      AddDrawWall(view_projection * model,
                  {width, height},
                  theme.front_appearance(),
                  not_cylinder,
                  draw_data);
    }

    {
      // Floor wall
      glm::mat4 model(1.f);
      model = glm::translate(model, glm::vec3(0, -0.5 * depth, -0.5 * height));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1, 0, 0));
      model = glm::scale(model, glm::vec3(width, 1.0f, depth));
      AddDrawWall(view_projection * model,
                  {width, depth},
                  theme.floor_appearance(),
                  not_cylinder,
                  draw_data);
    }

    {
      // Left wall
      glm::mat4 model(1.f);
      model = glm::translate(model, glm::vec3(-0.5 * width, -0.5 * depth, 0));
      model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0, 0, 1));
      model = glm::scale(model, glm::vec3(depth, 1.0f, height));
      AddDrawWall(view_projection * model,
                  {depth, height},
                  theme.side_appearance(),
                  not_cylinder,
                  draw_data);
    }

    {
      // Right wall
      glm::mat4 model(1.f);
      model = glm::translate(model, glm::vec3(0.5 * width, -0.5 * depth, 0));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      model = glm::scale(model, glm::vec3(depth, 1.0f, height));
      AddDrawWall(view_projection * model,
                  {depth, height},
                  theme.side_appearance(),
                  not_cylinder,
                  draw_data);
    }

    {
      // Top wall
      glm::mat4 model(1.f);
      model = glm::translate(model, glm::vec3(0, -0.5 * depth, 0.5 * height));
      model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1, 0, 0));
      model = glm::scale(model, glm::vec3(width, 1.0f, depth));
      AddDrawWall(view_projection * model,
                  {width, depth},
                  theme.roof_appearance(),
                  not_cylinder,
                  draw_data);
    }
  }

  void AddDrawCylinderRoom(const glm::mat4& view_projection,
                           const Theme& theme,
                           const CylinderRoom& room,
                           DrawData* draw_data) {
    float quad_scale = room.radius() * 2.5;
    float height = room.height();

    {
      // Floor wall
      glm::mat4 model(1.f);
      model = glm::translate(model, glm::vec3(0, 0, -0.505 * height));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1, 0, 0));
      model = glm::scale(model, glm::vec3(quad_scale, 1.0f, quad_scale));
      AddDrawWall(view_projection * model,
                  {quad_scale, quad_scale},
                  theme.floor_appearance(),
                  false,
                  draw_data);
    }

    {
      // Top wall
      glm::mat4 model(1.f);
      model = glm::translate(model, glm::vec3(0, 0, 0.505 * height));
      model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1, 0, 0));
      model = glm::scale(model, glm::vec3(quad_scale, 1.0f, quad_scale));
      AddDrawWall(view_projection * model,
                  {quad_scale, quad_scale},
                  theme.roof_appearance(),
                  false,
                  draw_data);
    }

    if (room.has_side_angle_degrees()) {
      float perimeter = room.radius() * glm::two_pi<float>();
      float width = room.width();
      if (room.width_degrees() > 0) {
        width = (room.width_degrees() / 360.0f) * perimeter;
      }

      float radians = (width / perimeter) * glm::pi<float>();
      glm::vec2 to_rotate(0, room.radius());
      float side_angle_degrees = room.side_angle_degrees();

      {
        // Left
        glm::vec2 left = RotateRadians(to_rotate, radians);
        glm::mat4 model(1.f);
        model = glm::translate(model, glm::vec3(left.x, left.y, 0));
        model = glm::rotate(model, glm::radians(90.0f + side_angle_degrees), glm::vec3(0, 0, 1));
        model = glm::translate(model, glm::vec3(-0.5 * kMaxDistance, 0, 0));
        model = glm::scale(model, glm::vec3(kMaxDistance, 1.0f, height));
        AddDrawWall(view_projection * model,
                    {kMaxDistance, height},
                    theme.side_appearance(),
                    false,
                    draw_data);
      }

      {
        // Right
        glm::vec2 right = RotateRadians(to_rotate, -1 * radians);
        glm::mat4 model(1.f);
        model = glm::translate(model, glm::vec3(right.x, right.y, 0));
        model = glm::rotate(model, glm::radians(-90.0f - side_angle_degrees), glm::vec3(0, 0, 1));
        model = glm::translate(model, glm::vec3(0.5 * kMaxDistance, 0, 0));
        model = glm::scale(model, glm::vec3(kMaxDistance, 1.0f, height));
        AddDrawWall(view_projection * model,
                    {kMaxDistance, height},
                    theme.side_appearance(),
                    false,
                    draw_data);
      }
    }

    {
      glm::mat4 model(1.f);
      model = glm::scale(model, glm::vec3(room.radius(), room.radius(), height));
      AddDrawWall(view_projection * model,
                  {glm::two_pi<float>() * room.radius(), height},
                  theme.front_appearance(),
                  /* is_cylinder_wall= */ true,
                  draw_data);
    }
  }

  void AddDrawBarrelRoom(const glm::mat4& view_projection,
                         const Theme& theme,
                         const BarrelRoom& room,
                         DrawData* draw_data) {
    float quad_scale = room.radius() * 100;

    {
      // Front wall
      glm::mat4 model(1.f);
      model = glm::scale(model, glm::vec3(quad_scale, 1.0f, quad_scale));
      AddDrawWall(view_projection * model,
                  {quad_scale, quad_scale},
                  theme.front_appearance(),
                  false,
                  draw_data);
    }

    {
      glm::mat4 model(1.f);
      // Leave a little gap between the wall and the barrel to prevent any z-fighting.
      model = glm::translate(model, glm::vec3(0, -0.5 * kMaxDistance, 0));
      model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1, 0, 0));
      model = glm::scale(model, glm::vec3(room.radius(), room.radius(), kMaxDistance));
      AddDrawWall(view_projection * model,
                  {glm::two_pi<float>() * room.radius(), kMaxDistance},
                  theme.side_appearance(),
                  /* is_cylinder_wall= */ true,
                  draw_data);
    }
  }

  void AddDrawWall(const glm::mat4& transform,
                   const Wall& wall,
                   const WallAppearance& appearance,
                   bool is_cylinder_wall,
                   DrawData* draw_data) {
    if (appearance.has_texture()) {
      Texture* texture = texture_manager_->GetTexture(appearance.texture().texture_name());
      if (texture == nullptr) {
        // Too spammy to log this error?
        AddDrawWallSolidColor(transform, glm::vec3(0.7), is_cylinder_wall, draw_data);
        return;
      }

      TextureWallDrawData data;
      data.is_cylinder = is_cylinder_wall;
      data.transform = transform;
      data.texture = texture;

      glm::vec2& tex_scale = data.tex_scale;

      float tex_scale_height = 100;
      float tex_scale_width = (texture->width() * tex_scale_height) / (float)texture->height();

      tex_scale.x = wall.width / tex_scale_width;
      tex_scale.y = wall.height / tex_scale_height;

      if (appearance.texture().has_scale()) {
        tex_scale *= appearance.texture().scale();
      }

      glm::vec3 mix_color = ToVec3(appearance.mix_color());
      data.color = glm::vec4(mix_color, appearance.mix_percent());

      draw_data->texture_walls.push_back(data);
      return;
    }
    AddDrawWallSolidColor(transform, GetSolidColor(appearance), is_cylinder_wall, draw_data);
  }

  void AddDrawWallSolidColor(const glm::mat4& transform,
                             const glm::vec3& color,
                             bool is_cylinder_wall,
                             DrawData* draw_data) {
    SolidColorInstanceData data;
    data.transform = transform;
    data.color = glm::vec4(color, 1.0f);
    if (is_cylinder_wall) {
      draw_data->solid_cylinder_walls.push_back(data);
    } else {
      draw_data->solid_quads.push_back(data);
    }
  }

  void AddDrawTargets(const glm::mat4& view_projection,
                      const LookAtInfo& look_at,
                      const Theme& theme,
                      const HealthBarSettings& health_bar_settings,
                      const std::vector<Target>& targets,
                      bool try_to_draw_center,
                      DrawData* draw_data) {
    // Only try to draw center if the theme has a specific color specified.
    try_to_draw_center = try_to_draw_center && theme.has_center_target_color();

    glm::vec3 target_color = theme.has_target_color() ? ToVec3(theme.target_color()) : glm::vec3(0);
    glm::vec3 center_target_color =
        theme.has_center_target_color() ? ToVec3(theme.center_target_color()) : target_color;
    glm::vec3 ghost_target_color =
        theme.has_ghost_target_color() ? ToVec3(theme.ghost_target_color()) : glm::vec3(0.3);

    auto& h = theme.health_bar();
    auto left = ToVec3(h.health_color());
    auto right = ToVec3(h.background_color());
    glm::vec4 health_color(left.r, left.g, left.b, h.has_health_alpha() ? h.health_alpha() : 1.0f);
    glm::vec4 health_background_color(
        right.r, right.g, right.b, h.has_background_alpha() ? h.background_alpha() : 1.0f);

    bool should_draw = false;
    for (const Target& target : targets) {
      if (target.ShouldDraw()) {
        should_draw = true;
      }
    }
    if (!should_draw) {
      return;
    }

    for (const Target& target : targets) {
      if (!target.ShouldDraw()) {
        continue;
      }
      const glm::vec3& color = target.is_ghost ? ghost_target_color : target_color;
      bool should_draw_center = try_to_draw_center && !target.is_ghost;
      if (target.is_pill && target.height >= (target.radius * 2.0f)) {
        Cylinder c;
        c.radius = target.radius;
        c.up = target.pill_up;
        c.height = target.height - (target.radius * 2);
        c.position = target.position;

        if (should_draw_center) {
          AddDrawCylinder(view_projection, c, color, draw_data);
        } else {
          AddDrawCylinderQuad(view_projection, c, color, look_at, draw_data);
        }

        glm::vec3 top = c.position + c.up * (c.height * 0.5f);
        AddDrawSphereCircle(view_projection, top, target.radius, color, look_at, draw_data);

        glm::vec3 bottom = c.position + c.up * (c.height * -0.5f);
        AddDrawSphereCircle(view_projection, bottom, target.radius, color, look_at, draw_data);

        if (should_draw_center) {
          AddDrawCylinderQuadCenter(view_projection, c, center_target_color, look_at, draw_data);
        }

        continue;
      }

      if (should_draw_center) {
        AddDrawSphereCircle(
            view_projection, target.position, target.radius, color, look_at, draw_data);
        AddDrawSphere(view_projection,
                      target.position,
                      target.radius * kCenterSphereSizeMultiplier,
                      center_target_color,
                      draw_data);
      } else {
        // AddDrawSphere(view_projection, target.position, target.radius, color, draw_data);
        AddDrawSphereCircle(
            view_projection, target.position, target.radius, color, look_at, draw_data);
      }

      if (health_bar_settings.show() && target.HasHealth()) {
        bool is_damaged = target.GetHealthPercent() < 1;
        if (!health_bar_settings.only_damaged() || is_damaged) {
          AddDrawHealthBar(view_projection,
                           target.GetHealthPercent(),
                           target.position,
                           target.radius,
                           look_at,
                           health_bar_settings,
                           health_color,
                           health_background_color,
                           draw_data);
        }
      }
    }
  }

  void AddDrawHealthBar(const glm::mat4& view_projection,
                        float health_percent,
                        const glm::vec3& position,
                        float radius,
                        const LookAtInfo& look_at,
                        const HealthBarSettings& health_bar_settings,
                        const glm::vec4& health_color,
                        const glm::vec4& health_background_color,
                        DrawData* draw_data) {
    float width = radius * 2.8;
    if (health_bar_settings.has_size()) {
      width *= health_bar_settings.size();
    }

    float height = width * 0.18;

    float height_above_target = height * 0.35;
    glm::vec3 up = glm::vec3(0, 0, 1);
    glm::vec3 health_bar_center = position + up * (height_above_target + radius + (height / 2.0f));

    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, health_bar_center);

    transform = RotateBasicShapeTowardsCamera(look_at, position, transform, /*rotate_z=*/false);

    transform = glm::scale(transform, glm::vec3(width, 1, height));

    if (health_percent >= 1) {
      // All health
      draw_data->solid_quads.emplace_back();
      SolidColorInstanceData& data = draw_data->solid_quads.back();
      data.transform = view_projection * transform;
      data.color = health_color;
      return;
    }

    if (health_percent <= 0) {
      // All background
      draw_data->solid_quads.emplace_back();
      SolidColorInstanceData& data = draw_data->solid_quads.back();
      data.transform = view_projection * transform;
      data.color = health_background_color;
      return;
    }

    {
      glm::mat4 health_transform =
          glm::translate(transform, glm::vec3((health_percent - 1) * 0.5f, 0.0f, 0.0f));
      health_transform = glm::scale(health_transform, glm::vec3(health_percent, 1, 1));
      draw_data->solid_quads.emplace_back();
      SolidColorInstanceData& data = draw_data->solid_quads.back();
      data.transform = view_projection * health_transform;
      data.color = health_color;
    }

    {
      glm::mat4 background_transform =
          glm::translate(transform, glm::vec3(health_percent * 0.5f, 0.0f, 0.0f));
      background_transform =
          glm::scale(background_transform, glm::vec3((1 - health_percent), 1, 1));
      draw_data->solid_quads.emplace_back();
      SolidColorInstanceData& data = draw_data->solid_quads.back();
      data.transform = view_projection * background_transform;
      data.color = health_background_color;
    }
  }

  void AddDrawSphere(const glm::mat4& view_projection,
                     const glm::vec3& position,
                     float radius,
                     const glm::vec3& color,
                     DrawData* draw_data) {
    draw_data->solid_spheres.emplace_back();
    SolidColorInstanceData& data = draw_data->solid_spheres.back();
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, position);
    transform = glm::scale(transform, glm::vec3(radius));
    data.transform = view_projection * transform;
    data.color = glm::vec4(color, 1.0f);
  }

  void AddDrawSphereCircle(const glm::mat4& view_projection,
                           const glm::vec3& position,
                           float radius,
                           const glm::vec3& color,
                           const LookAtInfo& look_at,
                           DrawData* draw_data) {
    draw_data->solid_circles.emplace_back();
    SolidColorInstanceData& data = draw_data->solid_circles.back();

    glm::mat4 model(1.0f);
    model = glm::translate(model, position);
    model = RotateBasicShapeTowardsCamera(look_at, position, model, /*rotate_z=*/true);
    model = glm::scale(model, glm::vec3(radius, 1.0, radius));

    data.transform = view_projection * model;
    data.color = glm::vec4(color, 1.0f);
  }

  void MaybeRotatePillUp(const Cylinder& c, glm::mat4* model) {
    if (c.up != glm::vec3(0, 0, 1) && c.up != glm::vec3(0, 0, -1)) {
      glm::vec3 up = glm::vec3(0, 0, 1);
      glm::vec3 rotate_axis = glm::normalize(glm::cross(up, c.up));
      float angle = glm::acos(glm::dot(up, c.up));
      *model = glm::rotate(*model, angle, rotate_axis);
    }
  }

  void AddDrawCylinder(const glm::mat4& view_projection,
                       const Cylinder& c,
                       const glm::vec3& color,
                       DrawData* draw_data) {
    draw_data->solid_cylinders.emplace_back();
    SolidColorInstanceData& data = draw_data->solid_cylinders.back();

    glm::mat4 model(1.0f);
    model = glm::translate(model, c.position);
    MaybeRotatePillUp(c, &model);
    model = glm::scale(model, glm::vec3(c.radius, c.radius, c.height));

    data.transform = view_projection * model;
    data.color = glm::vec4(color, 1.0f);
  }

  void AddDrawCylinderQuad(const glm::mat4& view_projection,
                           const Cylinder& c,
                           const glm::vec3& color,
                           const LookAtInfo& look_at,
                           DrawData* draw_data) {
    draw_data->solid_quads.emplace_back();
    SolidColorInstanceData& data = draw_data->solid_quads.back();

    glm::mat4 model(1.0f);
    model = glm::translate(model, c.position);
    MaybeRotatePillUp(c, &model);
    model = RotateBasicShapeTowardsCamera(look_at, c.position, model, /*rotate_z=*/false);
    model = glm::scale(model, glm::vec3(c.radius * 2, 1.0, c.height));

    data.transform = view_projection * model;
    data.color = glm::vec4(color, 1.0f);
  }

  void AddDrawCylinderQuadCenter(const glm::mat4& view_projection,
                                 const Cylinder& c,
                                 const glm::vec3& color,
                                 const LookAtInfo& look_at,
                                 DrawData* draw_data) {
    draw_data->solid_quads.emplace_back();
    SolidColorInstanceData& data = draw_data->solid_quads.back();

    // Draw a quad on the outside of the cylinder.

    glm::mat4 model(1.0f);
    model = glm::translate(model, c.position);
    MaybeRotatePillUp(c, &model);
    model = RotateBasicShapeTowardsCamera(look_at, c.position, model, /*rotate_z=*/false);
    // Translate to outside
    model = glm::translate(model, glm::vec3(0, -1.0f * (c.radius + 0.05), 0));
    model =
        glm::scale(model, glm::vec3(c.radius * 2.0f * kCenterSphereSizeMultiplier, 1.0, c.height));

    data.transform = view_projection * model;
    data.color = glm::vec4(color, 1.0f);
  }

  TextureManager* texture_manager_;
};

}  // namespace

// Turns the scenario scene description into normalized and sorted draw data that can be easily
// rendered.
void GetDrawDataForScenario(const glm::mat4& view_projection,
                            const Room& room,
                            bool draw_center,
                            const Theme& theme,
                            const HealthBarSettings& health_bar,
                            const std::vector<Target>& targets,
                            const LookAtInfo& look_at,
                            TextureManager& texture_manager,
                            DrawData* draw_data) {
  DrawDataBuilder builder(&texture_manager);
  builder.GetDrawDataForScenario(
      view_projection, room, draw_center, theme, health_bar, targets, look_at, draw_data);
}

}  // namespace aim
