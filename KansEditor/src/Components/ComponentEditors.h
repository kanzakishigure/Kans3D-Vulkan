#pragma once
#include <imgui.h>
#include "ComponentRegistry.h"
#include "Kans3D/Geometry/Shapes.h"

// Drawers of the built-in components, registered in ComponentEditors.cpp.
namespace Kans::ComponentEditors
{
    inline constexpr ImVec4 ErrorColor {0.95f, 0.38f, 0.35f, 1.0f};

    void DrawTransform(Entity entity, ComponentUI& ui);
    void DrawCamera(Entity entity, ComponentUI& ui);
    void DrawSprite(Entity entity, ComponentUI& ui);
    void DrawDirectionalLight(Entity entity, ComponentUI& ui);
    void DrawPointLight(Entity entity, ComponentUI& ui);
    void DrawScript(Entity entity, ComponentUI& ui);
    void DrawPrefabInstance(Entity root, ComponentUI& ui);
    void DrawMesh(Entity entity, ComponentUI& ui);
    void DrawEditableMesh(Entity entity, ComponentUI& ui);
    void DrawAnimator(Entity entity, ComponentUI& ui);
    const char* ShapeName(ShapeType type);
    // Adds a generated shape resting on the ground plane at the entity's position.
    void CreateShape(Entity entity, ShapeType type);
} // namespace Kans::ComponentEditors
