#include <donut/ecs/Archtype.h>

#include <donut/ecs/Attribute.h>
#include <donut/ecs/Stage.h>

#include "JsonFile.h"

namespace donut::ecs
{
    Prim Archtype::Instantiate(Stage& stage) const
    {
        Prim prim = stage.CreatePrim();
        AttributeRegistry::Get().Deserialize(stage.GetRegistry(), prim.GetEntity(), m_Attributes);
        return prim;
    }

    bool Archtype::Load(const std::filesystem::path& filename)
    {
        AttributeRegistry::RegisterDefaults();

        Json::Value root;
        if (!detail::ReadJsonFile(filename, root) || !root.isObject())
            return false;

        m_Name = root.get("name", "UntitledArchtype").asString();

        m_Attributes = root["attributes"];
        if (!m_Attributes.isObject())
            m_Attributes = Json::Value(Json::objectValue);

        return true;
    }

    bool Archtype::Save(const std::filesystem::path& filename) const
    {
        Json::Value root(Json::objectValue);
        root["name"] = m_Name;
        root["attributes"] = m_Attributes;
        return detail::WriteJsonFile(filename, root);
    }
}
