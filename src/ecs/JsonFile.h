#pragma once

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace donut::ecs::detail
{
    inline bool ReadJsonFile(const std::filesystem::path& filename, Json::Value& out)
    {
        std::ifstream in(filename, std::ios::binary);
        if (!in)
            return false;

        Json::CharReaderBuilder builder;
        std::string errors;
        return Json::parseFromStream(builder, in, &out, &errors);
    }

    inline bool WriteJsonFile(const std::filesystem::path& filename, const Json::Value& value)
    {
        std::ofstream out(filename, std::ios::binary);
        if (!out)
            return false;

        Json::StreamWriterBuilder builder;
        builder["indentation"] = "\t";
        out << Json::writeString(builder, value);
        return out.good();
    }
}
