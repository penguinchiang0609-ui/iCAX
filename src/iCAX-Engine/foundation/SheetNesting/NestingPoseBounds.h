#pragma once
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <limits>

namespace iCAX::SheetNesting::Detail
{
    // Scoring-only preparation for one immutable source and one exact stored
    // orientation. The source must outlive this object and remain unchanged.
    // This does not replace any material, collision or containment geometry.
    class PreparedPoseBounds
    {
        const Shape& Source;
        std::uint64_t AngleBits=0;
        bool Mirrored=false,Ready=false;
        // Actual source vertices, not rounded local minima or cached widths.
        std::array<Point,4> Witnesses{}; // min X, max X, min Y, max Y

        static bool SupportedArithmetic()
        {
#if defined(__FAST_MATH__) || (defined(_M_FP_FAST) && _M_FP_FAST) || !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD != 0
            return false;
#else
            return std::numeric_limits<double>::is_iec559&&
                std::numeric_limits<double>::radix==2&&std::numeric_limits<double>::digits==53&&
                AxisSafety::ArithmeticSupported();
#endif
        }
        static bool Finite(Point point)
        {return std::isfinite(point.X)&&std::isfinite(point.Y);}

    public:
        PreparedPoseBounds(const Shape& shape,Pose orientation):Source(shape),
            AngleBits(std::bit_cast<std::uint64_t>(orientation.AngleDegrees)),Mirrored(orientation.Mirrored)
        {
            if(!SupportedArithmetic()||shape.Radius>0||shape.Outer.empty()||
                !std::isfinite(shape.Radius)||!std::isfinite(orientation.AngleDegrees))return;
            orientation.X=orientation.Y=0;
            const auto first=shape.Outer.front();
            if(!Finite(first))return;
            const auto initial=Transform(first,orientation);
            if(!Finite(initial))return;
            Witnesses.fill(first);
            double x0=initial.X,x1=initial.X,y0=initial.Y,y1=initial.Y;
            for(const auto point:shape.Outer)
            {
                if(!Finite(point))return;
                const auto transformed=Transform(point,orientation);
                if(!Finite(transformed))return;
                // Strict comparisons retain the first equal extremum, matching
                // Bounds' std::min/max source order, including signed zero.
                if(transformed.X<x0){x0=transformed.X;Witnesses[0]=point;}
                if(x1<transformed.X){x1=transformed.X;Witnesses[1]=point;}
                if(transformed.Y<y0){y0=transformed.Y;Witnesses[2]=point;}
                if(y1<transformed.Y){y1=transformed.Y;Witnesses[3]=point;}
            }
            Ready=true;
        }

        Rect At(Pose pose)const
        {
            if(!Ready||!SupportedArithmetic()||pose.Mirrored!=Mirrored||
                std::bit_cast<std::uint64_t>(pose.AngleDegrees)!=AngleBits||
                !std::isfinite(pose.X)||!std::isfinite(pose.Y))return Bounds(Source,pose);
            // Transform has the same fixed rounded rotation expression for
            // every point, followed by one addition of the same translation.
            // IEEE binary64 addition is monotone. Its rotated extremum source
            // therefore still attains the actual translated extremum. Re-run
            // the original expression at each witness: adding cached minima,
            // or retaining a cached width, would change cancellation rounding.
            const auto lowX=Transform(Witnesses[0],pose),highX=Transform(Witnesses[1],pose);
            const auto lowY=Transform(Witnesses[2],pose),highY=Transform(Witnesses[3],pose);
            if(!Finite(lowX)||!Finite(highX)||!Finite(lowY)||!Finite(highY))return Bounds(Source,pose);
            return {lowX.X,lowY.Y,highX.X-lowX.X,highY.Y-lowY.Y};
        }
    };
}
