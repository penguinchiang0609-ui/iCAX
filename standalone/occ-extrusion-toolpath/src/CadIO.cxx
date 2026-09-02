#include "etp/Analyzer.hxx"

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <IGESControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace etp
{
    TopoDS_Shape ReadCadFile(const std::filesystem::path& path)
    {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
        const auto fileName = path.string();

        if (extension == ".step" || extension == ".stp")
        {
            STEPControl_Reader reader;
            if (reader.ReadFile(fileName.c_str()) != IFSelect_RetDone)
                throw std::runtime_error("failed to read STEP file: " + fileName);
            if (reader.TransferRoots() <= 0)
                throw std::runtime_error("STEP file contains no transferable root: " + fileName);
            return reader.OneShape();
        }

        if (extension == ".iges" || extension == ".igs")
        {
            IGESControl_Reader reader;
            if (reader.ReadFile(fileName.c_str()) != IFSelect_RetDone)
                throw std::runtime_error("failed to read IGES file: " + fileName);
            if (reader.TransferRoots() <= 0)
                throw std::runtime_error("IGES file contains no transferable root: " + fileName);
            return reader.OneShape();
        }

        if (extension == ".brep" || extension == ".rle")
        {
            TopoDS_Shape shape;
            BRep_Builder builder;
            if (!BRepTools::Read(shape, fileName.c_str(), builder))
                throw std::runtime_error("failed to read BREP file: " + fileName);
            return shape;
        }

        throw std::invalid_argument(
            "unsupported CAD extension; expected .step, .stp, .iges, .igs, or .brep");
    }
}
