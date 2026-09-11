#pragma once

#include "ExtrusionRecognitionExport.h"
#include "ExtrusionRecognitionTypes.h"

#include "GeometryData/GeometryData.h"
#include "Services/IService.h"
#include "Services/ServicesHelper.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX::ExtrusionRecognition
{
    class _EXTRUSION_RECOGNITION_EXP IExtrudeRecognizesService
        : public iCAX::Services::IService
    {
    public:
        ~IExtrudeRecognizesService() override = default;

        virtual SExtrusionDirectionResult Recognize(
            IN const iCAX::GeometryData::BRepModel& Geometry_,
            IN const SExtrusionDirectionOptions& Options_ = {}) const = 0;

        virtual SSectionWiresResult ExtractSectionWires(
            // Geometry_ is expected to have its extrusion axis aligned to X.
            IN const iCAX::GeometryData::BRepModel& Geometry_,
            IN const SSectionWireOptions& Options_ = {}) const = 0;

        virtual SFeatureToolpathResult AnalyzeFeatureToolpaths(
            // Geometry_ is expected to be the reset X-axial BRep.
            IN const iCAX::GeometryData::BRepModel& Geometry_,
            IN const SSectionWiresResult& Section_,
            IN const SFeatureToolpathOptions& Options_ = {}) const = 0;
    };

    class _EXTRUSION_RECOGNITION_EXP CExtrudeRecognizesService final
        : public IExtrudeRecognizesService
    {
        AUTO_REGIST_SERVICE(
            iCAX::ExtrusionRecognition::IExtrudeRecognizesService,
            CExtrudeRecognizesService)

    public:
        CExtrudeRecognizesService() = default;
        ~CExtrudeRecognizesService() override = default;

        void OnLoad() override;
        void OnUnload() override;

        SExtrusionDirectionResult Recognize(
            IN const iCAX::GeometryData::BRepModel& Geometry_,
            IN const SExtrusionDirectionOptions& Options_ = {}) const override;

        SSectionWiresResult ExtractSectionWires(
            IN const iCAX::GeometryData::BRepModel& Geometry_,
            IN const SSectionWireOptions& Options_ = {}) const override;

        SFeatureToolpathResult AnalyzeFeatureToolpaths(
            IN const iCAX::GeometryData::BRepModel& Geometry_,
            IN const SSectionWiresResult& Section_,
            IN const SFeatureToolpathOptions& Options_ = {}) const override;
    };
}
