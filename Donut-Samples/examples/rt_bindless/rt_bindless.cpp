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
#include <donut/engine/BindingCache.h>
#include <donut/engine/View.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/render/BloomPass.h>
#include <donut/render/ToneMappingPasses.h>
#include <donut/render/SkyPass.h>
#include <donut/app/DeviceManager.h>
#include <donut/app/imgui_renderer.h>
#include <donut/ecs/Attribute.h>
#include <donut/ecs/Stage.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/math/math.h>
#include <nvrhi/utils.h>
#include <algorithm>
#include <cstring>
#include <string>

using namespace donut;
using namespace donut::math;

#include <donut/shaders/sky_cb.h>

#include "lighting_cb.h"

static const char* g_WindowTitle = "Donut Example: Bindless Ray Tracing";

struct FrameConstants
{
    uint32_t frameIndex = 0;
    uint32_t historyValid = 0;
    float blend = 0.1f;
    float pad = 0.f;
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
    }

protected:
    void buildUI() override
    {
        const ImGuiIO& io = ImGui::GetIO();
        const float width = 380.f;
        const float margin = 16.f;

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - width - margin, margin), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(width, io.DisplaySize.y * 0.42f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Hierarchy"))
        {
            ImGui::Text("Stage: %s", m_Stage.GetName().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu prims)", m_Stage.GetPrimCount());
            ImGui::Separator();

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
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - width - margin, io.DisplaySize.y * 0.42f + margin * 2.f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(width, io.DisplaySize.y * 0.58f - margin * 3.f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Details"))
        {
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
        }
        ImGui::End();
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

    ecs::Stage& m_Stage;
    entt::entity m_Selected = entt::null;
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

    nvrhi::TextureHandle m_AOHistory[2];
    nvrhi::BindingSetHandle m_BindingSet[2];
    int m_HistoryIndex = 0;
    bool m_HasPrevViewProj = false;
    dm::float4x4 m_PrevViewProj;

    std::shared_ptr<engine::ShaderFactory> m_ShaderFactory;
    std::shared_ptr<engine::DescriptorTableManager> m_DescriptorTable;
    std::unique_ptr<engine::Scene> m_Scene;
    nvrhi::TextureHandle m_ColorBuffer;
    nvrhi::TextureHandle m_LdrColor;
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
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1)
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
        m_AOHistory[0] = nullptr;
        m_AOHistory[1] = nullptr;
        m_BindingSet[0] = nullptr;
        m_BindingSet[1] = nullptr;
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

            nvrhi::TextureDesc historyDesc = desc;
            historyDesc.format = nvrhi::Format::RG16_FLOAT;
            historyDesc.isRenderTarget = false;
            historyDesc.isUAV = true;
            historyDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
            for (int i = 0; i < 2; i++)
            {
                historyDesc.debugName = i == 0 ? "AOHistory0" : "AOHistory1";
                m_AOHistory[i] = GetDevice()->createTexture(historyDesc);
            }

            m_HistoryIndex = 0;
            m_HasPrevViewProj = false;

            for (int i = 0; i < 2; i++)
            {
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
                    nvrhi::BindingSetItem::Texture_SRV(4, m_AOHistory[1 - i]),
                    nvrhi::BindingSetItem::Texture_UAV(0, m_ColorBuffer),
                    nvrhi::BindingSetItem::Texture_UAV(1, m_AOHistory[i])
                };
                m_BindingSet[i] = GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
            }
        }

        nvrhi::Viewport windowViewport(float(fbinfo.width), float(fbinfo.height));
        m_View.SetViewport(windowViewport);
        m_View.SetMatrices(m_Camera.GetWorldToViewMatrix(), perspProjD3DStyleReverse(dm::PI_f * 0.25f, windowViewport.width() / windowViewport.height(), 0.1f));
        m_View.UpdateCache();

        if (!m_BloomPass)
        {
            m_BloomPass = std::make_unique<render::BloomPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_HdrFramebuffer, m_View);
            render::ToneMappingPass::CreateParameters toneMappingCreateParams;
            m_ToneMappingPass = std::make_unique<render::ToneMappingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_LdrFramebuffer, m_View, toneMappingCreateParams);
        }

        m_CommandList->open();

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
        frameConstants.historyValid = m_HasPrevViewProj && (memcmp(&viewProj, &m_PrevViewProj, sizeof(viewProj)) == 0) ? 1u : 0u;
        frameConstants.blend = 0.1f;

        m_PrevViewProj = viewProj;
        m_HasPrevViewProj = true;

        m_CommandList->writeBuffer(m_FrameCB, &frameConstants, sizeof(frameConstants));

        nvrhi::BindingSetHandle currentBindingSet = m_BindingSet[m_HistoryIndex];

        if (m_RayPipeline)
        {
            nvrhi::rt::State state;
            state.shaderTable = m_ShaderTable;
            state.bindings = { currentBindingSet, m_DescriptorTable->GetDescriptorTable() };
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
            state.bindings = { currentBindingSet, m_DescriptorTable->GetDescriptorTable() };
            m_CommandList->setComputeState(state);

            m_CommandList->dispatch(
                dm::div_ceil(fbinfo.width, 16),
                dm::div_ceil(fbinfo.height, 16));
        }

        m_HistoryIndex = 1 - m_HistoryIndex;
        
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
