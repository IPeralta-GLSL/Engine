#include <donut/app/ApplicationBase.h>
#include <donut/app/DeviceManager.h>
#include <donut/app/imgui_renderer.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <donut/ecs/Attribute.h>
#include <donut/ecs/Stage.h>
#include <donut/engine/ShaderFactory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace donut;
using namespace donut::ecs;

namespace
{
    class StageHierarchyUI : public app::ImGui_Renderer
    {
    public:
        StageHierarchyUI(app::DeviceManager* deviceManager, Stage& stage)
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

        void SetStageFile(const std::filesystem::path& path)
        {
            m_StageFile = path;
            std::error_code ec;
            m_StageFileTime = std::filesystem::last_write_time(path, ec);
            m_HasStageFileTime = !ec;
        }

        bool ReloadStage()
        {
            if (m_StageFile.empty())
                return false;

            if (!m_Stage.Load(m_StageFile))
            {
                log::warning("Failed to reload stage '%s'", m_StageFile.generic_string().c_str());
                return false;
            }

            std::error_code ec;
            m_StageFileTime = std::filesystem::last_write_time(m_StageFile, ec);
            m_HasStageFileTime = !ec;

            m_Selected = entt::null;
            for (Prim prim : m_Stage.GetPrims())
            {
                m_Selected = prim.GetEntity();
                break;
            }

            log::info("Stage reloaded: %s", m_StageFile.generic_string().c_str());
            return true;
        }

        bool SaveStage()
        {
            if (m_StageFile.empty())
                return false;

            if (!m_Stage.Save(m_StageFile))
            {
                log::warning("Failed to save stage '%s'", m_StageFile.generic_string().c_str());
                return false;
            }

            std::error_code ec;
            m_StageFileTime = std::filesystem::last_write_time(m_StageFile, ec);
            m_HasStageFileTime = !ec;

            log::info("Stage saved: %s", m_StageFile.generic_string().c_str());
            return true;
        }

        void Animate(float elapsedTimeSeconds) override
        {
            app::ImGui_Renderer::Animate(elapsedTimeSeconds);

            if (m_StageFile.empty())
                return;

            m_ReloadPollTimer += elapsedTimeSeconds;
            if (m_ReloadPollTimer < 0.25f)
                return;
            m_ReloadPollTimer = 0.f;

            std::error_code ec;
            const auto fileTime = std::filesystem::last_write_time(m_StageFile, ec);
            if (ec)
                return;

            if (!m_HasStageFileTime || fileTime != m_StageFileTime)
                ReloadStage();
        }

    protected:
        void buildUI() override
        {
            const ImGuiIO& io = ImGui::GetIO();

            const float totalHeight = io.DisplaySize.y;
            const float hierarchyHeight = totalHeight * 0.42f;

            const ImGuiWindowFlags panelFlags =
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

            
            ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
            ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, hierarchyHeight));
            ImGui::Begin("Hierarchy", nullptr, panelFlags);

            ImGui::Text("Stage: %s", m_Stage.GetName().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu prims)", m_Stage.GetPrimCount());
            ImGui::SameLine();
            if (ImGui::Button("Save Stage"))
                SaveStage();
            ImGui::SameLine();
            if (ImGui::Button("Reload"))
                ReloadStage();
            ImGui::Separator();

            ImGui::BeginChild("HierarchyList", ImVec2(0, 0), false);
            std::vector<Prim> prims = m_Stage.GetPrims();
            for (Prim prim : prims)
            {
                if (!prim.IsValid())
                    continue;

                std::string label;
                if (prim.HasAttribute<NameAttribute>())
                    label = prim.GetAttribute<NameAttribute>().value;
                else
                    label = "Prim " + std::to_string(uint32_t(prim.GetEntity()));

                const bool selected = (m_Selected == prim.GetEntity());

                ImGui::PushID(int(uint32_t(prim.GetEntity())));
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
                    m_Selected = prim.GetEntity();
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    m_Selected = prim.GetEntity();
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::End();

            
            ImGui::SetNextWindowPos(ImVec2(0.f, hierarchyHeight));
            ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, totalHeight - hierarchyHeight));
            ImGui::Begin("Details", nullptr, panelFlags);

            Prim selectedPrim;
            if (m_Selected != entt::null)
                selectedPrim = Prim(m_Stage.GetRegistry(), m_Selected);

            if (!selectedPrim.IsValid())
            {
                ImGui::TextDisabled("Select a Prim in the Hierarchy to edit its attributes.");
            }
            else
            {
                DrawDetails(selectedPrim);
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

        void DrawDetails(Prim prim)
        {
            entt::entity entity = prim.GetEntity();

            ImGui::Text("Prim: %s", prim.HasAttribute<NameAttribute>()
                ? prim.GetAttribute<NameAttribute>().value.c_str()
                : "(unnamed)");
            ImGui::SameLine();
            ImGui::TextDisabled("entity #%u", uint32_t(entity));
            ImGui::Separator();

            
            if (ImGui::Button("+ Add Attribute"))
                ImGui::OpenPopup("AddAttributePopup");

            if (ImGui::BeginPopup("AddAttributePopup"))
            {
                bool any = false;
                for (const std::string& name : AttributeRegistry::Get().GetAttributeNames())
                {
                    if (AttributeRegistry::Get().Has(m_Stage.GetRegistry(), entity, name))
                        continue;
                    any = true;
                    if (ImGui::MenuItem(name.c_str()))
                        AttributeRegistry::Get().AddDefault(m_Stage.GetRegistry(), entity, name);
                }
                if (!any)
                    ImGui::TextDisabled("All attributes present");
                ImGui::EndPopup();
            }

            ImGui::Separator();

            DrawAttributeSection<NameAttribute>(prim, "Name", [](NameAttribute& attribute)
            {
                InputStringField("Name", attribute.value);
            });

            DrawAttributeSection<TagAttribute>(prim, "Tag", [](TagAttribute& attribute)
            {
                InputStringField("Tag", attribute.value);
            });

            DrawAttributeSection<TransformAttribute>(prim, "Transform", [](TransformAttribute& attribute)
            {
                ImGui::DragFloat3("Translation", &attribute.translation.x, 0.05f);
                ImGui::DragFloat4("Rotation", &attribute.rotation.x, 0.01f);
                ImGui::DragFloat3("Scaling", &attribute.scaling.x, 0.05f);
            });

            DrawAttributeSection<CameraAttribute>(prim, "Camera", [](CameraAttribute& attribute)
            {
                ImGui::DragFloat3("Position", &attribute.position.x, 0.05f);
                ImGui::DragFloat3("Target", &attribute.target.x, 0.05f);
                ImGui::DragFloat3("Up", &attribute.up.x, 0.05f);
                ImGui::DragFloat("FOV (deg)", &attribute.fovDegrees, 0.5f, 1.f, 179.f);
                ImGui::DragFloat("Near", &attribute.zNear, 0.001f, 0.001f, 10.f);
                ImGui::DragFloat("Far", &attribute.zFar, 1.f, 1.f, 100000.f);
            });

            DrawAttributeSection<DirectionalLightAttribute>(prim, "DirectionalLight", [](DirectionalLightAttribute& attribute)
            {
                ImGui::DragFloat3("Direction", &attribute.direction.x, 0.01f);
                ImGui::DragFloat3("Color", &attribute.color.x, 0.01f);
                ImGui::DragFloat("Irradiance", &attribute.irradiance, 0.05f, 0.f, 100.f);
                ImGui::DragFloat("Angular Size", &attribute.angularSize, 0.01f, 0.1f, 90.f);
            });

            DrawAttributeSection<SkyAttribute>(prim, "Sky", [](SkyAttribute& attribute)
            {
                ImGui::DragFloat3("Sky Color", &attribute.skyColor.x, 0.01f);
                ImGui::DragFloat3("Horizon Color", &attribute.horizonColor.x, 0.01f);
                ImGui::DragFloat3("Ground Color", &attribute.groundColor.x, 0.01f);
                ImGui::DragFloat("Brightness", &attribute.brightness, 0.005f, 0.f, 10.f);
                ImGui::DragFloat("Horizon Size", &attribute.horizonSize, 0.5f, 0.f, 90.f);
                ImGui::DragFloat("Glow Size", &attribute.glowSize, 0.1f, 0.f, 90.f);
                ImGui::DragFloat("Glow Intensity", &attribute.glowIntensity, 0.01f, 0.f, 1.f);
                ImGui::DragFloat("Glow Sharpness", &attribute.glowSharpness, 0.1f, 1.f, 10.f);
            });

            DrawAttributeSection<ModelAttribute>(prim, "Model", [](ModelAttribute& attribute)
            {
                InputStringField("Path", attribute.path);
            });
        }

        template<typename T, typename Editor>
        void DrawAttributeSection(Prim prim, const char* name, Editor editor)
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
                    AttributeRegistry::Get().Remove(prim.GetRegistry(), prim.GetEntity(), name);
            }
            ImGui::PopID();
        }

        Stage& m_Stage;
        entt::entity m_Selected = entt::null;

        std::filesystem::path m_StageFile;
        std::filesystem::file_time_type m_StageFileTime{};
        bool m_HasStageFileTime = false;
        float m_ReloadPollTimer = 0.f;
    };
}

#ifdef WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int __argc, const char** __argv)
#endif
{
    nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);

    app::DeviceManager* deviceManager = app::DeviceManager::Create(api);

    app::DeviceCreationParameters deviceParams;
    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, "Threshold Engine ECS: Stage Hierarchy"))
    {
        log::fatal("Cannot initialize a graphics device");
        return 1;
    }

    auto rootFs = std::make_shared<vfs::RootFileSystem>();
    std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(api);
    rootFs->mount("/shaders/donut", frameworkShaderPath);

    auto shaderFactory = std::make_shared<engine::ShaderFactory>(deviceManager->GetDevice(), rootFs, "/shaders");

    std::filesystem::path stagePath = app::GetDirectoryWithExecutable().parent_path() / "stages/intel_sponza.stage";
    for (int i = 1; i < __argc; i++)
    {
        if (__argv[i][0] != '-')
        {
            stagePath = __argv[i];
            break;
        }
    }

    Stage stage;
    if (!stage.Load(stagePath))
        log::warning("Failed to load stage '%s'", stagePath.generic_string().c_str());

    auto ui = std::make_shared<StageHierarchyUI>(deviceManager, stage);
    if (!ui->Init(shaderFactory))
    {
        log::fatal("Cannot initialize the ImGui renderer");
        return 1;
    }
    ui->SetStageFile(stagePath);

    deviceManager->AddRenderPassToBack(ui.get());
    deviceManager->RunMessageLoop();
    deviceManager->RemoveRenderPass(ui.get());

    ui.reset();
    shaderFactory.reset();
    rootFs.reset();

    deviceManager->Shutdown();
    delete deviceManager;

    return 0;
}
