#pragma once

#include "framework.h"
#include "../GeometryAdapter/GeometryAdapter.h"
#include "../GeometryData/GeometryData.h"

#include <string>

namespace iCAX::GeometryAlgo
{
    GEOMETRYALGO_API bool IsValid(const GeometryData::Point2& point);
    GEOMETRYALGO_API bool IsValid(const GeometryData::Point3& point);
    GEOMETRYALGO_API bool IsValid(const GeometryData::Vector2& vector);
    GEOMETRYALGO_API bool IsValid(const GeometryData::Vector3& vector);
    GEOMETRYALGO_API bool IsValid(const GeometryData::Quaternion& quaternion);

    GEOMETRYALGO_API GeometryData::Vector2 ToVector(const GeometryData::Point2& from, const GeometryData::Point2& to);
    GEOMETRYALGO_API GeometryData::Vector3 ToVector(const GeometryData::Point3& from, const GeometryData::Point3& to);
    GEOMETRYALGO_API GeometryData::Point2 Translate(const GeometryData::Point2& point, const GeometryData::Vector2& vector);
    GEOMETRYALGO_API GeometryData::Point3 Translate(const GeometryData::Point3& point, const GeometryData::Vector3& vector);
    GEOMETRYALGO_API double Dot(const GeometryData::Vector2& left, const GeometryData::Vector2& right);
    GEOMETRYALGO_API double Dot(const GeometryData::Vector3& left, const GeometryData::Vector3& right);
    GEOMETRYALGO_API double Cross(const GeometryData::Vector2& left, const GeometryData::Vector2& right);
    GEOMETRYALGO_API GeometryData::Vector3 Cross(const GeometryData::Vector3& left, const GeometryData::Vector3& right);
    GEOMETRYALGO_API double LengthSquared(const GeometryData::Vector2& vector);
    GEOMETRYALGO_API double LengthSquared(const GeometryData::Vector3& vector);
    GEOMETRYALGO_API double Length(const GeometryData::Vector2& vector);
    GEOMETRYALGO_API double Length(const GeometryData::Vector3& vector);
    GEOMETRYALGO_API bool TryNormalize(const GeometryData::Vector2& vector, GeometryData::Direction2& direction, std::string& error, double tolerance = 1.0e-12);
    GEOMETRYALGO_API bool TryNormalize(const GeometryData::Vector3& vector, GeometryData::Direction3& direction, std::string& error, double tolerance = 1.0e-12);
    GEOMETRYALGO_API GeometryData::Vector2 Reversed(const GeometryData::Vector2& vector);
    GEOMETRYALGO_API GeometryData::Vector3 Reversed(const GeometryData::Vector3& vector);

    GEOMETRYALGO_API double DistanceSquared(const GeometryData::Point2& left, const GeometryData::Point2& right);
    GEOMETRYALGO_API double DistanceSquared(const GeometryData::Point3& left, const GeometryData::Point3& right);
    GEOMETRYALGO_API double Distance(const GeometryData::Point2& left, const GeometryData::Point2& right);
    GEOMETRYALGO_API double Distance(const GeometryData::Point3& left, const GeometryData::Point3& right);

    GEOMETRYALGO_API GeometryData::Transform2 MakeTranslation2(const GeometryData::Vector2& offset);
    GEOMETRYALGO_API GeometryData::Transform3 MakeTranslation3(const GeometryData::Vector3& offset);
    GEOMETRYALGO_API GeometryData::Transform2 MakeScale2(double x, double y);
    GEOMETRYALGO_API GeometryData::Transform3 MakeScale3(double x, double y, double z);
    GEOMETRYALGO_API GeometryData::Transform2 MakeRotation2(double angle);
    GEOMETRYALGO_API bool TryMakeRotation3(const GeometryData::Vector3& axis, double angle, GeometryData::Transform3& transform, std::string& error);
    GEOMETRYALGO_API GeometryData::Transform2 Multiply(const GeometryData::Transform2& left, const GeometryData::Transform2& right);
    GEOMETRYALGO_API GeometryData::Transform3 Multiply(const GeometryData::Transform3& left, const GeometryData::Transform3& right);
    GEOMETRYALGO_API GeometryData::Point2 Apply(const GeometryData::Transform2& transform, const GeometryData::Point2& point);
    GEOMETRYALGO_API GeometryData::Point3 Apply(const GeometryData::Transform3& transform, const GeometryData::Point3& point);
    GEOMETRYALGO_API GeometryData::Vector2 Apply(const GeometryData::Transform2& transform, const GeometryData::Vector2& vector);
    GEOMETRYALGO_API GeometryData::Vector3 Apply(const GeometryData::Transform3& transform, const GeometryData::Vector3& vector);
    GEOMETRYALGO_API GeometryData::Direction2 Apply(const GeometryData::Transform2& transform, const GeometryData::Direction2& direction);
    GEOMETRYALGO_API GeometryData::Direction3 Apply(const GeometryData::Transform3& transform, const GeometryData::Direction3& direction);

    GEOMETRYALGO_API GeometryData::Quaternion MakeQuaternionFromAxisAngle(const GeometryData::Direction3& axis, double angle);
    GEOMETRYALGO_API GeometryData::Quaternion MakeQuaternionFromEulerZYX(double roll, double pitch, double yaw);
    GEOMETRYALGO_API GeometryData::Quaternion Multiply(const GeometryData::Quaternion& left, const GeometryData::Quaternion& right);
    GEOMETRYALGO_API GeometryData::Quaternion Conjugate(const GeometryData::Quaternion& quaternion);
    GEOMETRYALGO_API double MagnitudeSquared(const GeometryData::Quaternion& quaternion);
    GEOMETRYALGO_API double Magnitude(const GeometryData::Quaternion& quaternion);
    GEOMETRYALGO_API bool TryNormalize(const GeometryData::Quaternion& quaternion, GeometryData::Quaternion& normalized, std::string& error, double tolerance = 1.0e-12);
    GEOMETRYALGO_API bool TryInverse(const GeometryData::Quaternion& quaternion, GeometryData::Quaternion& inverse, std::string& error, double tolerance = 1.0e-12);
    GEOMETRYALGO_API GeometryData::Vector3 Apply(const GeometryData::Quaternion& quaternion, const GeometryData::Vector3& vector);
    GEOMETRYALGO_API GeometryData::Direction3 Apply(const GeometryData::Quaternion& quaternion, const GeometryData::Direction3& direction);
    GEOMETRYALGO_API GeometryData::Quaternion Slerp(const GeometryData::Quaternion& start, const GeometryData::Quaternion& end, double factor);

    GEOMETRYALGO_API double SignedDistance(const GeometryData::Plane3& plane, const GeometryData::Point3& point);
    GEOMETRYALGO_API GeometryData::Point3 Project(const GeometryData::Plane3& plane, const GeometryData::Point3& point);
    GEOMETRYALGO_API bool IsOn(const GeometryData::Plane3& plane, const GeometryData::Point3& point, double tolerance = 1.0e-9);

    GEOMETRYALGO_API GeometryData::Point2 Center(const GeometryData::BoundingBox2& box);
    GEOMETRYALGO_API GeometryData::Point3 Center(const GeometryData::BoundingBox3& box);
    GEOMETRYALGO_API bool Contains(const GeometryData::BoundingBox2& box, const GeometryData::Point2& point);
    GEOMETRYALGO_API bool Contains(const GeometryData::BoundingBox3& box, const GeometryData::Point3& point);
    GEOMETRYALGO_API GeometryData::BoundingBox2 Extend(const GeometryData::BoundingBox2& box, const GeometryData::Point2& point);
    GEOMETRYALGO_API GeometryData::BoundingBox3 Extend(const GeometryData::BoundingBox3& box, const GeometryData::Point3& point);
    GEOMETRYALGO_API GeometryData::BoundingBox2 Union(const GeometryData::BoundingBox2& left, const GeometryData::BoundingBox2& right);
    GEOMETRYALGO_API GeometryData::BoundingBox3 Union(const GeometryData::BoundingBox3& left, const GeometryData::BoundingBox3& right);

    GEOMETRYALGO_API GeometryData::Point2 Evaluate(const GeometryData::Line2& line, double parameter);
    GEOMETRYALGO_API GeometryData::Point3 Evaluate(const GeometryData::Line3& line, double parameter);
    GEOMETRYALGO_API GeometryData::Point2 Evaluate(const GeometryData::Ray2& ray, double parameter);
    GEOMETRYALGO_API GeometryData::Point3 Evaluate(const GeometryData::Ray3& ray, double parameter);
    GEOMETRYALGO_API GeometryData::Point2 Evaluate(const GeometryData::Segment2& segment, double parameter);
    GEOMETRYALGO_API GeometryData::Point3 Evaluate(const GeometryData::Segment3& segment, double parameter);
    GEOMETRYALGO_API GeometryData::Point2 Evaluate(const GeometryData::Circle2& circle, double angle);
    GEOMETRYALGO_API GeometryData::Point3 Evaluate(const GeometryData::Circle3& circle, double angle);
    GEOMETRYALGO_API GeometryData::Point2 Evaluate(const GeometryData::Arc2& arc, double parameter);
    GEOMETRYALGO_API GeometryData::Point3 Evaluate(const GeometryData::Arc3& arc, double parameter);

    GEOMETRYALGO_API bool Validate(const GeometryData::BSpline2& curve, std::string& error);
    GEOMETRYALGO_API bool Validate(const GeometryData::BSpline3& curve, std::string& error);
    GEOMETRYALGO_API bool Validate(const GeometryData::NURBS2& curve, std::string& error);
    GEOMETRYALGO_API bool Validate(const GeometryData::NURBS3& curve, std::string& error);
    GEOMETRYALGO_API bool Validate(const GeometryData::BSplineSurface3& surface, std::string& error);
    GEOMETRYALGO_API bool Validate(const GeometryData::BRepModel& model, std::string& error);

    // 基础曲线由本模块直接计算；B 样条、NURBS 等复杂曲线可传入 adapter 下沉到 OCC/CGAL。
    GEOMETRYALGO_API bool TryEvaluate(
        const GeometryData::Curve3& curve,
        double parameter,
        GeometryData::Point3& point,
        std::string& error,
        const GeometryAdapter::IGeometryKernelAdapter* adapter = nullptr);
}
