#pragma once

#include <donut/core/json.h>
#include <donut/core/math/math.h>

#include <entt/entt.hpp>

#include <functional>
#include <string>
#include <vector>

namespace donut::ecs
{
    
    struct NameAttribute
    {
        std::string value;
    };

    struct TagAttribute
    {
        std::string value;
    };

    struct TransformAttribute
    {
        dm::float3 translation = dm::float3(0.f);
        dm::float4 rotation = dm::float4(0.f, 0.f, 0.f, 1.f);
        dm::float3 scaling = dm::float3(1.f);
    };

    struct CameraAttribute
    {
        dm::float3 position = dm::float3(0.f, 1.8f, 0.f);
        dm::float3 target = dm::float3(1.f, 1.8f, 0.f);
        dm::float3 up = dm::float3(0.f, 1.f, 0.f);
        float fovDegrees = 45.f;
        float zNear = 0.1f;
        float zFar = 1000.f;
    };

    struct DirectionalLightAttribute
    {
        dm::float3 direction = dm::float3(0.1f, -1.f, -0.15f);
        dm::float3 color = dm::float3(1.f);
        float irradiance = 4.f;
        float angularSize = 0.53f;
    };

    struct SkyAttribute
    {
        dm::float3 skyColor = dm::float3(0.17f, 0.37f, 0.65f);
        dm::float3 horizonColor = dm::float3(0.5f, 0.7f, 0.92f);
        dm::float3 groundColor = dm::float3(0.62f, 0.59f, 0.55f);
        float brightness = 0.12f;
        float horizonSize = 30.f;
        float glowSize = 5.f;
        float glowIntensity = 0.1f;
        float glowSharpness = 4.f;
    };

    struct ModelAttribute
    {
        std::string path;
    };

    
    
    class AttributeRegistry
    {
    public:
        static AttributeRegistry& Get();

        template<typename T>
        void RegisterAttribute(std::string name,
            std::function<void(const T&, Json::Value&)> serialize,
            std::function<T(const Json::Value&)> deserialize)
        {
            Descriptor descriptor;
            descriptor.name = std::move(name);
            descriptor.has = [](const entt::registry& registry, entt::entity entity)
            {
                return registry.all_of<T>(entity);
            };
            descriptor.serialize = [serialize](const entt::registry& registry, entt::entity entity, Json::Value& out)
            {
                serialize(registry.get<T>(entity), out);
            };
            descriptor.deserialize = [deserialize](entt::registry& registry, entt::entity entity, const Json::Value& in)
            {
                registry.emplace_or_replace<T>(entity, deserialize(in));
            };
            descriptor.addDefault = [](entt::registry& registry, entt::entity entity)
            {
                registry.emplace_or_replace<T>(entity);
            };
            descriptor.remove = [](entt::registry& registry, entt::entity entity)
            {
                registry.remove<T>(entity);
            };
            m_Attributes.push_back(std::move(descriptor));
        }

        bool IsRegistered(const std::string& name) const;
        std::vector<std::string> GetAttributeNames() const;

        bool Has(const entt::registry& registry, entt::entity entity, const std::string& name) const;
        bool AddDefault(entt::registry& registry, entt::entity entity, const std::string& name);
        bool Remove(entt::registry& registry, entt::entity entity, const std::string& name);

        
        void Serialize(const entt::registry& registry, entt::entity entity, Json::Value& out) const;

        
        void Deserialize(entt::registry& registry, entt::entity entity, const Json::Value& in) const;

        
        static void RegisterDefaults();

    private:
        AttributeRegistry() = default;

        struct Descriptor
        {
            std::string name;
            std::function<bool(const entt::registry&, entt::entity)> has;
            std::function<void(const entt::registry&, entt::entity, Json::Value&)> serialize;
            std::function<void(entt::registry&, entt::entity, const Json::Value&)> deserialize;
            std::function<void(entt::registry&, entt::entity)> addDefault;
            std::function<void(entt::registry&, entt::entity)> remove;
        };

        std::vector<Descriptor> m_Attributes;
    };
}
