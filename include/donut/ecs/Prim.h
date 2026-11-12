#pragma once

#include <entt/entt.hpp>

#include <string>
#include <utility>

namespace donut::ecs
{
    
    
    class Prim
    {
    public:
        Prim() = default;
        Prim(entt::registry& registry, entt::entity entity)
            : m_Registry(&registry), m_Entity(entity)
        { }

        bool IsValid() const
        {
            return m_Registry != nullptr && m_Entity != entt::null && m_Registry->valid(m_Entity);
        }

        explicit operator bool() const { return IsValid(); }

        entt::entity GetEntity() const { return m_Entity; }
        entt::registry& GetRegistry() const { return *m_Registry; }

        template<typename T, typename... Args>
        T& AddAttribute(Args&&... args)
        {
            return m_Registry->emplace<T>(m_Entity, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        T& GetOrAddAttribute(Args&&... args)
        {
            return m_Registry->get_or_emplace<T>(m_Entity, std::forward<Args>(args)...);
        }

        template<typename T>
        bool HasAttribute() const
        {
            return IsValid() && m_Registry->all_of<T>(m_Entity);
        }

        template<typename T>
        T& GetAttribute() { return m_Registry->get<T>(m_Entity); }

        template<typename T>
        const T& GetAttribute() const { return m_Registry->get<T>(m_Entity); }

        template<typename T>
        void RemoveAttribute() { m_Registry->remove<T>(m_Entity); }

        bool operator==(const Prim& other) const
        {
            return m_Registry == other.m_Registry && m_Entity == other.m_Entity;
        }

        bool operator!=(const Prim& other) const { return !(*this == other); }

    private:
        entt::registry* m_Registry = nullptr;
        entt::entity m_Entity = entt::null;
    };
}
