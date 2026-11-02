#pragma once

#include <filesystem>
#include <istream>
#include <stdexcept>
#include <string>
#include <vector>

namespace iCAX::Application
{
    // One enabled product manifest per line. Relative paths are anchored to
    // the executable directory, never to the process working directory.
    inline std::vector<std::filesystem::path> ReadProductSelection(
        std::istream& Input, const std::filesystem::path& ExecutableDirectory)
    {
        std::vector<std::filesystem::path> Paths;
        std::string Line;
        bool FirstLine = true;
        while (std::getline(Input, Line))
        {
            if (FirstLine && Line.starts_with("\xEF\xBB\xBF")) Line.erase(0, 3);
            FirstLine = false;
            const auto Begin = Line.find_first_not_of(" \t\r\n");
            if (Begin == std::string::npos || Line[Begin] == '#') continue;
            Line = Line.substr(Begin, Line.find_last_not_of(" \t\r\n") - Begin + 1);
            auto Path = std::filesystem::path(std::u8string(Line.begin(), Line.end()));
            if (Path.is_relative()) Path = ExecutableDirectory / Path;
            Paths.push_back(std::filesystem::weakly_canonical(Path));
        }
        if (Input.bad()) throw std::runtime_error("Cannot read Application.Product.Setting");
        if (Paths.empty()) throw std::runtime_error("Application.Product.Setting contains no products");
        return Paths;
    }
}
