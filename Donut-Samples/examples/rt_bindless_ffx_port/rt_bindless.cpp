/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <donut/app/ApplicationBase.h>
#include <donut/app/Camera.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/Scene.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/BindingCache.h>
#include <donut/engine/View.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/render/BloomPass.h>
#include <donut/render/ToneMappingPasses.h>
#include <donut/render/SkyPass.h>
#include <donut/render/SsaoPass.h>
#include <donut/app/DeviceManager.h>
#include <donut/app/imgui_renderer.h>
#include <donut/ecs/Attribute.h>
#include <donut/ecs/Stage.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/math/math.h>
#include <nvrhi/utils.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

using namespace donut;
using namespace donut::math;

#include <donut/shaders/sky_cb.h>

#include "lighting_cb.h"
#include "shadow_cb.h"

static const char* g_WindowTitle = "Threshold Engine: Bindless Ray Tracing";

struct FrameConstants
{
    uint32_t frameIndex = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    uint32_t pad2 = 0;
};

struct CompositeConstants
{
    float strength = 0.7f;
    float pad0 = 0.f;
    float pad1 = 0.f;
    float pad2 = 0.f;
};

class StagePanelUI : public app::ImGui_Renderer
{
public:
    StagePanelUI(app::DeviceManager* deviceManager, ecs::Stage& stage)
        : ImGui_Renderer(deviceManager)
        , m_Stage(stage)
    {
        ImGui::GetIO().IniFilename = nullptr;

        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.52f, 0.24f, 1.00f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.66f, 0.31f, 1.00f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.14f, 0.42f, 0.19f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.52f, 0.24f, 1.00f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.66f, 0.31f, 1.00f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.14f, 0.42f, 0.19f, 1.00f);

        for (ecs::Prim prim : m_Stage.GetPrims())
        {
            if (prim.HasAttribute<ecs::TransformAttribute>())
            {
                m_Selected = prim.GetEntity();
                break;
            }
        }
    }

    void SetCamera(const dm::float4x4& viewProj, const dm::float3& cameraPos)
    {
        m_ViewProj = viewProj;
        m_CameraPos = cameraPos;
        m_HasCamera = true;
    }

    bool MouseButtonUpdate(int button, int action, int mods) override
    {
        bool consumed = app::ImGui_Renderer::MouseButtonUpdate(button, action, mods);
        if (button == 0)
        {
            if (action == 1)
            {
                if (m_GizmoHover != 0)
                    consumed = true;
            }
            else if (action == 0)
            {
                if (m_GizmoActive != 0)
                {
                    m_GizmoActive = 0;
                    consumed = true;
                }
            }
        }
        return consumed || (m_GizmoActive != 0);
    }

    bool MousePosUpdate(double xpos, double ypos) override
    {
        bool consumed = app::ImGui_Renderer::MousePosUpdate(xpos, ypos);
        return consumed || (m_GizmoActive != 0);
    }

protected:
    void buildUI() override
    {
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 display = io.DisplaySize;
        const float topBarHeight = 34.f;
        const float panelWidth = 320.f;
        const float panelHeight = display.y - topBarHeight;
        const float hierarchyHeight = panelHeight * 0.42f;
        const float panelX = display.x - panelWidth;

        const ImGuiWindowFlags panelFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(display.x, topBarHeight));
        ImGui::Begin("##EditorTopBar", nullptr, panelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::TextUnformatted("Engine Editor");
        ImGui::SameLine();
        ImGui::TextDisabled("|  %s  |  %zu prims  |  %.1f FPS", m_Stage.GetName().c_str(), m_Stage.GetPrimCount(), io.Framerate);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(panelX, topBarHeight));
        ImGui::SetNextWindowSize(ImVec2(panelWidth, hierarchyHeight));
        ImGui::Begin("Hierarchy", nullptr, panelFlags);
        for (ecs::Prim prim : m_Stage.GetPrims())
        {
            if (!prim.IsValid())
                continue;

            std::string label;
            if (prim.HasAttribute<ecs::NameAttribute>())
                label = prim.GetAttribute<ecs::NameAttribute>().value;
            else
                label = "Prim " + std::to_string(uint32_t(prim.GetEntity()));

            const bool selected = (m_Selected == prim.GetEntity());
            ImGui::PushID(int(uint32_t(prim.GetEntity())));
            if (ImGui::Selectable(label.c_str(), selected))
                m_Selected = prim.GetEntity();
            ImGui::PopID();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(panelX, topBarHeight + hierarchyHeight));
        ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight - hierarchyHeight));
        ImGui::Begin("Details", nullptr, panelFlags);
        ecs::Prim selectedPrim;
        if (m_Selected != entt::null)
            selectedPrim = ecs::Prim(m_Stage.GetRegistry(), m_Selected);

        if (!selectedPrim.IsValid())
        {
            ImGui::TextDisabled("Select a Prim in the Hierarchy.");
        }
        else
        {
            DrawDetails(selectedPrim);
        }
        ImGui::End();

        ImGui::GetForegroundDrawList()->AddText(
            ImVec2(10.f, topBarHeight + 8.f),
            IM_COL32(210, 210, 210, 200), "Viewport");

        DrawGizmo(display);
    }

private:
    static bool InputStringField(const char* label, std::string& value)
    {
        char buffer[512];
        const size_t length = std::min(value.size(), sizeof(buffer) - 1);
        memcpy(buffer, value.data(), length);
        buffer[length] = '\0';
        if (ImGui::InputText(label, buffer, sizeof(buffer)))
        {
            value = buffer;
            return true;
        }
        return false;
    }

    template<typename T, typename Editor>
    void DrawAttributeSection(ecs::Prim prim, const char* name, Editor editor)
    {
        if (!prim.HasAttribute<T>())
            return;

        ImGui::PushID(name);
        if (ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen))
        {
            T& attribute = prim.GetAttribute<T>();
            editor(attribute);
            ImGui::Spacing();
            if (ImGui::Button("Remove Attribute"))
                ecs::AttributeRegistry::Get().Remove(prim.GetRegistry(), prim.GetEntity(), name);
        }
        ImGui::PopID();
    }

    void DrawDetails(ecs::Prim prim)
    {
        entt::entity entity = prim.GetEntity();

        ImGui::Text("Prim: %s", prim.HasAttribute<ecs::NameAttribute>()
            ? prim.GetAttribute<ecs::NameAttribute>().value.c_str()
            : "(unnamed)");
        ImGui::SameLine();
        ImGui::TextDisabled("#%u", uint32_t(entity));
        ImGui::Separator();

        if (ImGui::Button("+ Add Attribute"))
            ImGui::OpenPopup("AddAttributePopup");

        if (ImGui::BeginPopup("AddAttributePopup"))
        {
            bool any = false;
            for (const std::string& name : ecs::AttributeRegistry::Get().GetAttributeNames())
            {
                if (ecs::AttributeRegistry::Get().Has(m_Stage.GetRegistry(), entity, name))
                    continue;
                any = true;
                if (ImGui::MenuItem(name.c_str()))
                    ecs::AttributeRegistry::Get().AddDefault(m_Stage.GetRegistry(), entity, name);
            }
            if (!any)
                ImGui::TextDisabled("All attributes present");
            ImGui::EndPopup();
        }

        ImGui::Separator();

        DrawAttributeSection<ecs::NameAttribute>(prim, "Name", [](ecs::NameAttribute& a) { InputStringField("Name", a.value); });
        DrawAttributeSection<ecs::TagAttribute>(prim, "Tag", [](ecs::TagAttribute& a) { InputStringField("Tag", a.value); });
        DrawAttributeSection<ecs::TransformAttribute>(prim, "Transform", [](ecs::TransformAttribute& a)
        {
            ImGui::DragFloat3("Translation", &a.translation.x, 0.05f);
            ImGui::DragFloat4("Rotation", &a.rotation.x, 0.01f);
            ImGui::DragFloat3("Scaling", &a.scaling.x, 0.05f);
        });
        DrawAttributeSection<ecs::CameraAttribute>(prim, "Camera", [](ecs::CameraAttribute& a)
        {
            ImGui::DragFloat3("Position", &a.position.x, 0.05f);
            ImGui::DragFloat3("Target", &a.target.x, 0.05f);
            ImGui::DragFloat3("Up", &a.up.x, 0.05f);
            ImGui::DragFloat("FOV (deg)", &a.fovDegrees, 0.5f, 1.f, 179.f);
            ImGui::DragFloat("Near", &a.zNear, 0.001f, 0.001f, 10.f);
            ImGui::DragFloat("Far", &a.zFar, 1.f, 1.f, 100000.f);
        });
        DrawAttributeSection<ecs::DirectionalLightAttribute>(prim, "DirectionalLight", [](ecs::DirectionalLightAttribute& a)
        {
            ImGui::DragFloat3("Direction", &a.direction.x, 0.01f);
            ImGui::DragFloat3("Color", &a.color.x, 0.01f);
            ImGui::DragFloat("Irradiance", &a.irradiance, 0.05f, 0.f, 100.f);
            ImGui::DragFloat("Angular Size", &a.angularSize, 0.01f, 0.1f, 90.f);
        });
        DrawAttributeSection<ecs::SkyAttribute>(prim, "Sky", [](ecs::SkyAttribute& a)
        {
            ImGui::DragFloat3("Sky Color", &a.skyColor.x, 0.01f);
            ImGui::DragFloat3("Horizon Color", &a.horizonColor.x, 0.01f);
            ImGui::DragFloat3("Ground Color", &a.groundColor.x, 0.01f);
            ImGui::DragFloat("Brightness", &a.brightness, 0.005f, 0.f, 10.f);
            ImGui::DragFloat("Horizon Size", &a.horizonSize, 0.5f, 0.f, 90.f);
            ImGui::DragFloat("Glow Size", &a.glowSize, 0.1f, 0.f, 90.f);
            ImGui::DragFloat("Glow Intensity", &a.glowIntensity, 0.01f, 0.f, 1.f);
            ImGui::DragFloat("Glow Sharpness", &a.glowSharpness, 0.1f, 1.f, 10.f);
        });
        DrawAttributeSection<ecs::ModelAttribute>(prim, "Model", [](ecs::ModelAttribute& a) { InputStringField("Path", a.path); });
    }

    static float DistancePointSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b)
    {
        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        const float lengthSquared = abx * abx + aby * aby;
        float t = 0.f;
        if (lengthSquared > 1e-6f)
            t = std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / lengthSquared, 0.f, 1.f);
        const float cx = a.x + abx * t;
        const float cy = a.y + aby * t;
        const float dx = p.x - cx;
        const float dy = p.y - cy;
        return std::sqrt(dx * dx + dy * dy);
    }

    static ImU32 AxisColor(int axis, bool highlighted)
    {
        if (highlighted)
            return ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
        if (axis == 0)
            return ImGui::ColorConvertFloat4ToU32(ImVec4(0.90f, 0.25f, 0.25f, 1.0f));
        if (axis == 1)
            return ImGui::ColorConvertFloat4ToU32(ImVec4(0.30f, 0.85f, 0.30f, 1.0f));
        return ImGui::ColorConvertFloat4ToU32(ImVec4(0.30f, 0.45f, 0.95f, 1.0f));
    }

    static dm::quaternion<float> MultiplyQuat(const dm::quaternion<float>& a, const dm::quaternion<float>& b)
    {
        return dm::quaternion<float>(
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w);
    }

    bool ProjectPoint(const dm::float3& world, ImVec2& out, const ImVec2& display) const
    {
        if (!m_HasCamera)
            return false;
        dm::float4 clip = dm::float4(world, 1.f) * m_ViewProj;
        if (clip.w <= 1e-4f)
            return false;
        dm::float3 ndc = dm::float3(clip.x, clip.y, clip.z) / clip.w;
        out = ImVec2((ndc.x * 0.5f + 0.5f) * display.x, (1.f - (ndc.y * 0.5f + 0.5f)) * display.y);
        return true;
    }

    void DrawGizmo(const ImVec2& display)
    {
        if (!m_HasCamera || m_Selected == entt::null)
        {
            m_GizmoHover = 0;
            return;
        }

        ecs::Prim prim(m_Stage.GetRegistry(), m_Selected);
        if (!prim.IsValid() || !prim.HasAttribute<ecs::TransformAttribute>())
        {
            m_GizmoHover = 0;
            return;
        }

        ecs::TransformAttribute& transform = prim.GetAttribute<ecs::TransformAttribute>();

        ImVec2 origin;
        if (!ProjectPoint(transform.translation, origin, display))
        {
            m_GizmoHover = 0;
            return;
        }

        const float distanceToCamera = dm::length(transform.translation - m_CameraPos);
        const float worldSize = std::max(0.25f, distanceToCamera * 0.12f);

        const dm::float3 axes[3] = { dm::float3(1.f, 0.f, 0.f), dm::float3(0.f, 1.f, 0.f), dm::float3(0.f, 0.f, 1.f) };

        ImVec2 ends[3];
        bool endValid[3] = { false, false, false };
        for (int i = 0; i < 3; i++)
        {
            ImVec2 projected;
            if (ProjectPoint(transform.translation + axes[i] * worldSize, projected, display))
            {
                ends[i] = projected;
                endValid[i] = true;
            }
        }

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        ImDrawList* drawList = ImGui::GetForegroundDrawList();

        int hover = 0;

        for (int i = 0; i < 3; i++)
        {
            if (!endValid[i])
                continue;
            const float sx = origin.x + (ends[i].x - origin.x) * 1.15f;
            const float sy = origin.y + (ends[i].y - origin.y) * 1.15f;
            const float dx = mouse.x - sx;
            const float dy = mouse.y - sy;
            if (dx * dx + dy * dy < 100.f)
                hover = 7 + i;
        }

        if (hover == 0)
        {
            for (int i = 0; i < 3; i++)
            {
                if (endValid[i] && DistancePointSegment(mouse, origin, ends[i]) < 7.f)
                    hover = 1 + i;
            }
        }

        if (hover == 0)
        {
            for (int i = 0; i < 3; i++)
            {
                const dm::float3 u = axes[(i + 1) % 3];
                const dm::float3 v = axes[(i + 2) % 3];
                ImVec2 previous;
                bool hasPrevious = false;
                float best = 1e9f;
                const int segments = 48;
                for (int k = 0; k <= segments; k++)
                {
                    const float angle = (float(k) / float(segments)) * 2.f * dm::PI_f;
                    ImVec2 projected;
                    if (!ProjectPoint(transform.translation + (u * std::cos(angle) + v * std::sin(angle)) * (worldSize * 0.78f), projected, display))
                    {
                        hasPrevious = false;
                        continue;
                    }
                    if (hasPrevious)
                        best = std::min(best, DistancePointSegment(mouse, previous, projected));
                    previous = projected;
                    hasPrevious = true;
                }
                if (best < 7.f)
                {
                    hover = 4 + i;
                    break;
                }
            }
        }

        m_GizmoHover = hover;

        for (int i = 0; i < 3; i++)
        {
            const dm::float3 u = axes[(i + 1) % 3];
            const dm::float3 v = axes[(i + 2) % 3];
            const bool highlighted = (m_GizmoHover == 4 + i || m_GizmoActive == 4 + i);
            ImVec2 previous;
            bool hasPrevious = false;
            const int segments = 48;
            for (int k = 0; k <= segments; k++)
            {
                const float angle = (float(k) / float(segments)) * 2.f * dm::PI_f;
                ImVec2 projected;
                if (!ProjectPoint(transform.translation + (u * std::cos(angle) + v * std::sin(angle)) * (worldSize * 0.78f), projected, display))
                {
                    hasPrevious = false;
                    continue;
                }
                if (hasPrevious)
                    drawList->AddLine(previous, projected, AxisColor(i, highlighted), highlighted ? 2.5f : 1.5f);
                previous = projected;
                hasPrevious = true;
            }
        }

        for (int i = 0; i < 3; i++)
        {
            if (!endValid[i])
                continue;

            const bool translateHighlighted = (m_GizmoHover == 1 + i || m_GizmoActive == 1 + i);
            const bool scaleHighlighted = (m_GizmoHover == 7 + i || m_GizmoActive == 7 + i);

            drawList->AddLine(origin, ends[i], AxisColor(i, translateHighlighted), translateHighlighted ? 3.f : 2.f);

            const float dirX = ends[i].x - origin.x;
            const float dirY = ends[i].y - origin.y;
            const float dirLength = std::sqrt(dirX * dirX + dirY * dirY);
            if (dirLength > 1.f)
            {
                const float dx = dirX / dirLength;
                const float dy = dirY / dirLength;
                const ImVec2 base(ends[i].x - dx * 12.f, ends[i].y - dy * 12.f);
                drawList->AddTriangleFilled(
                    ends[i],
                    ImVec2(base.x - dy * 6.f, base.y + dx * 6.f),
                    ImVec2(base.x + dy * 6.f, base.y - dx * 6.f),
                    AxisColor(i, translateHighlighted));
            }

            const float sx = origin.x + dirX * 1.15f;
            const float sy = origin.y + dirY * 1.15f;
            drawList->AddRectFilled(ImVec2(sx - 5.f, sy - 5.f), ImVec2(sx + 5.f, sy + 5.f), AxisColor(i, scaleHighlighted));
        }

        if (hover != 0 && m_GizmoActive == 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_GizmoActive = hover;
            m_DragStartMouse = mouse;
            m_DragStartTransform = transform;
            m_DragEntity = m_Selected;
        }

        if (m_GizmoActive == 0 || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            return;

        const int mode = (m_GizmoActive - 1) / 3;
        const int axisIndex = (m_GizmoActive - 1) % 3;
        const float deltaX = mouse.x - m_DragStartMouse.x;
        const float deltaY = mouse.y - m_DragStartMouse.y;

        if (mode == 1)
        {
            const float startAngle = std::atan2(m_DragStartMouse.y - origin.y, m_DragStartMouse.x - origin.x);
            const float currentAngle = std::atan2(mouse.y - origin.y, mouse.x - origin.x);
            float angleDelta = currentAngle - startAngle;
            while (angleDelta > dm::PI_f)
                angleDelta -= 2.f * dm::PI_f;
            while (angleDelta < -dm::PI_f)
                angleDelta += 2.f * dm::PI_f;

            const dm::quaternion<float> startRotation = dm::quaternion<float>::fromXYZW(m_DragStartTransform.rotation);
            const dm::quaternion<float> deltaRotation = dm::rotationQuat(axes[axisIndex], angleDelta);
            const dm::quaternion<float> result = dm::normalize(MultiplyQuat(deltaRotation, startRotation));
            transform.rotation = dm::float4(result.x, result.y, result.z, result.w);
            return;
        }

        if (!endValid[axisIndex])
            return;

        const float dirX = ends[axisIndex].x - origin.x;
        const float dirY = ends[axisIndex].y - origin.y;
        const float dirLength = std::sqrt(dirX * dirX + dirY * dirY);
        if (dirLength <= 1.f)
            return;

        const float projectedDelta = (deltaX * dirX + deltaY * dirY) / dirLength;

        if (mode == 0)
        {
            const float worldPerPixel = worldSize / dirLength;
            transform.translation[axisIndex] = m_DragStartTransform.translation[axisIndex] + projectedDelta * worldPerPixel;
        }
        else
        {
            const float factor = std::max(0.05f, 1.f + projectedDelta / dirLength);
            transform.scaling[axisIndex] = std::max(0.01f, m_DragStartTransform.scaling[axisIndex] * factor);
        }
    }

    ecs::Stage& m_Stage;
    entt::entity m_Selected = entt::null;

    dm::float4x4 m_ViewProj;
    dm::float3 m_CameraPos = dm::float3(0.f);
    bool m_HasCamera = false;

    int m_GizmoHover = 0;
    int m_GizmoActive = 0;
    ImVec2 m_DragStartMouse = ImVec2(0.f, 0.f);
    ecs::TransformAttribute m_DragStartTransform;
    entt::entity m_DragEntity = entt::null;
};

class BindlessRayTracing : public app::ApplicationBase
{
private:
	std::shared_ptr<vfs::RootFileSystem> m_RootFS;

    nvrhi::ShaderLibraryHandle m_ShaderLibrary;
    nvrhi::rt::PipelineHandle m_RayPipeline;
    nvrhi::rt::ShaderTableHandle m_ShaderTable;
    nvrhi::ShaderHandle m_ComputeShader;
    nvrhi::ComputePipelineHandle m_ComputePipeline;
    nvrhi::CommandListHandle m_CommandList;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingLayoutHandle m_BindlessLayout;

    nvrhi::rt::AccelStructHandle m_TopLevelAS;

    nvrhi::BufferHandle m_ConstantBuffer;
    nvrhi::BufferHandle m_SkyCB;
    nvrhi::BufferHandle m_FrameCB;
    render::SkyParameters m_SkyParams;

    nvrhi::TextureHandle m_DepthHistory[2];
    nvrhi::TextureHandle m_Moments[2];
    nvrhi::BindingSetHandle m_BindingSet;
    int m_HistoryIndex = 0;
    bool m_HasPrevViewProj = false;
    dm::float4x4 m_PrevViewProj;
    dm::float4x4 m_PrevClipToView;

    std::shared_ptr<engine::ShaderFactory> m_ShaderFactory;
    std::shared_ptr<engine::DescriptorTableManager> m_DescriptorTable;
    std::unique_ptr<engine::Scene> m_Scene;
    nvrhi::TextureHandle m_ColorBuffer;
    nvrhi::TextureHandle m_LdrColor;
    nvrhi::TextureHandle m_GBufferDepth;
    nvrhi::TextureHandle m_GBufferNormal;
    nvrhi::TextureHandle m_AmbientOcclusion;
    nvrhi::TextureHandle m_DirectBuffer;
    nvrhi::TextureHandle m_VisBuffer;
    nvrhi::TextureHandle m_GeomNormal;
    nvrhi::TextureHandle m_ReprojMap;
    nvrhi::TextureHandle m_ShadowMask;
    nvrhi::TextureHandle m_TileMeta;
    nvrhi::TextureHandle m_DenoiseBuffer[3];
    std::unique_ptr<render::SsaoPass> m_SsaoPass;
    render::SsaoParameters m_SsaoParams;
    nvrhi::BufferHandle m_ReprojectCB;
    nvrhi::BufferHandle m_BitpackCB;
    nvrhi::BufferHandle m_TemporalCB;
    nvrhi::BufferHandle m_AtrousCB[3];
    nvrhi::BindingLayoutHandle m_ReprojectLayout;
    nvrhi::ShaderHandle m_ReprojectShader;
    nvrhi::ComputePipelineHandle m_ReprojectPipeline;
    nvrhi::BindingSetHandle m_ReprojectBindingSet[2];
    nvrhi::BindingLayoutHandle m_BitpackLayout;
    nvrhi::ShaderHandle m_BitpackShader;
    nvrhi::ComputePipelineHandle m_BitpackPipeline;
    nvrhi::BindingSetHandle m_BitpackBindingSet;
    nvrhi::BindingLayoutHandle m_TemporalLayout;
    nvrhi::ShaderHandle m_TemporalShader;
    nvrhi::ComputePipelineHandle m_TemporalPipeline;
    nvrhi::BindingSetHandle m_TemporalBindingSet[2];
    nvrhi::BindingLayoutHandle m_DenoiseLayout;
    nvrhi::ShaderHandle m_DenoiseShader;
    nvrhi::ComputePipelineHandle m_DenoisePipeline;
    nvrhi::BindingSetHandle m_DenoiseBindingSet[3];
    nvrhi::BindingLayoutHandle m_CompositeLayout;
    nvrhi::ShaderHandle m_CompositeShader;
    nvrhi::ComputePipelineHandle m_CompositePipeline;
    nvrhi::BufferHandle m_CompositeCB;
    nvrhi::BindingSetHandle m_CompositeBindingSet;
    std::shared_ptr<engine::FramebufferFactory> m_HdrFramebuffer;
    std::shared_ptr<engine::FramebufferFactory> m_LdrFramebuffer;
    std::unique_ptr<render::BloomPass> m_BloomPass;
    std::unique_ptr<render::ToneMappingPass> m_ToneMappingPass;
    app::FirstPersonCamera m_Camera;
    engine::PlanarView m_View;
    std::shared_ptr<engine::DirectionalLight> m_SunLight;
    std::unique_ptr<engine::BindingCache> m_BindingCache;

    std::unique_ptr<ecs::Stage> m_EcsStage;
    std::shared_ptr<StagePanelUI> m_StageUI;
    bool m_StageUIAdded = false;

    bool m_EnableAnimations = false;
    float m_WallclockTime = 0.f;

public:
    using ApplicationBase::ApplicationBase;

    ~BindlessRayTracing() override
    {
        RemoveUIRenderPass();
    }

    void AddUIRenderPass()
    {
        if (m_StageUI && !m_StageUIAdded)
        {
            GetDeviceManager()->AddRenderPassToBack(m_StageUI.get());
            m_StageUIAdded = true;
        }
    }

    void RemoveUIRenderPass()
    {
        if (m_StageUI && m_StageUIAdded)
        {
            GetDeviceManager()->RemoveRenderPass(m_StageUI.get());
            m_StageUIAdded = false;
        }
    }

    bool Init(bool useRayQuery, const std::filesystem::path& sceneFileName, const std::filesystem::path& stageFileName)
    {
        std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
        std::filesystem::path appShaderPath = app::GetDirectoryWithExecutable() / "shaders/rt_bindless" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
        
		m_RootFS = std::make_shared<vfs::RootFileSystem>();
		m_RootFS->mount("/shaders/donut", frameworkShaderPath);
		m_RootFS->mount("/shaders/app", appShaderPath);

		m_ShaderFactory = std::make_shared<engine::ShaderFactory>(GetDevice(), m_RootFS, "/shaders");
		m_CommonPasses = std::make_shared<engine::CommonRenderPasses>(GetDevice(), m_ShaderFactory);
        m_BindingCache = std::make_unique<engine::BindingCache>(GetDevice());

        nvrhi::BindingLayoutDesc compositeLayoutDesc;
        compositeLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        compositeLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        m_CompositeLayout = GetDevice()->createBindingLayout(compositeLayoutDesc);
        m_CompositeShader = m_ShaderFactory->CreateShader("app/ssao_composite.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
        if (!m_CompositeShader)
            return false;

        nvrhi::ComputePipelineDesc compositePipelineDesc;
        compositePipelineDesc.bindingLayouts = { m_CompositeLayout };
        compositePipelineDesc.CS = m_CompositeShader;
        m_CompositePipeline = GetDevice()->createComputePipeline(compositePipelineDesc);
        if (!m_CompositePipeline)
            return false;

        m_CompositeCB = GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(CompositeConstants), "CompositeConstants", engine::c_MaxRenderPassConstantBufferVersions));

        m_ReprojectCB = GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(ShadowDenoiseConstants), "ShadowReprojectConstants", engine::c_MaxRenderPassConstantBufferVersions));

        m_BitpackCB = GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(ShadowDenoiseConstants), "ShadowBitpackConstants", engine::c_MaxRenderPassConstantBufferVersions));

        m_TemporalCB = GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(ShadowDenoiseConstants), "ShadowTemporalConstants", engine::c_MaxRenderPassConstantBufferVersions));

        for (int i = 0; i < 3; i++)
        {
            m_AtrousCB[i] = GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
                sizeof(ShadowDenoiseConstants), "ShadowAtrousConstants", engine::c_MaxRenderPassConstantBufferVersions));
        }

        nvrhi::BindingLayoutDesc reprojectLayoutDesc;
        reprojectLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        reprojectLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1)
        };
        m_ReprojectLayout = GetDevice()->createBindingLayout(reprojectLayoutDesc);
        m_ReprojectShader = m_ShaderFactory->CreateShader("app/shadow_reproject.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
        if (!m_ReprojectShader)
            return false;

        nvrhi::ComputePipelineDesc reprojectPipelineDesc;
        reprojectPipelineDesc.bindingLayouts = { m_ReprojectLayout };
        reprojectPipelineDesc.CS = m_ReprojectShader;
        m_ReprojectPipeline = GetDevice()->createComputePipeline(reprojectPipelineDesc);
        if (!m_ReprojectPipeline)
            return false;

        nvrhi::BindingLayoutDesc bitpackLayoutDesc;
        bitpackLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        bitpackLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        m_BitpackLayout = GetDevice()->createBindingLayout(bitpackLayoutDesc);
        m_BitpackShader = m_ShaderFactory->CreateShader("app/shadow_bitpack.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
        if (!m_BitpackShader)
            return false;

        nvrhi::ComputePipelineDesc bitpackPipelineDesc;
        bitpackPipelineDesc.bindingLayouts = { m_BitpackLayout };
        bitpackPipelineDesc.CS = m_BitpackShader;
        m_BitpackPipeline = GetDevice()->createComputePipeline(bitpackPipelineDesc);
        if (!m_BitpackPipeline)
            return false;

        nvrhi::BindingLayoutDesc temporalLayoutDesc;
        temporalLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        temporalLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_SRV(5),
            nvrhi::BindingLayoutItem::Texture_SRV(6),
            nvrhi::BindingLayoutItem::Texture_SRV(7),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(2),
            nvrhi::BindingLayoutItem::Sampler(0)
        };
        m_TemporalLayout = GetDevice()->createBindingLayout(temporalLayoutDesc);
        m_TemporalShader = m_ShaderFactory->CreateShader("app/shadow_temporal.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
        if (!m_TemporalShader)
            return false;

        nvrhi::ComputePipelineDesc temporalPipelineDesc;
        temporalPipelineDesc.bindingLayouts = { m_TemporalLayout };
        temporalPipelineDesc.CS = m_TemporalShader;
        m_TemporalPipeline = GetDevice()->createComputePipeline(temporalPipelineDesc);
        if (!m_TemporalPipeline)
            return false;

        nvrhi::BindingLayoutDesc denoiseLayoutDesc;
        denoiseLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        denoiseLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        m_DenoiseLayout = GetDevice()->createBindingLayout(denoiseLayoutDesc);
        m_DenoiseShader = m_ShaderFactory->CreateShader("app/shadow_denoise.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
        if (!m_DenoiseShader)
            return false;

        nvrhi::ComputePipelineDesc denoisePipelineDesc;
        denoisePipelineDesc.bindingLayouts = { m_DenoiseLayout };
        denoisePipelineDesc.CS = m_DenoiseShader;
        m_DenoisePipeline = GetDevice()->createComputePipeline(denoisePipelineDesc);
        if (!m_DenoisePipeline)
            return false;

        nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
        bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
        bindlessLayoutDesc.firstSlot = 0;
        bindlessLayoutDesc.maxCapacity = 1024;
        bindlessLayoutDesc.registerSpaces = {
            nvrhi::BindingLayoutItem::RawBuffer_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2)
        };
        m_BindlessLayout = GetDevice()->createBindlessLayout(bindlessLayoutDesc);

        nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
        globalBindingLayoutDesc.visibility = nvrhi::ShaderType::All;
        globalBindingLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
            nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(2),
            nvrhi::BindingLayoutItem::Texture_UAV(3),
            nvrhi::BindingLayoutItem::Texture_UAV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(5)
        };
        m_BindingLayout = GetDevice()->createBindingLayout(globalBindingLayoutDesc);

        m_DescriptorTable = std::make_shared<engine::DescriptorTableManager>(GetDevice(), m_BindlessLayout);

        auto nativeFS = std::make_shared<vfs::NativeFileSystem>();
        m_TextureCache = std::make_shared<engine::TextureCache>(GetDevice(), nativeFS, m_DescriptorTable);
        m_TextureCache->SetMaxTextureSize(2048);
        
        SetAsynchronousLoadingEnabled(false);
        BeginLoadingScene(nativeFS, sceneFileName);

        m_SunLight = std::make_shared<engine::DirectionalLight>();
        m_Scene->GetSceneGraph()->AttachLeafNode(m_Scene->GetSceneGraph()->GetRootNode(), m_SunLight);

        m_SunLight->SetDirection(double3(0.1f, -1.0f, -0.15f));
        m_SunLight->angularSize = 0.53f;
        m_SunLight->irradiance = 4.f;

        m_SkyParams.brightness = 0.12f;

        m_Scene->FinishedLoading(GetFrameIndex());
        
        m_Camera.LookAt(float3(0.f, 1.8f, 0.f), float3(1.f, 1.8f, 0.f));
        m_Camera.SetMoveSpeed(3.f);

        m_ConstantBuffer = GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(LightingConstants), "LightingConstants", engine::c_MaxRenderPassConstantBufferVersions));

        m_SkyCB = GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(ProceduralSkyShaderParameters), "SkyConstants", engine::c_MaxRenderPassConstantBufferVersions));

        m_FrameCB = GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(FrameConstants), "FrameConstants", engine::c_MaxRenderPassConstantBufferVersions));

        if (useRayQuery)
        {
            if (!CreateComputePipeline(*m_ShaderFactory))
                return false;
        }
        else
        {
            if (!CreateRayTracingPipeline(*m_ShaderFactory))
                return false;
        }

        m_CommandList = GetDevice()->createCommandList();

        m_CommandList->open();

        CreateAccelStructs(m_CommandList);

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);

        GetDevice()->waitForIdle();

        if (!stageFileName.empty())
        {
            m_EcsStage = std::make_unique<ecs::Stage>();
            if (!m_EcsStage->Load(stageFileName))
                log::warning("Failed to load stage '%s'", stageFileName.generic_string().c_str());

            m_StageUI = std::make_shared<StagePanelUI>(GetDeviceManager(), *m_EcsStage);
            if (!m_StageUI->Init(m_ShaderFactory))
            {
                m_StageUI.reset();
                return false;
            }
        }

        return true;
    }

    bool LoadScene(std::shared_ptr<vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName) override 
    {
        std::unique_ptr<engine::Scene> scene = std::make_unique<engine::Scene>(GetDevice(),
            *m_ShaderFactory, fs, m_TextureCache, m_DescriptorTable, nullptr);

        if (scene->Load(sceneFileName))
        {
            m_Scene = std::move(scene);
            return true;
        }

        return false;
    }

    bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
        m_Camera.KeyboardUpdate(key, scancode, action, mods);

        if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
        {
            m_EnableAnimations = !m_EnableAnimations;
            return true;
        }

        return true;
    }

    bool MousePosUpdate(double xpos, double ypos) override
    {
        m_Camera.MousePosUpdate(xpos, ypos);
        return true;
    }

    bool MouseButtonUpdate(int button, int action, int mods) override
    {
        m_Camera.MouseButtonUpdate(button, action, mods);
        return true;
    }

    bool MouseScrollUpdate(double xoffset, double yoffset) override
    {
        m_Camera.MouseScrollUpdate(xoffset, yoffset);
        return true;
    }

    void Animate(float fElapsedTimeSeconds) override
    {
        m_Camera.Animate(fElapsedTimeSeconds);

        if (m_ToneMappingPass)
            m_ToneMappingPass->AdvanceFrame(fElapsedTimeSeconds);

        if (IsSceneLoaded() && m_EnableAnimations)
        {
            m_WallclockTime += fElapsedTimeSeconds;
            float offset = 0;

            for (const auto& anim : m_Scene->GetSceneGraph()->GetAnimations())
            {
                float duration = anim->GetDuration();
                float integral;
                float animationTime = std::modf((m_WallclockTime + offset) / duration, &integral) * duration;
                (void)anim->Apply(animationTime);
                offset += 1.0f;
            }
        }

        const char* extraInfo = (m_RayPipeline != nullptr) ? "- using RayPipeline" : "- using RayQuery";
        GetDeviceManager()->SetInformativeWindowTitle(g_WindowTitle, extraInfo);
    }

    bool CreateRayTracingPipeline(engine::ShaderFactory& shaderFactory)
    {
        std::vector<engine::ShaderMacro> defines = { { "USE_RAY_QUERY", "0" } };
        m_ShaderLibrary = shaderFactory.CreateShaderLibrary("app/rt_bindless.hlsl", &defines);

        if (!m_ShaderLibrary)
            return false;

        nvrhi::rt::PipelineDesc pipelineDesc;
        pipelineDesc.globalBindingLayouts = { m_BindingLayout, m_BindlessLayout };
        pipelineDesc.shaders = {
            { "", m_ShaderLibrary->getShader("RayGen", nvrhi::ShaderType::RayGeneration), nullptr },
            { "", m_ShaderLibrary->getShader("Miss", nvrhi::ShaderType::Miss), nullptr }
        };

        pipelineDesc.hitGroups = { {
            "HitGroup",
            m_ShaderLibrary->getShader("ClosestHit", nvrhi::ShaderType::ClosestHit),
            m_ShaderLibrary->getShader("AnyHit", nvrhi::ShaderType::AnyHit),
            nullptr, // intersectionShader
            nullptr, // bindingLayout
            false  // isProceduralPrimitive
        } };

        pipelineDesc.maxPayloadSize = sizeof(float) * 6;

        m_RayPipeline = GetDevice()->createRayTracingPipeline(pipelineDesc);

        if (!m_RayPipeline)
            return false;

        auto shaderTableDesc = nvrhi::rt::ShaderTableDesc()
            .enableCaching(3)
            .setDebugName("Shader Table");
        m_ShaderTable = m_RayPipeline->createShaderTable(shaderTableDesc);

        if (!m_ShaderTable)
            return false;

        m_ShaderTable->setRayGenerationShader("RayGen");
        m_ShaderTable->addHitGroup("HitGroup");
        m_ShaderTable->addMissShader("Miss");

        return true;
    }

    bool CreateComputePipeline(engine::ShaderFactory& shaderFactory)
    {
        std::vector<engine::ShaderMacro> defines = { { "USE_RAY_QUERY", "1" } };
        m_ComputeShader = shaderFactory.CreateShader("app/rt_bindless.hlsl", "main", &defines, nvrhi::ShaderType::Compute);

        if (!m_ComputeShader)
            return false;

        auto pipelineDesc = nvrhi::ComputePipelineDesc()
            .setComputeShader(m_ComputeShader)
            .addBindingLayout(m_BindingLayout)
            .addBindingLayout(m_BindlessLayout);

        m_ComputePipeline = GetDevice()->createComputePipeline(pipelineDesc);

        if (!m_ComputePipeline)
            return false;

        return true;
    }

    void GetMeshBlasDesc(engine::MeshInfo& mesh, nvrhi::rt::AccelStructDesc& blasDesc) const
    {
        blasDesc.isTopLevel = false;
        blasDesc.debugName = mesh.name;

        for (const auto& geometry : mesh.geometries)
        {
            nvrhi::rt::GeometryDesc geometryDesc;
            auto & triangles = geometryDesc.geometryData.triangles;
            triangles.indexBuffer = mesh.buffers->indexBuffer;
            triangles.indexOffset = (mesh.indexOffset + geometry->indexOffsetInMesh) * sizeof(uint32_t);
            triangles.indexFormat = nvrhi::Format::R32_UINT;
            triangles.indexCount = geometry->numIndices;
            triangles.vertexBuffer = mesh.buffers->vertexBuffer;
            triangles.vertexOffset = (mesh.vertexOffset + geometry->vertexOffsetInMesh) * sizeof(float3) + mesh.buffers->getVertexBufferRange(engine::VertexAttribute::Position).byteOffset;
            triangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
            triangles.vertexStride = sizeof(float3);
            triangles.vertexCount = geometry->numVertices;
            geometryDesc.geometryType = nvrhi::rt::GeometryType::Triangles;
            bool nonOpaque =
                geometry->material->domain == engine::MaterialDomain::AlphaTested ||
                geometry->material->domain == engine::MaterialDomain::AlphaBlended ||
                geometry->material->domain == engine::MaterialDomain::TransmissiveAlphaTested ||
                geometry->material->domain == engine::MaterialDomain::TransmissiveAlphaBlended;
            geometryDesc.flags = nonOpaque
                ? nvrhi::rt::GeometryFlags::None
                : nvrhi::rt::GeometryFlags::Opaque;
            blasDesc.bottomLevelGeometries.push_back(geometryDesc);
        }

        // don't compact acceleration structures that are built per frame
        if (mesh.skinPrototype != nullptr)
        {
            blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;
        }
        else
        {
            blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace | nvrhi::rt::AccelStructBuildFlags::AllowCompaction;
        }
    }

    void CreateAccelStructs(nvrhi::ICommandList* commandList)
    {
        for (const auto& mesh : m_Scene->GetSceneGraph()->GetMeshes())
        {
            if (mesh->isSkinPrototype)
                continue; // skip the skinning prototypes
            
            nvrhi::rt::AccelStructDesc blasDesc;

            GetMeshBlasDesc(*mesh, blasDesc);

            nvrhi::rt::AccelStructHandle as = GetDevice()->createAccelStruct(blasDesc);

            if (!mesh->skinPrototype)
                nvrhi::utils::BuildBottomLevelAccelStruct(commandList, as, blasDesc);

            mesh->accelStruct = as;
        }


        nvrhi::rt::AccelStructDesc tlasDesc;
        tlasDesc.isTopLevel = true;
        tlasDesc.topLevelMaxInstances = m_Scene->GetSceneGraph()->GetMeshInstances().size();
        m_TopLevelAS = GetDevice()->createAccelStruct(tlasDesc);
    }

    void BuildTLAS(nvrhi::ICommandList* commandList, uint32_t frameIndex) const
    {
        commandList->beginMarker("Skinned BLAS Updates");

        // Transition all the buffers to their necessary states before building the BLAS'es to allow BLAS batching
        for (const auto& skinnedInstance : m_Scene->GetSceneGraph()->GetSkinnedMeshInstances())
        {
            if (skinnedInstance->GetLastUpdateFrameIndex() < frameIndex)
                continue;

            commandList->setAccelStructState(skinnedInstance->GetMesh()->accelStruct, nvrhi::ResourceStates::AccelStructWrite);
            commandList->setBufferState(skinnedInstance->GetMesh()->buffers->vertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
        }
        commandList->commitBarriers();

        // Now build the BLAS'es
        for (const auto& skinnedInstance : m_Scene->GetSceneGraph()->GetSkinnedMeshInstances())
        {
            if (skinnedInstance->GetLastUpdateFrameIndex() < frameIndex)
                continue;
            
            nvrhi::rt::AccelStructDesc blasDesc;
            GetMeshBlasDesc(*skinnedInstance->GetMesh(), blasDesc);
            
            nvrhi::utils::BuildBottomLevelAccelStruct(commandList, skinnedInstance->GetMesh()->accelStruct, blasDesc);
        }
        commandList->endMarker();

        std::vector<nvrhi::rt::InstanceDesc> instances;

        for (const auto& instance : m_Scene->GetSceneGraph()->GetMeshInstances())
        {
            nvrhi::rt::InstanceDesc instanceDesc;
            instanceDesc.bottomLevelAS = instance->GetMesh()->accelStruct;
            assert(instanceDesc.bottomLevelAS);
            instanceDesc.instanceMask = 1;
            instanceDesc.instanceID = instance->GetInstanceIndex();

            auto node = instance->GetNode();
            assert(node);
            dm::affineToColumnMajor(node->GetLocalToWorldTransformFloat(), instanceDesc.transform);

            instances.push_back(instanceDesc);
        }

        // Compact acceleration structures that are tagged for compaction and have finished executing the original build
        commandList->compactBottomLevelAccelStructs();

        commandList->beginMarker("TLAS Update");
        commandList->buildTopLevelAccelStruct(m_TopLevelAS, instances.data(), instances.size());
        commandList->endMarker();
    }


    void BackBufferResizing() override
    { 
        m_ColorBuffer = nullptr;
        m_LdrColor = nullptr;
        m_GBufferDepth = nullptr;
        m_GBufferNormal = nullptr;
        m_AmbientOcclusion = nullptr;
        m_DirectBuffer = nullptr;
        m_VisBuffer = nullptr;
        m_GeomNormal = nullptr;
        m_ReprojMap = nullptr;
        m_ShadowMask = nullptr;
        m_TileMeta = nullptr;
        for (int i = 0; i < 3; i++)
            m_DenoiseBuffer[i] = nullptr;
        m_SsaoPass.reset();
        m_ReprojectBindingSet[0] = nullptr;
        m_ReprojectBindingSet[1] = nullptr;
        m_BitpackBindingSet = nullptr;
        m_TemporalBindingSet[0] = nullptr;
        m_TemporalBindingSet[1] = nullptr;
        for (int i = 0; i < 3; i++)
            m_DenoiseBindingSet[i] = nullptr;
        m_CompositeBindingSet = nullptr;
        for (int i = 0; i < 2; i++)
        {
            m_DepthHistory[i] = nullptr;
            m_Moments[i] = nullptr;
        }
        m_BindingSet = nullptr;
        m_HistoryIndex = 0;
        m_HasPrevViewProj = false;
        m_HdrFramebuffer.reset();
        m_LdrFramebuffer.reset();
        m_BloomPass.reset();
        m_ToneMappingPass.reset();
        m_BindingCache->Clear();
    }

    void Render(nvrhi::IFramebuffer* framebuffer) override
    {
        const auto& fbinfo = framebuffer->getFramebufferInfo();

        if (!m_ColorBuffer)
        {
            nvrhi::TextureDesc desc;
            desc.width = fbinfo.width;
            desc.height = fbinfo.height;
            desc.isUAV = true;
            desc.isRenderTarget = true;
            desc.keepInitialState = true;
            desc.format = nvrhi::Format::RGBA16_FLOAT;
            desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
            desc.debugName = "ColorBuffer";
            m_ColorBuffer = GetDevice()->createTexture(desc);

            nvrhi::TextureDesc ldrDesc = desc;
            ldrDesc.format = nvrhi::Format::SRGBA8_UNORM;
            ldrDesc.isUAV = false;
            ldrDesc.initialState = nvrhi::ResourceStates::RenderTarget;
            ldrDesc.debugName = "LdrColor";
            m_LdrColor = GetDevice()->createTexture(ldrDesc);

            m_HdrFramebuffer = std::make_shared<engine::FramebufferFactory>(GetDevice());
            m_HdrFramebuffer->RenderTargets = { m_ColorBuffer };

            m_LdrFramebuffer = std::make_shared<engine::FramebufferFactory>(GetDevice());
            m_LdrFramebuffer->RenderTargets = { m_LdrColor };

            nvrhi::TextureDesc gbufferDesc = desc;
            gbufferDesc.isRenderTarget = false;
            gbufferDesc.isUAV = true;
            gbufferDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;

            gbufferDesc.format = nvrhi::Format::R32_FLOAT;
            gbufferDesc.debugName = "GBufferDepth";
            m_GBufferDepth = GetDevice()->createTexture(gbufferDesc);

            gbufferDesc.format = nvrhi::Format::RGBA16_FLOAT;
            gbufferDesc.debugName = "GBufferNormal";
            m_GBufferNormal = GetDevice()->createTexture(gbufferDesc);

            gbufferDesc.format = nvrhi::Format::R8_UNORM;
            gbufferDesc.debugName = "AmbientOcclusion";
            m_AmbientOcclusion = GetDevice()->createTexture(gbufferDesc);

            gbufferDesc.format = nvrhi::Format::RGBA16_FLOAT;
            gbufferDesc.debugName = "DirectBuffer";
            m_DirectBuffer = GetDevice()->createTexture(gbufferDesc);

            gbufferDesc.format = nvrhi::Format::R32_FLOAT;
            gbufferDesc.debugName = "VisBuffer";
            m_VisBuffer = GetDevice()->createTexture(gbufferDesc);

            gbufferDesc.format = nvrhi::Format::RGBA16_FLOAT;
            gbufferDesc.debugName = "GeomNormal";
            m_GeomNormal = GetDevice()->createTexture(gbufferDesc);

            gbufferDesc.debugName = "ReprojMap";
            m_ReprojMap = GetDevice()->createTexture(gbufferDesc);

            for (int i = 0; i < 3; i++)
            {
                gbufferDesc.debugName = "DenoiseBuffer";
                m_DenoiseBuffer[i] = GetDevice()->createTexture(gbufferDesc);
            }

            nvrhi::TextureDesc historyDesc = desc;
            historyDesc.isRenderTarget = false;
            historyDesc.isUAV = true;
            historyDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
            for (int i = 0; i < 2; i++)
            {
                historyDesc.format = nvrhi::Format::R32_FLOAT;
                historyDesc.debugName = i == 0 ? "DepthHistory0" : "DepthHistory1";
                m_DepthHistory[i] = GetDevice()->createTexture(historyDesc);

                historyDesc.format = nvrhi::Format::RGBA32_FLOAT;
                historyDesc.debugName = i == 0 ? "Moments0" : "Moments1";
                m_Moments[i] = GetDevice()->createTexture(historyDesc);
            }

            const uint32_t tilesX = dm::div_ceil(fbinfo.width, 8);
            const uint32_t maskTilesY = dm::div_ceil(fbinfo.height, 4);
            const uint32_t metaTilesY = dm::div_ceil(fbinfo.height, 8);

            nvrhi::TextureDesc tileDesc = historyDesc;
            tileDesc.format = nvrhi::Format::R32_UINT;
            tileDesc.width = tilesX;
            tileDesc.height = maskTilesY;
            tileDesc.debugName = "ShadowMask";
            m_ShadowMask = GetDevice()->createTexture(tileDesc);

            tileDesc.height = metaTilesY;
            tileDesc.debugName = "TileMeta";
            m_TileMeta = GetDevice()->createTexture(tileDesc);

            m_HistoryIndex = 0;
            m_HasPrevViewProj = false;

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::ConstantBuffer(1, m_SkyCB),
                nvrhi::BindingSetItem::ConstantBuffer(2, m_FrameCB),
                nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_TopLevelAS),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_Scene->GetInstanceBuffer()),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_Scene->GetGeometryBuffer()),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_Scene->GetMaterialBuffer()),
                nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_AnisotropicWrapSampler),
                nvrhi::BindingSetItem::Texture_UAV(0, m_ColorBuffer),
                nvrhi::BindingSetItem::Texture_UAV(1, m_GBufferDepth),
                nvrhi::BindingSetItem::Texture_UAV(2, m_GBufferNormal),
                nvrhi::BindingSetItem::Texture_UAV(3, m_DirectBuffer),
                nvrhi::BindingSetItem::Texture_UAV(4, m_VisBuffer),
                nvrhi::BindingSetItem::Texture_UAV(5, m_GeomNormal)
            };
            m_BindingSet = GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

            render::SsaoPass::CreateParameters ssaoCreateParams;
            ssaoCreateParams.dimensions = dm::int2(fbinfo.width, fbinfo.height);
            ssaoCreateParams.inputLinearDepth = true;
            ssaoCreateParams.octEncodedNormals = false;
            ssaoCreateParams.directionalOcclusion = false;
            ssaoCreateParams.numBindingSets = 1;
            m_SsaoPass = std::make_unique<render::SsaoPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, ssaoCreateParams);
            m_SsaoPass->CreateBindingSet(m_GBufferDepth, m_GBufferNormal, m_AmbientOcclusion);

            m_SsaoParams.radiusWorld = 0.6f;
            m_SsaoParams.powerExponent = 2.f;
            m_SsaoParams.backgroundViewDepth = 100.f;

            for (int i = 0; i < 2; i++)
            {
                nvrhi::BindingSetDesc reprojectSetDesc;
                reprojectSetDesc.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, m_ReprojectCB),
                    nvrhi::BindingSetItem::Texture_SRV(0, m_GBufferDepth),
                    nvrhi::BindingSetItem::Texture_SRV(1, m_GeomNormal),
                    nvrhi::BindingSetItem::Texture_SRV(2, m_DepthHistory[1 - i]),
                    nvrhi::BindingSetItem::Texture_UAV(0, m_ReprojMap),
                    nvrhi::BindingSetItem::Texture_UAV(1, m_DepthHistory[i])
                };
                m_ReprojectBindingSet[i] = GetDevice()->createBindingSet(reprojectSetDesc, m_ReprojectLayout);

                nvrhi::BindingSetDesc temporalSetDesc;
                temporalSetDesc.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, m_TemporalCB),
                    nvrhi::BindingSetItem::Texture_SRV(0, m_VisBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(1, m_ShadowMask),
                    nvrhi::BindingSetItem::Texture_SRV(2, m_Moments[1 - i]),
                    nvrhi::BindingSetItem::Texture_SRV(3, m_DenoiseBuffer[1]),
                    nvrhi::BindingSetItem::Texture_SRV(4, m_ReprojMap),
                    nvrhi::BindingSetItem::Texture_SRV(5, m_GBufferDepth),
                    nvrhi::BindingSetItem::Texture_SRV(6, m_GeomNormal),
                    nvrhi::BindingSetItem::Texture_SRV(7, m_DepthHistory[1 - i]),
                    nvrhi::BindingSetItem::Texture_UAV(0, m_Moments[i]),
                    nvrhi::BindingSetItem::Texture_UAV(1, m_DenoiseBuffer[0]),
                    nvrhi::BindingSetItem::Texture_UAV(2, m_TileMeta),
                    nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearClampSampler)
                };
                m_TemporalBindingSet[i] = GetDevice()->createBindingSet(temporalSetDesc, m_TemporalLayout);
            }

            nvrhi::BindingSetDesc bitpackSetDesc;
            bitpackSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_BitpackCB),
                nvrhi::BindingSetItem::Texture_SRV(0, m_VisBuffer),
                nvrhi::BindingSetItem::Texture_UAV(0, m_ShadowMask)
            };
            m_BitpackBindingSet = GetDevice()->createBindingSet(bitpackSetDesc, m_BitpackLayout);

            const int spatialSource[3] = { 0, 1, 2 };
            const int spatialTarget[3] = { 1, 2, 0 };
            for (int pass = 0; pass < 3; pass++)
            {
                nvrhi::BindingSetDesc denoiseSetDesc;
                denoiseSetDesc.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, m_AtrousCB[pass]),
                    nvrhi::BindingSetItem::Texture_SRV(0, m_DenoiseBuffer[spatialSource[pass]]),
                    nvrhi::BindingSetItem::Texture_SRV(1, m_TileMeta),
                    nvrhi::BindingSetItem::Texture_SRV(2, m_GeomNormal),
                    nvrhi::BindingSetItem::Texture_SRV(3, m_GBufferDepth),
                    nvrhi::BindingSetItem::Texture_UAV(0, m_DenoiseBuffer[spatialTarget[pass]])
                };
                m_DenoiseBindingSet[pass] = GetDevice()->createBindingSet(denoiseSetDesc, m_DenoiseLayout);
            }

            nvrhi::BindingSetDesc compositeSetDesc;
            compositeSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_CompositeCB),
                nvrhi::BindingSetItem::Texture_SRV(0, m_AmbientOcclusion),
                nvrhi::BindingSetItem::Texture_SRV(1, m_GBufferDepth),
                nvrhi::BindingSetItem::Texture_SRV(2, m_DirectBuffer),
                nvrhi::BindingSetItem::Texture_SRV(3, m_VisBuffer),
                nvrhi::BindingSetItem::Texture_SRV(4, m_DenoiseBuffer[0]),
                nvrhi::BindingSetItem::Texture_UAV(0, m_ColorBuffer)
            };
            m_CompositeBindingSet = GetDevice()->createBindingSet(compositeSetDesc, m_CompositeLayout);
        }

        nvrhi::Viewport windowViewport(float(fbinfo.width), float(fbinfo.height));
        m_View.SetViewport(windowViewport);
        m_View.SetMatrices(m_Camera.GetWorldToViewMatrix(), perspProjD3DStyleReverse(dm::PI_f * 0.25f, windowViewport.width() / windowViewport.height(), 0.1f));
        m_View.UpdateCache();

        if (m_StageUI)
            m_StageUI->SetCamera(m_View.GetViewProjectionMatrix(), m_Camera.GetPosition());

        if (!m_BloomPass)
        {
            m_BloomPass = std::make_unique<render::BloomPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_HdrFramebuffer, m_View);
            render::ToneMappingPass::CreateParameters toneMappingCreateParams;
            m_ToneMappingPass = std::make_unique<render::ToneMappingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_LdrFramebuffer, m_View, toneMappingCreateParams);
        }

        m_CommandList->open();

        if (m_EcsStage)
        {
            for (ecs::Prim prim : m_EcsStage->GetPrims())
            {
                if (!prim.HasAttribute<ecs::ModelAttribute>() || !prim.HasAttribute<ecs::TransformAttribute>())
                    continue;

                ecs::TransformAttribute& t = prim.GetAttribute<ecs::TransformAttribute>();
                dm::dquat rotation(t.rotation.w, t.rotation.x, t.rotation.y, t.rotation.z);
                dm::double3 translation(t.translation.x, t.translation.y, t.translation.z);
                dm::double3 scaling(t.scaling.x, t.scaling.y, t.scaling.z);
                m_Scene->GetSceneGraph()->GetRootNode()->SetTransform(&translation, &rotation, &scaling);
                break;
            }
        }

        m_Scene->Refresh(m_CommandList, GetFrameIndex());
        BuildTLAS(m_CommandList, GetFrameIndex());
        
        LightingConstants constants = {};
        constants.ambientColor = float4(0.05f);
        m_View.FillPlanarViewConstants(constants.view);
        m_SunLight->FillLightConstants(constants.light);
        m_CommandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

        ProceduralSkyShaderParameters skyParams = {};
        render::SkyPass::FillShaderParameters(*m_SunLight, m_SkyParams, skyParams);
        m_CommandList->writeBuffer(m_SkyCB, &skyParams, sizeof(skyParams));

        dm::affine3 worldToView = m_Camera.GetWorldToViewMatrix();
        dm::float4x4 projection = perspProjD3DStyleReverse(dm::PI_f * 0.25f, windowViewport.width() / windowViewport.height(), 0.1f);
        dm::float4x4 viewProj = affineToHomogeneous(worldToView) * projection;

        FrameConstants frameConstants = {};
        frameConstants.frameIndex = uint32_t(GetFrameIndex());
        m_CommandList->writeBuffer(m_FrameCB, &frameConstants, sizeof(frameConstants));

        PlanarViewConstants viewConstants = {};
        m_View.FillPlanarViewConstants(viewConstants);

        ShadowDenoiseConstants shadowConstants = {};
        shadowConstants.matClipToView = viewConstants.matClipToView;
        shadowConstants.matViewToWorld = viewConstants.matViewToWorld;
        shadowConstants.matClipToWorld = viewConstants.matClipToWorld;
        shadowConstants.prevViewProj = m_HasPrevViewProj ? m_PrevViewProj : viewProj;
        shadowConstants.prevClipToView = m_HasPrevViewProj ? m_PrevClipToView : viewConstants.matClipToView;
        shadowConstants.texSize = dm::float4(
            float(fbinfo.width), float(fbinfo.height),
            1.f / float(fbinfo.width), 1.f / float(fbinfo.height));
        shadowConstants.clipToView = dm::float4(
            projection[2].w / projection[0].x,
            projection[2].w / projection[1].y, 0.f, 0.f);
        shadowConstants.misc = dm::float4(
            float(GetFrameIndex()),
            m_HasPrevViewProj ? 0.f : 1.f,
            0.01f,
            0.001f * (1080.f / float(fbinfo.height)));
        shadowConstants.filter = dm::float4(1.f, 0.f, 0.f, 0.f);

        m_PrevViewProj = viewProj;
        m_PrevClipToView = viewConstants.matClipToView;
        m_HasPrevViewProj = true;

        if (m_RayPipeline)
        {
            nvrhi::rt::State state;
            state.shaderTable = m_ShaderTable;
            state.bindings = { m_BindingSet, m_DescriptorTable->GetDescriptorTable() };
            m_CommandList->setRayTracingState(state);

            nvrhi::rt::DispatchRaysArguments args;
            args.width = fbinfo.width;
            args.height = fbinfo.height;
            m_CommandList->dispatchRays(args);
        }
        else if (m_ComputePipeline)
        {
            nvrhi::ComputeState state;
            state.pipeline = m_ComputePipeline;
            state.bindings = { m_BindingSet, m_DescriptorTable->GetDescriptorTable() };
            m_CommandList->setComputeState(state);

            m_CommandList->dispatch(
                dm::div_ceil(fbinfo.width, 16),
                dm::div_ceil(fbinfo.height, 16));
        }

        if (m_SsaoPass && m_CompositePipeline)
        {
            CompositeConstants compositeConstants;
            compositeConstants.strength = 0.7f;
            m_CommandList->writeBuffer(m_CompositeCB, &compositeConstants, sizeof(compositeConstants));

            m_SsaoPass->Render(m_CommandList, m_SsaoParams, m_View);

            const uint32_t passWidth = dm::div_ceil(fbinfo.width, 8);
            const uint32_t passHeight = dm::div_ceil(fbinfo.height, 8);
            const uint32_t tilesX = dm::div_ceil(fbinfo.width, 8);
            const uint32_t maskTilesY = dm::div_ceil(fbinfo.height, 4);

            bool denoiseRan = false;

            if (m_ReprojectPipeline && m_BitpackPipeline && m_TemporalPipeline && m_DenoisePipeline)
            {
                m_CommandList->writeBuffer(m_ReprojectCB, &shadowConstants, sizeof(shadowConstants));
                nvrhi::ComputeState reprojectState;
                reprojectState.pipeline = m_ReprojectPipeline;
                reprojectState.bindings = { m_ReprojectBindingSet[m_HistoryIndex] };
                m_CommandList->setComputeState(reprojectState);
                m_CommandList->dispatch(passWidth, passHeight, 1);

                m_CommandList->writeBuffer(m_BitpackCB, &shadowConstants, sizeof(shadowConstants));
                nvrhi::ComputeState bitpackState;
                bitpackState.pipeline = m_BitpackPipeline;
                bitpackState.bindings = { m_BitpackBindingSet };
                m_CommandList->setComputeState(bitpackState);
                m_CommandList->dispatch(dm::div_ceil(tilesX, 4), dm::div_ceil(maskTilesY, 4), 1);

                m_CommandList->writeBuffer(m_TemporalCB, &shadowConstants, sizeof(shadowConstants));
                nvrhi::ComputeState temporalState;
                temporalState.pipeline = m_TemporalPipeline;
                temporalState.bindings = { m_TemporalBindingSet[m_HistoryIndex] };
                m_CommandList->setComputeState(temporalState);
                m_CommandList->dispatch(passWidth, passHeight, 1);

                m_HistoryIndex = 1 - m_HistoryIndex;

                for (int pass = 0; pass < 3; pass++)
                {
                    shadowConstants.filter.x = float(1 << pass);
                    m_CommandList->writeBuffer(m_AtrousCB[pass], &shadowConstants, sizeof(shadowConstants));

                    nvrhi::ComputeState denoiseState;
                    denoiseState.pipeline = m_DenoisePipeline;
                    denoiseState.bindings = { m_DenoiseBindingSet[pass] };
                    m_CommandList->setComputeState(denoiseState);
                    m_CommandList->dispatch(passWidth, passHeight, 1);
                }

                denoiseRan = true;
            }

            if (denoiseRan)
            {
                nvrhi::ComputeState compositeState;
                compositeState.pipeline = m_CompositePipeline;
                compositeState.bindings = { m_CompositeBindingSet };
                m_CommandList->setComputeState(compositeState);
                m_CommandList->dispatch(passWidth, passHeight, 1);
            }
        }

        if (m_BloomPass)
        {
            m_BloomPass->Render(m_CommandList, m_HdrFramebuffer, m_View, m_ColorBuffer, 8.0f, 0.15f);
        }

        if (m_ToneMappingPass)
        {
            render::ToneMappingParameters toneMappingParams;
            toneMappingParams.enableColorLUT = false;
            toneMappingParams.exposureBias = -1.0f;
            m_ToneMappingPass->SimpleRender(m_CommandList, toneMappingParams, m_View, m_ColorBuffer);
            m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_LdrColor, m_BindingCache.get());
        }
        else
        {
            m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_ColorBuffer, m_BindingCache.get());
        }

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
    }

};

#ifdef WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int __argc, const char** __argv)
#endif
{
    nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);
    app::DeviceManager* deviceManager = app::DeviceManager::Create(api);

    app::DeviceCreationParameters deviceParams;
    deviceParams.enableRayTracingExtensions = true;

    bool useRayQuery = false;
    std::filesystem::path sceneFileName = app::GetDirectoryWithExecutable().parent_path() / "media/intel-sponza.scene.json";
    std::filesystem::path stageFileName = app::GetDirectoryWithExecutable().parent_path() / "stages/intel_sponza.stage";
    for (int i = 1; i < __argc; i++)
    {
        if (strcmp(__argv[i], "-rayQuery") == 0)
        {
            useRayQuery = true;
        }
        else if (strcmp(__argv[i], "-debug") == 0)
        {
            deviceParams.enableDebugRuntime = true;
            deviceParams.enableNvrhiValidationLayer = true;
        }
        else if (__argv[i][0] != '-')
        {
            std::filesystem::path argPath = __argv[i];
            if (argPath.is_relative())
                argPath = app::GetDirectoryWithExecutable().parent_path() / argPath;

            if (argPath.extension() == ".stage")
                stageFileName = argPath;
            else
                sceneFileName = argPath;
        }
    }

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, g_WindowTitle))
    {
        log::fatal("Cannot initialize a graphics device with the requested parameters");
        return 1;
    }

    if (!useRayQuery && !deviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline))
    {
        log::fatal("The graphics device does not support Ray Tracing Pipelines");
        return 1;
    }

    if (useRayQuery && !deviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::RayQuery))
    {
        log::fatal("The graphics device does not support Ray Queries");
        return 1;
    }

    {
        BindlessRayTracing example(deviceManager);
        if (example.Init(useRayQuery, sceneFileName, stageFileName))
        {
            deviceManager->AddRenderPassToBack(&example);
            example.AddUIRenderPass();
            deviceManager->RunMessageLoop();
            example.RemoveUIRenderPass();
            deviceManager->RemoveRenderPass(&example);
        }
    }
    
    deviceManager->Shutdown();

    delete deviceManager;

    return 0;
}
