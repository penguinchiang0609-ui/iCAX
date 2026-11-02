#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "Services/IService.h"
#include "GeometryData/TubeNeutralGeometry.h"
#include <TopoDS_Shape.hxx>
#include <map>
#include <string>
#include <vector>

namespace iCAX::CAD
{
    struct SCadNamedShape final
    {
        std::string ID;
        TopoDS_Shape Shape;
    };
    struct SCadFeatureModelResult final
    {
        TopoDS_Shape Shape;
        SCadNamedShape Base;
        std::vector<SCadNamedShape> Cuts;
        // Names describe source topology and operation history. A name is only
        // emitted when it resolves uniquely; split/ambiguous boundaries have
        // no persistent name and must be selected again after a model change.
        std::map<std::uint64_t, std::string> EdgeNames;
        std::map<std::uint64_t, std::string> FaceNames;
    };
    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ICadFeatureModelService : public Services::IService
    {
    public:
        virtual ~ICadFeatureModelService() = default;
        virtual SCadFeatureModelResult DifferenceNative(const SCadNamedShape& Base,
            const std::vector<SCadNamedShape>& Cuts) const = 0;
        virtual GeometryData::BRepModel Evaluate(const GeometryData::Tube::CTubeNeutralGeometry& Model,
            const std::map<std::string, GeometryData::BRepModel>& Inputs) const = 0;
    };
}
