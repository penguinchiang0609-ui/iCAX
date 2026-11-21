#pragma once

#include "ExtrusionRecognitionTypes.h"

#include <Geom2d_Curve.hxx>

namespace iCAX::ExtrusionRecognition
{
    inline constexpr double SectionContourFitToleranceMm = 0.001;

    struct SSectionContourFitSpan final
    {
        std::size_t EdgeIndex = 0;
        Handle(Geom2d_Curve) Curve;
        double First = 0;
        double Last = 0;
        bool Reversed = false;
        bool EntireEdge = true;
    };

    struct SSectionContourFitEdge final
    {
        std::string Kind;
        Handle(Geom2d_Curve) Curve;
        double First = 0;
        double Last = 0;
        bool Reversed = false;
        double DeviationUpperBound = 0;
        std::vector<SSectionContourFitSpan> Sources;
    };

    struct SSectionContourFitWire final
    {
        std::vector<SSectionContourFitEdge> Curves;
    };

    struct _EXTRUSION_RECOGNITION_EXP SSectionContourFit final
    {
        bool bSuccess = false;
        std::string Reason;
        std::vector<SSectionContourFitWire> Wires;
    };

    _EXTRUSION_RECOGNITION_EXP SSectionContourFit FitSectionContour(
        const SSectionWiresResult& section, double connectionTolerance);
}
