#include <donut/app/ApplicationBase.h>
#include <donut/app/DeviceManager.h>
#include <donut/app/Camera.h>
#include <donut/app/imgui_renderer.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <donut/ecs/Stage.h>
#include <donut/ecs/Attribute.h>
#include <donut/engine/ShaderFactory.h>
#include <nvrhi/utils.h>

#include <imgui.h>
#include <imgui_internal.h>

#define main rt_bindless_entry_main
#define WinMain rt_bindless_entry_winmain
#include "../rt_bindless/rt_bindless.cpp"
#undef main
#undef WinMain

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace donut;
using namespace donut::math;

namespace
{
    struct Vertex
    {
        dm::float3 pos;
        dm::float3 normal;
    };

    const Vertex g_Cube[] = {
        {{-1,-1, 1}, {0,0,1}}, {{ 1,-1, 1}, {0,0,1}}, {{ 1, 1, 1}, {0,0,1}}, {{-1, 1, 1}, {0,0,1}},
        {{-1,-1,-1}, {0,0,-1}}, {{-1, 1,-1}, {0,0,-1}}, {{ 1, 1,-1}, {0,0,-1}}, {{ 1,-1,-1}, {0,0,-1}},
        {{-1, 1,-1}, {0,1,0}}, {{-1, 1, 1}, {0,1,0}}, {{ 1, 1, 1}, {0,1,0}}, {{ 1, 1,-1}, {0,1,0}},
        {{-1,-1,-1}, {0,-1,0}}, {{ 1,-1,-1}, {0,-1,0}}, {{ 1,-1, 1}, {0,-1,0}}, {{-1,-1, 1}, {0,-1,0}},
        {{ 1,-1,-1}, {1,0,0}}, {{ 1, 1,-1}, {1,0,0}}, {{ 1, 1, 1}, {1,0,0}}, {{ 1,-1, 1}, {1,0,0}},
        {{-1,-1,-1}, {-1,0,0}}, {{-1,-1, 1}, {-1,0,0}}, {{-1, 1, 1}, {-1,0,0}}, {{-1, 1,-1}, {-1,0,0}},
    };

    const uint16_t g_CubeIndices[] = {
        0,1,2, 0,2,3, 4,5,6, 4,6,7, 8,9,10, 8,10,11,
        12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23,
    };

    struct DrawEntry
    {
        dm::float4x4 viewProj;
        dm::float4x4 model;
        dm::float4 color;
        dm::float4 lightDir;
        float padding[24];
    };
    static_assert(sizeof(DrawEntry) == 256, "DrawEntry must be 256 bytes");

    constexpr uint32_t c_MaxDraws = 512;

    dm::float3x3 QuatToMatrix(const dm::quaternion<float>& q)
    {
        const float x = q.x, y = q.y, z = q.z, w = q.w;
        return dm::float3x3(
            dm::float3(1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)),
            dm::float3(2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)),
            dm::float3(2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)));
    }

    dm::float4x4 MakeModel(const dm::float3& translation, const dm::quaternion<float>& rotation, const dm::float3& scaling)
    {
        dm::float3x3 r = QuatToMatrix(rotation);
        dm::float3x3 linear = dm::float3x3(
            dm::float3(r[0][0] * scaling.x, r[0][1] * scaling.y, r[0][2] * scaling.z),
            dm::float3(r[1][0] * scaling.x, r[1][1] * scaling.y, r[1][2] * scaling.z),
            dm::float3(r[2][0] * scaling.x, r[2][1] * scaling.y, r[2][2] * scaling.z));
        return affineToHomogeneous(dm::affine3(linear, translation));
    }

    bool RayBox(const dm::float3& origin, const dm::float3& dir, const dm::float3& center, const dm::float3& half, float& tHit)
    {
        float tMin = 0.f;
        float tMax = 1e30f;
        for (int i = 0; i < 3; ++i)
        {
            const float o = origin[i], d = dir[i], c = center[i], h = half[i];
            if (std::abs(d) < 1e-8f)
            {
                if (o < c - h || o > c + h)
                    return false;
            }
            else
            {
                float t1 = (c - h - o) / d;
                float t2 = (c + h - o) / d;
                if (t1 > t2) std::swap(t1, t2);
                tMin = std::max(tMin, t1);
                tMax = std::min(tMax, t2);
                if (tMin > tMax)
                    return false;
            }
        }
        tHit = tMin;
        return true;
    }

    void ApplyTheme()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 4.f;
        style.FrameRounding = 3.f;
        style.GrabRounding = 2.f;
        style.TabRounding = 3.f;
        style.ScrollbarRounding = 3.f;
        style.WindowBorderSize = 1.f;
        style.FrameBorderSize = 0.f;

        const ImVec4 bg = ImVec4(0.086f, 0.090f, 0.098f, 1.f);
        const ImVec4 bgLight = ImVec4(0.130f, 0.137f, 0.148f, 1.f);
        const ImVec4 frame = ImVec4(0.160f, 0.170f, 0.182f, 1.f);
        const ImVec4 green = ImVec4(0.260f, 0.720f, 0.350f, 1.f);
        const ImVec4 greenDim = ImVec4(0.160f, 0.400f, 0.220f, 1.f);
        const ImVec4 greenHover = ImVec4(0.300f, 0.800f, 0.400f, 1.f);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.94f, 1.f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.52f, 0.54f, 1.f);
        colors[ImGuiCol_WindowBg] = bg;
        colors[ImGuiCol_ChildBg] = bg;
        colors[ImGuiCol_PopupBg] = bgLight;
        colors[ImGuiCol_Border] = ImVec4(0.22f, 0.24f, 0.26f, 1.f);
        colors[ImGuiCol_FrameBg] = frame;
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.24f, 0.26f, 1.f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.29f, 0.31f, 1.f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.075f, 0.08f, 1.f);
        colors[ImGuiCol_TitleBgActive] = greenDim;
        colors[ImGuiCol_TitleBgCollapsed] = bg;
        colors[ImGuiCol_MenuBarBg] = bg;
        colors[ImGuiCol_ScrollbarBg] = bg;
        colors[ImGuiCol_ScrollbarGrab] = frame;
        colors[ImGuiCol_ScrollbarGrabHovered] = greenDim;
        colors[ImGuiCol_ScrollbarGrabActive] = green;
        colors[ImGuiCol_CheckMark] = green;
        colors[ImGuiCol_SliderGrab] = greenDim;
        colors[ImGuiCol_SliderGrabActive] = green;
        colors[ImGuiCol_Button] = greenDim;
        colors[ImGuiCol_ButtonHovered] = green;
        colors[ImGuiCol_ButtonActive] = greenHover;
        colors[ImGuiCol_Header] = greenDim;
        colors[ImGuiCol_HeaderHovered] = green;
        colors[ImGuiCol_HeaderActive] = greenHover;
        colors[ImGuiCol_Separator] = ImVec4(0.22f, 0.24f, 0.26f, 1.f);
        colors[ImGuiCol_SeparatorHovered] = green;
        colors[ImGuiCol_SeparatorActive] = green;
        colors[ImGuiCol_ResizeGrip] = greenDim;
        colors[ImGuiCol_ResizeGripHovered] = green;
        colors[ImGuiCol_ResizeGripActive] = greenHover;
        colors[ImGuiCol_Tab] = greenDim;
        colors[ImGuiCol_TabHovered] = green;
        colors[ImGuiCol_TabSelected] = green;
        colors[ImGuiCol_TabSelectedOverline] = greenHover;
        colors[ImGuiCol_TabDimmed] = bg;
        colors[ImGuiCol_TabDimmedSelected] = greenDim;
        colors[ImGuiCol_DockingPreview] = green;
        colors[ImGuiCol_DockingEmptyBg] = bg;
        colors[ImGuiCol_PlotLines] = green;
        colors[ImGuiCol_PlotHistogram] = green;
        colors[ImGuiCol_TextSelectedBg] = ImVec4(green.x, green.y, green.z, 0.35f);
        colors[ImGuiCol_DragDropTarget] = green;
        colors[ImGuiCol_NavHighlight] = green;
    }
}

class EditorUI : public app::ImGui_Renderer
{
public:
    EditorUI(app::DeviceManager* deviceManager)
        : ImGui_Renderer(deviceManager)
        , m_Rt(deviceManager)
    {
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ApplyTheme();
    }

    bool Init(std::shared_ptr<engine::ShaderFactory> shaderFactory)
    {
        if (!ImGui_Renderer::Init(shaderFactory))
            return false;


        m_VertexShader = shaderFactory->CreateShader("app/editor.hlsl", "main_vs", nullptr, nvrhi::ShaderType::Vertex);
        m_PixelShader = shaderFactory->CreateShader("app/editor.hlsl", "main_ps", nullptr, nvrhi::ShaderType::Pixel);
        if (!m_VertexShader || !m_PixelShader)
            return false;

        nvrhi::VertexAttributeDesc attributes[] = {
            nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(0).setBufferIndex(0).setElementStride(sizeof(Vertex)),
            nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(12).setBufferIndex(0).setElementStride(sizeof(Vertex)),
        };
        m_InputLayout = GetDevice()->createInputLayout(attributes, uint32_t(std::size(attributes)), m_VertexShader);

        m_ConstantBuffer = GetDevice()->createBuffer(nvrhi::utils::CreateStaticConstantBufferDesc(sizeof(DrawEntry) * c_MaxDraws, "EditorDraws")
            .setInitialState(nvrhi::ResourceStates::ConstantBuffer).setKeepInitialState(true));

        for (uint32_t i = 0; i < c_MaxDraws; ++i)
        {
            nvrhi::BindingSetDesc desc;
            desc.bindings = { nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer, nvrhi::BufferRange(sizeof(DrawEntry) * i, sizeof(DrawEntry))) };
            if (!nvrhi::utils::CreateBindingSetAndLayout(GetDevice(), nvrhi::ShaderType::All, 0, desc, m_BindingLayout, m_BindingSets[i]))
                return false;
        }

        m_CommandList = GetDevice()->createCommandList();
        m_CommandList->open();

        nvrhi::BufferDesc vb;
        vb.byteSize = sizeof(g_Cube);
        vb.isVertexBuffer = true;
        vb.debugName = "EditorCubeVB";
        vb.initialState = nvrhi::ResourceStates::CopyDest;
        m_VertexBuffer = GetDevice()->createBuffer(vb);
        m_CommandList->beginTrackingBufferState(m_VertexBuffer, nvrhi::ResourceStates::CopyDest);
        m_CommandList->writeBuffer(m_VertexBuffer, g_Cube, sizeof(g_Cube));
        m_CommandList->setPermanentBufferState(m_VertexBuffer, nvrhi::ResourceStates::VertexBuffer);

        nvrhi::BufferDesc ib;
        ib.byteSize = sizeof(g_CubeIndices);
        ib.isIndexBuffer = true;
        ib.debugName = "EditorCubeIB";
        ib.initialState = nvrhi::ResourceStates::CopyDest;
        m_IndexBuffer = GetDevice()->createBuffer(ib);
        m_CommandList->beginTrackingBufferState(m_IndexBuffer, nvrhi::ResourceStates::CopyDest);
        m_CommandList->writeBuffer(m_IndexBuffer, g_CubeIndices, sizeof(g_CubeIndices));
        m_CommandList->setPermanentBufferState(m_IndexBuffer, nvrhi::ResourceStates::IndexBuffer);

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);

        m_Camera.LookAt(dm::float3(0.f, 3.f, 8.f), dm::float3(0.f, 1.f, 0.f));
        m_Camera.SetMoveSpeed(6.f);

        m_RootDir = app::GetDirectoryWithExecutable().parent_path();
        m_StageFile = m_RootDir / "stages/intel_sponza.stage";
        m_BrowseDir = m_RootDir / "stages";
        LoadStage(m_StageFile);
        RefreshContent();

        std::filesystem::path modelFile = m_RootDir / "media/IntelSponza/NewSponza_Main_glTF_003.gltf";
        for (ecs::Prim prim : m_Stage.GetPrims())
        {
            if (prim.HasAttribute<ecs::ModelAttribute>())
            {
                modelFile = m_RootDir / prim.GetAttribute<ecs::ModelAttribute>().path;
                break;
            }
        }
        m_RtReady = m_Rt.Init(false, modelFile, m_StageFile, false);
        if (m_RtReady)
        {
            m_Rt.m_UpdateWindowTitle = false;
            m_Rt.FrameCamera();
            Log("RT viewport renderer ready (same render as engine)");
        }
        else
            Log("RT renderer unavailable - using proxy fallback");

        return true;
    }

    void Log(const std::string& message)
    {
        m_Log.push_back(message);
        if (m_Log.size() > 300)
            m_Log.erase(m_Log.begin());
        m_LogScroll = true;
    }

    void LoadStage(const std::filesystem::path& path)
    {
        if (m_Stage.Load(path))
        {
            m_StageFile = path;
            m_Selected = entt::null;
            Log("Loaded stage: " + path.generic_string());
            RefreshContent();
        }
        else
        {
            Log("Failed to load stage: " + path.generic_string());
        }
    }

    void SaveStage(const std::filesystem::path& path)
    {
        if (m_Stage.Save(path))
        {
            m_StageFile = path;
            Log("Saved stage: " + path.generic_string());
        }
        else
        {
            Log("Failed to save stage: " + path.generic_string());
        }
    }

    void Play()
    {
        SaveStage(m_StageFile);
        std::filesystem::path binary = app::GetDirectoryWithExecutable() / "rt_bindless";
#ifdef WIN32
        binary += ".exe";
#endif
        std::string command = "\"" + binary.generic_string() + "\" \"" + m_StageFile.generic_string() + "\"";
#ifndef WIN32
        command += " &";
#endif
        std::system(command.c_str());
        Log("Launched engine preview");
    }

    void RefreshContent()
    {
        m_Archtypes.clear();
        m_Models.clear();
        std::error_code ec;
        const std::filesystem::path archDir = m_RootDir / "archtypes";
        if (std::filesystem::exists(archDir, ec))
        {
            for (const auto& entry : std::filesystem::directory_iterator(archDir, ec))
                if (entry.is_regular_file() && entry.path().extension() == ".archtype")
                    m_Archtypes.push_back(entry.path());
        }
        const std::filesystem::path mediaDir = m_RootDir / "media";
        if (std::filesystem::exists(mediaDir, ec))
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(mediaDir, ec))
            {
                if (!entry.is_regular_file())
                    continue;
                const auto ext = entry.path().extension();
                if (ext == ".gltf" || ext == ".glb")
                    m_Models.push_back(entry.path());
            }
        }
        std::sort(m_Archtypes.begin(), m_Archtypes.end());
        std::sort(m_Models.begin(), m_Models.end());
    }

    bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
        const bool imguiConsumed = ImGui_Renderer::KeyboardUpdate(key, scancode, action, mods);
        if (ImGui::GetIO().WantCaptureKeyboard)
            return true;
        if (m_ViewportHovered || m_ViewportFocused)
        {
            if (m_RtReady)
                m_Rt.KeyboardUpdate(key, scancode, action, mods);
            else
                m_Camera.KeyboardUpdate(key, scancode, action, mods);
            return true;
        }
        return imguiConsumed;
    }

    bool MousePosUpdate(double xpos, double ypos) override
    {
        const bool imguiConsumed = ImGui_Renderer::MousePosUpdate(xpos, ypos);
        if (ImGui::GetIO().WantCaptureMouse)
            return true;
        if (m_ViewportHovered)
        {
            if (m_RtReady)
                m_Rt.MousePosUpdate(xpos, ypos);
            else
                m_Camera.MousePosUpdate(xpos, ypos);
            return true;
        }
        return imguiConsumed;
    }

    bool MouseButtonUpdate(int button, int action, int mods) override
    {
        const bool imguiConsumed = ImGui_Renderer::MouseButtonUpdate(button, action, mods);
        if (ImGui::GetIO().WantCaptureMouse)
            return true;
        if (m_ViewportHovered)
        {
            if (button == 0 && action == 1 && !m_RtReady)
                PickAtMouse();
            if (m_RtReady)
                m_Rt.MouseButtonUpdate(button, action, mods);
            else
                m_Camera.MouseButtonUpdate(button, action, mods);
            return true;
        }
        return imguiConsumed;
    }

    void BackBufferResizing() override
    {
        m_Pipeline = nullptr;
        ImGui_Renderer::BackBufferResizing();
    }

    void Animate(float deltaT) override
    {
        if (m_RtReady)
            m_Rt.Animate(deltaT);
        else
            m_Camera.Animate(deltaT);
        ImGui_Renderer::Animate(deltaT);
    }

    void Render(nvrhi::IFramebuffer* framebuffer) override
    {
        if (m_RtReady)
            m_Rt.Render(framebuffer);
        else
            RenderProxies(framebuffer);
        ImGui_Renderer::Render(framebuffer);
    }

protected:
    void GetProxy(ecs::Prim prim, dm::float3& translation, dm::quaternion<float>& rotation, dm::float3& size, dm::float4& color)
    {
        translation = dm::float3(0.f);
        rotation = dm::quaternion<float>(0.f, 0.f, 0.f, 1.f);
        dm::float3 scaling = dm::float3(1.f);
        if (prim.HasAttribute<ecs::TransformAttribute>())
        {
            const auto& t = prim.GetAttribute<ecs::TransformAttribute>();
            translation = t.translation;
            rotation = dm::quaternion<float>(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w);
            scaling = t.scaling;
        }

        color = dm::float4(0.7f, 0.7f, 0.7f, 1.f);
        size = dm::float3(0.3f);

        if (prim.HasAttribute<ecs::ModelAttribute>())
        {
            color = dm::float4(0.45f, 0.55f, 0.75f, 1.f);
            size = scaling;
        }
        else if (prim.HasAttribute<ecs::CameraAttribute>())
        {
            translation = prim.GetAttribute<ecs::CameraAttribute>().position;
            color = dm::float4(0.3f, 0.8f, 0.4f, 1.f);
            size = dm::float3(0.3f, 0.3f, 0.6f);
        }
        else if (prim.HasAttribute<ecs::DirectionalLightAttribute>())
        {
            color = dm::float4(0.95f, 0.8f, 0.3f, 1.f);
            size = dm::float3(0.35f);
        }
        else if (prim.HasAttribute<ecs::SkyAttribute>())
        {
            color = dm::float4(0.3f, 0.5f, 0.9f, 1.f);
            size = dm::float3(0.25f);
        }

        if (prim.GetEntity() == m_Selected)
            color = dm::float4(1.f, 0.55f, 0.15f, 1.f);
    }

    void PickAtMouse()
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const ImVec2 rel = ImVec2(mouse.x - m_ViewportPos.x, mouse.y - m_ViewportPos.y);
        if (rel.x < 0 || rel.y < 0 || rel.x > m_ViewportSize.x || rel.y > m_ViewportSize.y)
            return;

        const float ndcX = (rel.x / m_ViewportSize.x) * 2.f - 1.f;
        const float ndcY = 1.f - (rel.y / m_ViewportSize.y) * 2.f;
        const float aspect = m_ViewportSize.x / std::max(m_ViewportSize.y, 1.f);
        const dm::float4x4 proj = perspProjD3DStyleReverse(dm::PI_f * 0.25f, aspect, 0.1f);
        const dm::float4x4 viewProj = affineToHomogeneous(m_Camera.GetWorldToViewMatrix()) * proj;
        const dm::float4x4 inv = inverse(viewProj);

        dm::float4 nearH = inv * dm::float4(ndcX, ndcY, 1.f, 1.f);
        dm::float4 farH = inv * dm::float4(ndcX, ndcY, 0.f, 1.f);
        const dm::float3 nearP = dm::float3(nearH.x / nearH.w, nearH.y / nearH.w, nearH.z / nearH.w);
        const dm::float3 farP = dm::float3(farH.x / farH.w, farH.y / farH.w, farH.z / farH.w);
        const dm::float3 dir = normalize(farP - nearP);

        float bestT = 1e30f;
        entt::entity best = entt::null;
        for (ecs::Prim prim : m_Stage.GetPrims())
        {
            dm::float3 t, s; dm::quaternion<float> r; dm::float4 c;
            GetProxy(prim, t, r, s, c);
            float hit;
            if (RayBox(nearP, dir, t, s, hit) && hit < bestT)
            {
                bestT = hit;
                best = prim.GetEntity();
            }
        }
        if (best != entt::null)
        {
            m_Selected = best;
            Log("Selected prim #" + std::to_string(uint32_t(best)));
        }
    }

    void RenderProxies(nvrhi::IFramebuffer* framebuffer)
    {
        const auto& fbinfo = framebuffer->getFramebufferInfo();
        const float aspect = float(fbinfo.width) / float(fbinfo.height);
        dm::float4x4 proj = perspProjD3DStyleReverse(dm::PI_f * 0.25f, aspect, 0.1f);
        dm::float4x4 viewProj = affineToHomogeneous(m_Camera.GetWorldToViewMatrix()) * proj;

        dm::float4 lightDir = dm::float4(0.3f, -1.f, 0.2f, 0.f);
        for (ecs::Prim prim : m_Stage.GetPrims())
        {
            if (prim.HasAttribute<ecs::DirectionalLightAttribute>())
            {
                lightDir = dm::float4(prim.GetAttribute<ecs::DirectionalLightAttribute>().direction, 0.f);
                break;
            }
        }

        if (!m_Pipeline)
        {
            nvrhi::GraphicsPipelineDesc desc;
            desc.VS = m_VertexShader;
            desc.PS = m_PixelShader;
            desc.inputLayout = m_InputLayout;
            desc.bindingLayouts = { m_BindingLayout };
            desc.primType = nvrhi::PrimitiveType::TriangleList;
            desc.renderState.depthStencilState.depthTestEnable = true;
            desc.renderState.depthStencilState.depthWriteEnable = true;
            desc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::GreaterOrEqual;
            desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
            m_Pipeline = GetDevice()->createGraphicsPipeline(desc, fbinfo);
        }

        std::vector<DrawEntry> entries;
        DrawEntry ground = {};
        ground.viewProj = viewProj;
        ground.model = MakeModel(dm::float3(0.f, -0.1f, 0.f), dm::quaternion<float>(0.f, 0.f, 0.f, 1.f), dm::float3(40.f, 0.05f, 40.f));
        ground.color = dm::float4(0.16f, 0.17f, 0.18f, 1.f);
        ground.lightDir = lightDir;
        entries.push_back(ground);

        for (ecs::Prim prim : m_Stage.GetPrims())
        {
            if (entries.size() >= c_MaxDraws)
                break;
            dm::float3 t, s; dm::quaternion<float> r; dm::float4 c;
            GetProxy(prim, t, r, s, c);
            DrawEntry entry = {};
            entry.viewProj = viewProj;
            entry.model = MakeModel(t, r, s);
            entry.color = c;
            entry.lightDir = lightDir;
            entries.push_back(entry);
        }

        m_CommandList->open();
        nvrhi::utils::ClearColorAttachment(m_CommandList, framebuffer, 0, nvrhi::Color(0.086f, 0.090f, 0.098f, 1.f));
        nvrhi::utils::ClearDepthStencilAttachment(m_CommandList, framebuffer, 0.f, 0);

        for (size_t i = 0; i < entries.size(); ++i)
            m_CommandList->writeBuffer(m_ConstantBuffer, &entries[i], sizeof(DrawEntry), sizeof(DrawEntry) * i);

        nvrhi::GraphicsState state;
        state.pipeline = m_Pipeline;
        state.framebuffer = framebuffer;
        state.viewport.addViewportAndScissorRect(fbinfo.getViewport());
        state.vertexBuffers.push_back({ m_VertexBuffer, 0, 0 });
        state.indexBuffer = { m_IndexBuffer, nvrhi::Format::R16_UINT, 0 };

        for (size_t i = 0; i < entries.size(); ++i)
        {
            state.bindings = { m_BindingSets[i] };
            m_CommandList->setGraphicsState(state);
            nvrhi::DrawArguments args;
            args.vertexCount = uint32_t(std::size(g_CubeIndices));
            m_CommandList->drawIndexed(args);
        }

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
    }

    void buildUI() override
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open Stage..."))
                    m_ShowOpenDialog = true;
                if (ImGui::MenuItem("Save Stage"))
                    SaveStage(m_StageFile);
                if (ImGui::MenuItem("Save As..."))
                    m_ShowSaveDialog = true;
                if (ImGui::MenuItem("Reload"))
                    LoadStage(m_StageFile);
                ImGui::Separator();
                if (ImGui::MenuItem("Play"))
                    Play();
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Play"))
                Play();
            ImGui::TextDisabled("|  %s  |  %zu prims", m_Stage.GetName().c_str(), m_Stage.GetPrimCount());
            ImGui::EndMainMenuBar();
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGuiID dock = ImGui::DockSpaceOverViewport(viewport->ID, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

        if (!m_DockInitialized)
        {
            m_DockInitialized = true;
            ImGui::DockBuilderRemoveNode(dock);
            ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dock, viewport->WorkSize);
            ImGuiID left, right, bottom, center;
            ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.18f, &left, &dock);
            ImGui::DockBuilderSplitNode(dock, ImGuiDir_Right, 0.24f, &right, &dock);
            ImGui::DockBuilderSplitNode(dock, ImGuiDir_Down, 0.25f, &bottom, &dock);
            center = dock;
            ImGuiID bottomLeft, bottomRight;
            ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.5f, &bottomLeft, &bottomRight);
            ImGui::DockBuilderDockWindow("Stage", left);
            ImGui::DockBuilderDockWindow("Attribute", right);
            ImGui::DockBuilderDockWindow("Content", bottomLeft);
            ImGui::DockBuilderDockWindow("Console", bottomRight);
            ImGui::DockBuilderFinish(dock);
        }

        DrawStagePanel();
        DrawAttributePanel();
        DrawContentPanel();
        DrawConsolePanel();

        ImGuiViewport* mainVp = ImGui::GetMainViewport();
        m_ViewportPos = mainVp->WorkPos;
        m_ViewportSize = mainVp->WorkSize;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool overWork = mouse.x >= mainVp->WorkPos.x && mouse.y >= mainVp->WorkPos.y &&
            mouse.x <= mainVp->WorkPos.x + mainVp->WorkSize.x && mouse.y <= mainVp->WorkPos.y + mainVp->WorkSize.y;
        m_ViewportHovered = overWork && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
        m_ViewportFocused = m_ViewportHovered;

        DrawFileDialogs();
    }

    void DrawFileDialogs()
    {
        if (m_ShowOpenDialog)
        {
            ImGui::OpenPopup("Open Stage");
            m_ShowOpenDialog = false;
        }
        if (m_ShowSaveDialog)
        {
            ImGui::OpenPopup("Save Stage As");
            m_ShowSaveDialog = false;
            snprintf(m_SaveName, sizeof(m_SaveName), "%s", m_StageFile.filename().generic_string().c_str());
        }

        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Open Stage", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextDisabled("%s", m_BrowseDir.generic_string().c_str());
            ImGui::Separator();
            ImGui::BeginChild("BrowseList", ImVec2(420, 260), true);
            if (m_BrowseDir.has_parent_path())
            {
                if (ImGui::Selectable(".."))
                    m_BrowseDir = m_BrowseDir.parent_path();
            }
            std::error_code ec;
            std::vector<std::filesystem::path> dirs, files;
            for (const auto& entry : std::filesystem::directory_iterator(m_BrowseDir, ec))
            {
                if (entry.is_directory())
                    dirs.push_back(entry.path());
                else if (entry.path().extension() == ".stage")
                    files.push_back(entry.path());
            }
            std::sort(dirs.begin(), dirs.end());
            std::sort(files.begin(), files.end());
            for (const auto& d : dirs)
                if (ImGui::Selectable(("[dir] " + d.filename().generic_string()).c_str()))
                    m_BrowseDir = d;
            for (const auto& f : files)
            {
                const bool sel = (m_BrowseSelection == f);
                if (ImGui::Selectable(f.filename().generic_string().c_str(), sel))
                    m_BrowseSelection = f;
            }
            ImGui::EndChild();
            if (ImGui::Button("Load", ImVec2(120, 0)) && !m_BrowseSelection.empty())
            {
                LoadStage(m_BrowseSelection);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Save Stage As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("File name", m_SaveName, sizeof(m_SaveName));
            if (ImGui::Button("Save", ImVec2(120, 0)))
            {
                std::filesystem::path target = m_BrowseDir / m_SaveName;
                if (target.extension() != ".stage")
                    target += ".stage";
                SaveStage(target);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void DrawStagePanel()
    {
        ImGui::Begin("Stage");
        if (ImGui::Button("Add"))
        {
            ecs::Prim prim = m_Stage.CreatePrim();
            ecs::AttributeRegistry::Get().AddDefault(m_Stage.GetRegistry(), prim.GetEntity(), "Name");
            ecs::AttributeRegistry::Get().AddDefault(m_Stage.GetRegistry(), prim.GetEntity(), "Transform");
            m_Selected = prim.GetEntity();
            Log("Added prim");
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove") && m_Selected != entt::null)
        {
            m_Stage.GetRegistry().destroy(m_Selected);
            m_Selected = entt::null;
            Log("Removed prim");
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            RefreshContent();
        ImGui::Separator();
        for (ecs::Prim prim : m_Stage.GetPrims())
        {
            std::string label = prim.HasAttribute<ecs::NameAttribute>()
                ? prim.GetAttribute<ecs::NameAttribute>().value
                : ("Prim " + std::to_string(uint32_t(prim.GetEntity())));
            if (ImGui::Selectable(label.c_str(), m_Selected == prim.GetEntity()))
            {
                m_Selected = prim.GetEntity();
                Log("Selected " + label);
            }
        }
        ImGui::End();
    }

    void DrawAttributePanel()
    {
        ImGui::Begin("Attribute");
        ecs::Prim prim;
        if (m_Selected != entt::null)
            prim = ecs::Prim(m_Stage.GetRegistry(), m_Selected);
        if (!prim.IsValid())
        {
            ImGui::TextDisabled("Select a prim in Stage or click it in the Viewport.");
            ImGui::End();
            return;
        }

        if (ImGui::Button("+ Add Attribute"))
            ImGui::OpenPopup("AddAttributePopup");
        if (ImGui::BeginPopup("AddAttributePopup"))
        {
            bool any = false;
            for (const std::string& name : ecs::AttributeRegistry::Get().GetAttributeNames())
            {
                if (ecs::AttributeRegistry::Get().Has(m_Stage.GetRegistry(), prim.GetEntity(), name))
                    continue;
                any = true;
                if (ImGui::MenuItem(name.c_str()))
                {
                    ecs::AttributeRegistry::Get().AddDefault(m_Stage.GetRegistry(), prim.GetEntity(), name);
                    Log("Added attribute " + name);
                }
            }
            if (!any)
                ImGui::TextDisabled("All attributes present");
            ImGui::EndPopup();
        }
        ImGui::Separator();

        for (const std::string& name : ecs::AttributeRegistry::Get().GetAttributeNames())
        {
            if (!ecs::AttributeRegistry::Get().Has(m_Stage.GetRegistry(), prim.GetEntity(), name))
                continue;
            ImGui::PushID(name.c_str());
            if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawAttribute(prim, name);
                if (ImGui::SmallButton("Remove"))
                {
                    ecs::AttributeRegistry::Get().Remove(m_Stage.GetRegistry(), prim.GetEntity(), name);
                    Log("Removed attribute " + name);
                }
            }
            ImGui::PopID();
        }
        ImGui::End();
    }

    void DrawAttribute(ecs::Prim prim, const std::string& name)
    {
        if (name == "Name")
        {
            auto& a = prim.GetAttribute<ecs::NameAttribute>();
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%s", a.value.c_str());
            if (ImGui::InputText("Value", buffer, sizeof(buffer)))
                a.value = buffer;
        }
        else if (name == "Tag")
        {
            auto& a = prim.GetAttribute<ecs::TagAttribute>();
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%s", a.value.c_str());
            if (ImGui::InputText("Value", buffer, sizeof(buffer)))
                a.value = buffer;
        }
        else if (name == "Transform")
        {
            auto& a = prim.GetAttribute<ecs::TransformAttribute>();
            ImGui::DragFloat3("Translation", &a.translation.x, 0.05f);
            ImGui::DragFloat4("Rotation", &a.rotation.x, 0.01f);
            ImGui::DragFloat3("Scaling", &a.scaling.x, 0.05f);
        }
        else if (name == "Camera")
        {
            auto& a = prim.GetAttribute<ecs::CameraAttribute>();
            ImGui::DragFloat3("Position", &a.position.x, 0.05f);
            ImGui::DragFloat3("Target", &a.target.x, 0.05f);
            ImGui::DragFloat3("Up", &a.up.x, 0.01f);
            ImGui::DragFloat("FOV", &a.fovDegrees, 0.5f, 1.f, 179.f);
            ImGui::DragFloat("Near", &a.zNear, 0.001f, 0.001f, 10.f);
            ImGui::DragFloat("Far", &a.zFar, 1.f, 1.f, 100000.f);
        }
        else if (name == "DirectionalLight")
        {
            auto& a = prim.GetAttribute<ecs::DirectionalLightAttribute>();
            ImGui::DragFloat3("Direction", &a.direction.x, 0.01f);
            ImGui::DragFloat3("Color", &a.color.x, 0.01f);
            ImGui::DragFloat("Irradiance", &a.irradiance, 0.05f, 0.f, 100.f);
            ImGui::DragFloat("Angular Size", &a.angularSize, 0.01f, 0.1f, 90.f);
        }
        else if (name == "Sky")
        {
            auto& a = prim.GetAttribute<ecs::SkyAttribute>();
            ImGui::DragFloat3("Sky Color", &a.skyColor.x, 0.01f);
            ImGui::DragFloat3("Horizon Color", &a.horizonColor.x, 0.01f);
            ImGui::DragFloat3("Ground Color", &a.groundColor.x, 0.01f);
            ImGui::DragFloat("Brightness", &a.brightness, 0.005f, 0.f, 10.f);
        }
        else if (name == "Model")
        {
            auto& a = prim.GetAttribute<ecs::ModelAttribute>();
            char buffer[512];
            snprintf(buffer, sizeof(buffer), "%s", a.path.c_str());
            if (ImGui::InputText("Path", buffer, sizeof(buffer)))
                a.path = buffer;
        }
    }

    void DrawContentPanel()
    {
        ImGui::Begin("Content");
        if (ImGui::CollapsingHeader("Archetypes", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (m_Archtypes.empty())
                ImGui::TextDisabled("No archetypes found.");
            for (const auto& path : m_Archtypes)
            {
                if (ImGui::Selectable(path.filename().generic_string().c_str()))
                {
                    ecs::Archtype archtype;
                    if (archtype.Load(path))
                    {
                        ecs::Prim prim = m_Stage.Instantiate(archtype);
                        m_Selected = prim.GetEntity();
                        Log("Instanced archetype " + path.filename().generic_string());
                    }
                    else
                    {
                        Log("Failed to load archetype " + path.generic_string());
                    }
                }
            }
        }
        if (ImGui::CollapsingHeader("Models", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (m_Models.empty())
                ImGui::TextDisabled("No models found.");
            for (const auto& path : m_Models)
            {
                if (ImGui::Selectable(path.filename().generic_string().c_str()))
                {
                    ecs::Prim prim;
                    if (m_Selected != entt::null)
                        prim = ecs::Prim(m_Stage.GetRegistry(), m_Selected);
                    if (prim.IsValid())
                    {
                        if (!prim.HasAttribute<ecs::ModelAttribute>())
                            ecs::AttributeRegistry::Get().AddDefault(m_Stage.GetRegistry(), prim.GetEntity(), "Model");
                        prim.GetAttribute<ecs::ModelAttribute>().path = std::filesystem::relative(path, m_RootDir).generic_string();
                        Log("Assigned model to selected prim");
                    }
                    else
                    {
                        Log("Select a prim to assign a model");
                    }
                }
            }
        }
        ImGui::End();
    }

    void DrawConsolePanel()
    {
        ImGui::Begin("Console");
        if (ImGui::SmallButton("Clear"))
            m_Log.clear();
        ImGui::Separator();
        ImGui::BeginChild("ConsoleScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (const std::string& line : m_Log)
            ImGui::TextUnformatted(line.c_str());
        if (m_LogScroll)
        {
            ImGui::SetScrollHereY(1.f);
            m_LogScroll = false;
        }
        ImGui::EndChild();
        ImGui::End();
    }

private:
    BindlessRayTracing m_Rt;
    bool m_RtReady = false;
    ecs::Stage m_Stage;
    std::filesystem::path m_StageFile;
    std::filesystem::path m_RootDir;
    std::filesystem::path m_BrowseDir;
    std::filesystem::path m_BrowseSelection;
    char m_SaveName[256] = {};
    bool m_ShowOpenDialog = false;
    bool m_ShowSaveDialog = false;
    entt::entity m_Selected = entt::null;
    std::vector<std::string> m_Log;
    bool m_LogScroll = false;
    bool m_DockInitialized = false;
    std::vector<std::filesystem::path> m_Archtypes;
    std::vector<std::filesystem::path> m_Models;

    app::FirstPersonCamera m_Camera;
    ImVec2 m_ViewportSize = ImVec2(800, 600);
    ImVec2 m_ViewportPos = ImVec2(0, 0);
    bool m_ViewportHovered = false;
    bool m_ViewportFocused = false;
    int m_ViewportWidth = 0;
    int m_ViewportHeight = 0;

    nvrhi::ShaderHandle m_VertexShader;
    nvrhi::ShaderHandle m_PixelShader;
    nvrhi::InputLayoutHandle m_InputLayout;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingSetHandle m_BindingSets[c_MaxDraws];
    nvrhi::BufferHandle m_ConstantBuffer;
    nvrhi::BufferHandle m_VertexBuffer;
    nvrhi::BufferHandle m_IndexBuffer;
    nvrhi::GraphicsPipelineHandle m_Pipeline;
    nvrhi::CommandListHandle m_CommandList;
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
    deviceParams.startMaximized = true;
    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, "Donut Editor"))
    {
        log::fatal("Cannot initialize a graphics device");
        return 1;
    }

    auto nativeFS = std::make_shared<vfs::NativeFileSystem>();
    auto rootFS = std::make_shared<vfs::RootFileSystem>();
    rootFS->mount("/shaders/donut", app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(api));
    rootFS->mount("/shaders/app", app::GetDirectoryWithExecutable() / "shaders/editor" / app::GetShaderTypeName(api));
    auto shaderFactory = std::make_shared<engine::ShaderFactory>(deviceManager->GetDevice(), rootFS, "/shaders");

    auto editor = std::make_shared<EditorUI>(deviceManager);
    if (!editor->Init(shaderFactory))
    {
        log::fatal("Cannot initialize the editor");
        return 1;
    }

    deviceManager->AddRenderPassToBack(editor.get());
    deviceManager->RunMessageLoop();
    deviceManager->RemoveRenderPass(editor.get());

    editor.reset();
    shaderFactory.reset();
    deviceManager->Shutdown();
    delete deviceManager;
    return 0;
}
