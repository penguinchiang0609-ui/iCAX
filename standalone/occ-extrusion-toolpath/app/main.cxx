#include "etp/Analyzer.hxx"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void PrintUsage()
    {
        std::cout
            << "Usage: etp_cli <input.step|iges|brep> <output.json> "
               "[--tolerance value] [--samples count] [--scale value]\n";
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        PrintUsage();
        return EXIT_FAILURE;
    }

    try
    {
        etp::AnalyzerOptions options;
        for (int index = 3; index < argc; ++index)
        {
            const std::string argument(argv[index]);
            if (argument == "--tolerance" && index + 1 < argc)
            {
                options.DistanceTolerance = std::stod(argv[++index]);
            }
            else if (argument == "--samples" && index + 1 < argc)
            {
                options.ToolpathSamples = static_cast<std::size_t>(std::stoul(argv[++index]));
            }
            else if (argument == "--scale" && index + 1 < argc)
            {
                options.InputLengthScale = std::stod(argv[++index]);
            }
            else
            {
                throw std::invalid_argument("unknown or incomplete option: " + argument);
            }
        }

        const auto input = std::filesystem::path(argv[1]);
        const auto output = std::filesystem::path(argv[2]);
        const auto shape = etp::ReadCadFile(input);
        const etp::ExtrusionToolpathAnalyzer analyzer(options);
        const auto parts = analyzer.AnalyzeAssembly(shape);
        etp::WriteAnalysisJson(parts, output);

        std::size_t featureCount = 0;
        std::size_t unitCount = 0;
        for (const auto& part : parts)
        {
            featureCount += part.Features.size();
            for (const auto& feature : part.Features)
                unitCount += feature.SweepUnits.size();
        }
        std::cout << "Analyzed " << parts.size() << " part(s), "
            << featureCount << " feature group(s), and "
            << unitCount << " sweep unit(s).\n"
            << "Result: " << output.string() << '\n';
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "etp_cli: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
