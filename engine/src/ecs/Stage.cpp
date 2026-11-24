#include <donut/ecs/Stage.h>

#include <donut/ecs/Attribute.h>

#include "JsonFile.h"

namespace donut::ecs
{
    Stage::Stage()
    {
        AttributeRegistry::RegisterDefaults();
    }

    Prim Stage::CreatePrim()
    {
        entt::entity entity = m_Registry.create();
        m_Registry.emplace<PrimTag>(entity);
        return Prim(m_Registry, entity);
    }

    void Stage::DestroyPrim(Prim prim)
    {
        if (prim.IsValid())
            m_Registry.destroy(prim.GetEntity());
    }

    Prim Stage::Instantiate(const Archtype& archtype)
    {
        return archtype.Instantiate(*this);
    }

    Prim Stage::FindPrimByName(std::string_view name)
    {
        for (entt::entity entity : m_Registry.view<PrimTag>())
        {
            Prim prim(m_Registry, entity);
            if (prim.HasAttribute<NameAttribute>() && prim.GetAttribute<NameAttribute>().value == name)
                return prim;
        }

        return Prim();
    }

    std::vector<Prim> Stage::GetPrims()
    {
        std::vector<Prim> prims;

        auto view = m_Registry.view<PrimTag>();
        prims.reserve(view.size());
        for (entt::entity entity : view)
            prims.emplace_back(m_Registry, entity);

        return prims;
    }

    size_t Stage::GetPrimCount()
    {
        return m_Registry.view<PrimTag>().size();
    }

    bool Stage::Load(const std::filesystem::path& filename)
    {
        AttributeRegistry::RegisterDefaults();

        Json::Value root;
        if (!detail::ReadJsonFile(filename, root) || !root.isObject())
            return false;

        m_Name = root.get("name", "UntitledStage").asString();
        m_BaseDir = filename.parent_path();

        m_Registry.clear();

        const Json::Value& prims = root["prims"];
        if (prims.isArray())
        {
            for (const Json::Value& node : prims)
            {
                Prim prim = CreatePrim();

                if (node.isMember("archtype"))
                {
                    std::filesystem::path archtypePath = node["archtype"].asString();
                    if (archtypePath.is_relative())
                        archtypePath = m_BaseDir / archtypePath;

                    Archtype archtype;
                    if (archtype.Load(archtypePath))
                    {
                        AttributeRegistry::Get().Deserialize(m_Registry, prim.GetEntity(), archtype.GetAttributes());
                    }
                }

                if (node.isMember("attributes"))
                {
                    AttributeRegistry::Get().Deserialize(m_Registry, prim.GetEntity(), node["attributes"]);
                }
            }
        }

        return true;
    }

    bool Stage::Save(const std::filesystem::path& filename)
    {
        AttributeRegistry::RegisterDefaults();

        Json::Value root(Json::objectValue);
        root["name"] = m_Name;

        Json::Value prims(Json::arrayValue);

        for (entt::entity entity : m_Registry.view<PrimTag>())
        {
            Json::Value primNode(Json::objectValue);

            Json::Value attributes(Json::objectValue);
            AttributeRegistry::Get().Serialize(m_Registry, entity, attributes);
            primNode["attributes"] = attributes;

            prims.append(primNode);
        }

        root["prims"] = prims;

        return detail::WriteJsonFile(filename, root);
    }
}
