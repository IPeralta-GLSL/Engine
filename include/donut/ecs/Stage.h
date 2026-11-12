#pragma once

#include <donut/ecs/Archtype.h>
#include <donut/ecs/Prim.h>

#include <entt/entt.hpp>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace donut::ecs
{
    
    struct PrimTag { };

    
    class Stage
    {
    public:
        Stage();

        entt::registry& GetRegistry() { return m_Registry; }
        const entt::registry& GetRegistry() const { return m_Registry; }

        const std::string& GetName() const { return m_Name; }
        void SetName(std::string name) { m_Name = std::move(name); }

        Prim CreatePrim();
        void DestroyPrim(Prim prim);

        Prim Instantiate(const Archtype& archtype);

        Prim FindPrimByName(std::string_view name);
        std::vector<Prim> GetPrims();
        size_t GetPrimCount();

        bool Load(const std::filesystem::path& filename);
        bool Save(const std::filesystem::path& filename);

    private:
        entt::registry m_Registry;
        std::string m_Name = "UntitledStage";
        std::filesystem::path m_BaseDir;
    };
}
