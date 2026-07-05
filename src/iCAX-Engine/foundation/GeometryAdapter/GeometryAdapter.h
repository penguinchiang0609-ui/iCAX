#pragma once

#include "framework.h"
#include "../GeometryData/GeometryData.h"

#include <string>
#include <vector>

namespace iCAX::GeometryAdapter
{
    // 外部几何内核适配接口。实现类负责把 GeometryData 转换成 OCC、CGAL 等内核对象，
    // 调用内核完成复杂几何计算，再把结果转换回 GeometryData。
    class GEOMETRYADAPTER_API IGeometryKernelAdapter
    {
    public:
        virtual ~IGeometryKernelAdapter() = default;

        virtual bool TryEvaluate(
            const GeometryData::BSpline3& curve,
            double parameter,
            GeometryData::Point3& point,
            std::string& error) const = 0;

        virtual bool TryEvaluate(
            const GeometryData::NURBS3& curve,
            double parameter,
            GeometryData::Point3& point,
            std::string& error) const = 0;

        virtual bool TryTessellate(
            const GeometryData::Curve3& curve,
            double tolerance,
            std::vector<GeometryData::Point3>& points,
            std::string& error) const = 0;

        virtual bool TryTessellate(
            const GeometryData::Surface3& surface,
            double tolerance,
            GeometryData::Triangulation3& triangulation,
            std::string& error) const = 0;

        virtual bool TryTessellate(
            const GeometryData::BRepModel& model,
            double tolerance,
            GeometryData::Triangulation3& triangulation,
            std::string& error) const = 0;
    };
}
