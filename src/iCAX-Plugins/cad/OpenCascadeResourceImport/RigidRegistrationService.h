#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "Services/IService.h"
#include <array>
#include <string>
#include <vector>

namespace iCAX::Geometry
{
    using RegistrationPoint = std::array<double, 3>;
    struct SRigidRegistrationRequest
    {
        // All points in mm, directions dimensionless. No scene or resource state.
        enum class Method { Points, Lines, Plane, PairedPoints } Kind = Method::Points;
        RegistrationPoint Origin{}, XReference{}, PlaneReference{};
        std::vector<RegistrationPoint> ModelPoints, MeasuredPoints;
        std::vector<double> Weights;
        double MinimumSpreadMm = 0.001;
        double MinimumConditionRatio = 0.0001;
        double MaximumResidualMm = 0.1;
    };
    struct SRigidRegistrationResult
    {
        // Row-major, maps source model coordinates into the target setup frame.
        std::array<double, 16> Transform{};
        std::vector<RegistrationPoint> ResidualVectors;
        std::vector<double> Residuals;
        double RmsMm = 0, MaximumResidualMm = 0, ModelConditionRatio = 1, MeasuredConditionRatio = 1;
        bool ReflectionCorrected = false, Qualified = false;
        std::string Diagnostic;
    };
    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP IRigidRegistrationService : public iCAX::Services::IService
    {
    public:
        virtual SRigidRegistrationResult Solve(const SRigidRegistrationRequest&) const = 0;
    };
}
