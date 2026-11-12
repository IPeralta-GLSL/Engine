#pragma once

#include <donut/core/json.h>
#include <donut/ecs/Prim.h>

#include <filesystem>
#include <string>

namespace donut::ecs
{
    class Stage;

    
    class Archtype
    {
    public:
        Archtype() = default;
        explicit Archtype(std::string name) : m_Name(std::move(name)) { }

        const std::string& GetName() const { return m_Name; }
        void SetName(std::string name) { m_Name = std::move(name); }

        Json::Value& GetAttributes() { return m_Attributes; }
        const Json::Value& GetAttributes() const { return m_Attributes; }
        void SetAttributes(Json::Value attributes) { m_Attributes = std::move(attributes); }

        
        Prim Instantiate(Stage& stage) const;

        bool Load(const std::filesystem::path& filename);
        bool Save(const std::filesystem::path& filename) const;

    private:
        std::string m_Name = "UntitledArchtype";
        Json::Value m_Attributes = Json::Value(Json::objectValue);
    };
}
