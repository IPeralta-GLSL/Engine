#include <donut/ecs/Attribute.h>

namespace donut::ecs
{
    namespace
    {
        void SerializeName(const NameAttribute& attribute, Json::Value& out)
        {
            out["value"] = attribute.value;
        }

        NameAttribute DeserializeName(const Json::Value& in)
        {
            NameAttribute attribute;
            attribute.value = in.get("value", "").asString();
            return attribute;
        }

        void SerializeTag(const TagAttribute& attribute, Json::Value& out)
        {
            out["value"] = attribute.value;
        }

        TagAttribute DeserializeTag(const Json::Value& in)
        {
            TagAttribute attribute;
            attribute.value = in.get("value", "").asString();
            return attribute;
        }

        void SerializeTransform(const TransformAttribute& attribute, Json::Value& out)
        {
            donut::json::Write<dm::float3>(out["translation"], attribute.translation);
            donut::json::Write<dm::float4>(out["rotation"], attribute.rotation);
            donut::json::Write<dm::float3>(out["scaling"], attribute.scaling);
        }

        TransformAttribute DeserializeTransform(const Json::Value& in)
        {
            TransformAttribute attribute;
            attribute.translation = donut::json::Read<dm::float3>(in["translation"], attribute.translation);
            attribute.rotation = donut::json::Read<dm::float4>(in["rotation"], attribute.rotation);
            attribute.scaling = donut::json::Read<dm::float3>(in["scaling"], attribute.scaling);
            return attribute;
        }

        void SerializeCamera(const CameraAttribute& attribute, Json::Value& out)
        {
            donut::json::Write<dm::float3>(out["position"], attribute.position);
            donut::json::Write<dm::float3>(out["target"], attribute.target);
            donut::json::Write<dm::float3>(out["up"], attribute.up);
            donut::json::Write<float>(out["fovDegrees"], attribute.fovDegrees);
            donut::json::Write<float>(out["zNear"], attribute.zNear);
            donut::json::Write<float>(out["zFar"], attribute.zFar);
        }

        CameraAttribute DeserializeCamera(const Json::Value& in)
        {
            CameraAttribute attribute;
            attribute.position = donut::json::Read<dm::float3>(in["position"], attribute.position);
            attribute.target = donut::json::Read<dm::float3>(in["target"], attribute.target);
            attribute.up = donut::json::Read<dm::float3>(in["up"], attribute.up);
            attribute.fovDegrees = donut::json::Read<float>(in["fovDegrees"], attribute.fovDegrees);
            attribute.zNear = donut::json::Read<float>(in["zNear"], attribute.zNear);
            attribute.zFar = donut::json::Read<float>(in["zFar"], attribute.zFar);
            return attribute;
        }

        void SerializeDirectionalLight(const DirectionalLightAttribute& attribute, Json::Value& out)
        {
            donut::json::Write<dm::float3>(out["direction"], attribute.direction);
            donut::json::Write<dm::float3>(out["color"], attribute.color);
            donut::json::Write<float>(out["irradiance"], attribute.irradiance);
            donut::json::Write<float>(out["angularSize"], attribute.angularSize);
        }

        DirectionalLightAttribute DeserializeDirectionalLight(const Json::Value& in)
        {
            DirectionalLightAttribute attribute;
            attribute.direction = donut::json::Read<dm::float3>(in["direction"], attribute.direction);
            attribute.color = donut::json::Read<dm::float3>(in["color"], attribute.color);
            attribute.irradiance = donut::json::Read<float>(in["irradiance"], attribute.irradiance);
            attribute.angularSize = donut::json::Read<float>(in["angularSize"], attribute.angularSize);
            return attribute;
        }

        void SerializeSky(const SkyAttribute& attribute, Json::Value& out)
        {
            donut::json::Write<dm::float3>(out["skyColor"], attribute.skyColor);
            donut::json::Write<dm::float3>(out["horizonColor"], attribute.horizonColor);
            donut::json::Write<dm::float3>(out["groundColor"], attribute.groundColor);
            donut::json::Write<float>(out["brightness"], attribute.brightness);
            donut::json::Write<float>(out["horizonSize"], attribute.horizonSize);
            donut::json::Write<float>(out["glowSize"], attribute.glowSize);
            donut::json::Write<float>(out["glowIntensity"], attribute.glowIntensity);
            donut::json::Write<float>(out["glowSharpness"], attribute.glowSharpness);
        }

        SkyAttribute DeserializeSky(const Json::Value& in)
        {
            SkyAttribute attribute;
            attribute.skyColor = donut::json::Read<dm::float3>(in["skyColor"], attribute.skyColor);
            attribute.horizonColor = donut::json::Read<dm::float3>(in["horizonColor"], attribute.horizonColor);
            attribute.groundColor = donut::json::Read<dm::float3>(in["groundColor"], attribute.groundColor);
            attribute.brightness = donut::json::Read<float>(in["brightness"], attribute.brightness);
            attribute.horizonSize = donut::json::Read<float>(in["horizonSize"], attribute.horizonSize);
            attribute.glowSize = donut::json::Read<float>(in["glowSize"], attribute.glowSize);
            attribute.glowIntensity = donut::json::Read<float>(in["glowIntensity"], attribute.glowIntensity);
            attribute.glowSharpness = donut::json::Read<float>(in["glowSharpness"], attribute.glowSharpness);
            return attribute;
        }

        void SerializeModel(const ModelAttribute& attribute, Json::Value& out)
        {
            donut::json::Write<std::string>(out["path"], attribute.path);
        }

        ModelAttribute DeserializeModel(const Json::Value& in)
        {
            ModelAttribute attribute;
            attribute.path = donut::json::Read<std::string>(in["path"], attribute.path);
            return attribute;
        }
    }

    AttributeRegistry& AttributeRegistry::Get()
    {
        static AttributeRegistry instance;
        return instance;
    }

    bool AttributeRegistry::IsRegistered(const std::string& name) const
    {
        for (const Descriptor& descriptor : m_Attributes)
        {
            if (descriptor.name == name)
                return true;
        }
        return false;
    }

    std::vector<std::string> AttributeRegistry::GetAttributeNames() const
    {
        std::vector<std::string> names;
        names.reserve(m_Attributes.size());
        for (const Descriptor& descriptor : m_Attributes)
            names.push_back(descriptor.name);
        return names;
    }

    bool AttributeRegistry::Has(const entt::registry& registry, entt::entity entity, const std::string& name) const
    {
        for (const Descriptor& descriptor : m_Attributes)
        {
            if (descriptor.name == name)
                return descriptor.has(registry, entity);
        }
        return false;
    }

    bool AttributeRegistry::AddDefault(entt::registry& registry, entt::entity entity, const std::string& name)
    {
        for (const Descriptor& descriptor : m_Attributes)
        {
            if (descriptor.name == name)
            {
                descriptor.addDefault(registry, entity);
                return true;
            }
        }
        return false;
    }

    bool AttributeRegistry::Remove(entt::registry& registry, entt::entity entity, const std::string& name)
    {
        for (const Descriptor& descriptor : m_Attributes)
        {
            if (descriptor.name == name)
            {
                descriptor.remove(registry, entity);
                return true;
            }
        }
        return false;
    }

    void AttributeRegistry::Serialize(const entt::registry& registry, entt::entity entity, Json::Value& out) const
    {
        out = Json::Value(Json::objectValue);

        for (const Descriptor& descriptor : m_Attributes)
        {
            if (!descriptor.has(registry, entity))
                continue;

            Json::Value node;
            descriptor.serialize(registry, entity, node);
            out[descriptor.name] = node;
        }
    }

    void AttributeRegistry::Deserialize(entt::registry& registry, entt::entity entity, const Json::Value& in) const
    {
        if (!in.isObject())
            return;

        for (auto it = in.begin(); it != in.end(); ++it)
        {
            const std::string name = it.name();

            for (const Descriptor& descriptor : m_Attributes)
            {
                if (descriptor.name == name)
                {
                    descriptor.deserialize(registry, entity, *it);
                    break;
                }
            }
        }
    }

    void AttributeRegistry::RegisterDefaults()
    {
        static bool registered = false;
        if (registered)
            return;
        registered = true;

        AttributeRegistry& registry = Get();
        registry.RegisterAttribute<NameAttribute>("Name", SerializeName, DeserializeName);
        registry.RegisterAttribute<TagAttribute>("Tag", SerializeTag, DeserializeTag);
        registry.RegisterAttribute<TransformAttribute>("Transform", SerializeTransform, DeserializeTransform);
        registry.RegisterAttribute<CameraAttribute>("Camera", SerializeCamera, DeserializeCamera);
        registry.RegisterAttribute<DirectionalLightAttribute>("DirectionalLight", SerializeDirectionalLight, DeserializeDirectionalLight);
        registry.RegisterAttribute<SkyAttribute>("Sky", SerializeSky, DeserializeSky);
        registry.RegisterAttribute<ModelAttribute>("Model", SerializeModel, DeserializeModel);
    }
}
