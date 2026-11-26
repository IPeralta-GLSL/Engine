#include <donut/ecs/Stage.h>

#include <donut/ecs/Attribute.h>

#include "JsonFile.h"

#include <algorithm>
#include <cmath>

namespace donut::ecs
{
    namespace
    {
        bool JsonEqual(const Json::Value& a, const Json::Value& b)
        {
            if (a.isNumeric() && b.isNumeric())
            {
                const double valueA = a.asDouble();
                const double valueB = b.asDouble();
                const double scale = std::max(1.0, std::max(std::abs(valueA), std::abs(valueB)));
                return std::abs(valueA - valueB) <= 1e-6 * scale;
            }

            if (a.type() != b.type())
                return false;

            switch (a.type())
            {
            case Json::objectValue:
            {
                if (a.size() != b.size())
                    return false;

                for (const std::string& name : a.getMemberNames())
                {
                    if (!b.isMember(name) || !JsonEqual(a[name], b[name]))
                        return false;
                }

                return true;
            }
            case Json::arrayValue:
            {
                if (a.size() != b.size())
                    return false;

                for (Json::ArrayIndex i = 0; i < a.size(); ++i)
                {
                    if (!JsonEqual(a[i], b[i]))
                        return false;
                }

                return true;
            }
            case Json::stringValue:
                return a.asString() == b.asString();
            case Json::booleanValue:
                return a.asBool() == b.asBool();
            case Json::nullValue:
                return true;
            default:
                return a == b;
            }
        }

        std::vector<entt::entity> OrderedPrimEntities(const entt::registry& registry)
        {
            std::vector<entt::entity> entities;
            for (entt::entity entity : registry.view<PrimTag>())
                entities.push_back(entity);

            std::sort(entities.begin(), entities.end(), [&registry](entt::entity a, entt::entity b)
            {
                const PrimIndex* indexA = registry.try_get<PrimIndex>(a);
                const PrimIndex* indexB = registry.try_get<PrimIndex>(b);
                const uint32_t valueA = indexA ? indexA->value : UINT32_MAX;
                const uint32_t valueB = indexB ? indexB->value : UINT32_MAX;

                if (valueA != valueB)
                    return valueA < valueB;

                return static_cast<uint32_t>(a) < static_cast<uint32_t>(b);
            });

            return entities;
        }
    }

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

        std::vector<entt::entity> entities = OrderedPrimEntities(m_Registry);
        prims.reserve(entities.size());
        for (entt::entity entity : entities)
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
            uint32_t primIndex = 0;

            for (const Json::Value& node : prims)
            {
                Prim prim = CreatePrim();
                m_Registry.emplace<PrimIndex>(prim.GetEntity(), PrimIndex{ primIndex });
                ++primIndex;

                if (node.isMember("archtype"))
                {
                    const std::string archtypeRef = node["archtype"].asString();

                    std::filesystem::path archtypePath = archtypeRef;
                    if (archtypePath.is_relative())
                        archtypePath = m_BaseDir / archtypePath;

                    Archtype archtype;
                    if (archtype.Load(archtypePath))
                    {
                        AttributeRegistry::Get().Deserialize(m_Registry, prim.GetEntity(), archtype.GetAttributes());
                    }

                    m_Registry.emplace<ArchtypeRef>(prim.GetEntity(), ArchtypeRef{ archtypeRef });
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

        m_BaseDir = filename.parent_path();

        Json::Value root(Json::objectValue);
        root["name"] = m_Name;

        Json::Value prims(Json::arrayValue);

        for (entt::entity entity : OrderedPrimEntities(m_Registry))
        {
            Json::Value primNode(Json::objectValue);

            Json::Value attributes(Json::objectValue);
            AttributeRegistry::Get().Serialize(m_Registry, entity, attributes);

            if (const ArchtypeRef* archtypeRef = m_Registry.try_get<ArchtypeRef>(entity))
            {
                primNode["archtype"] = archtypeRef->path;

                Json::Value baseAttributes(Json::objectValue);

                std::filesystem::path archtypePath = archtypeRef->path;
                if (archtypePath.is_relative())
                    archtypePath = m_BaseDir / archtypePath;

                Archtype archtype;
                if (archtype.Load(archtypePath))
                    baseAttributes = archtype.GetAttributes();

                Json::Value overrides(Json::objectValue);

                for (const std::string& name : attributes.getMemberNames())
                {
                    if (baseAttributes.isMember(name) && JsonEqual(baseAttributes[name], attributes[name]))
                        continue;

                    overrides[name] = attributes[name];
                }

                if (overrides.size() > 0)
                    primNode["attributes"] = overrides;
            }
            else
            {
                primNode["attributes"] = attributes;
            }

            prims.append(primNode);
        }

        root["prims"] = prims;

        return detail::WriteJsonFile(filename, root);
    }
}
