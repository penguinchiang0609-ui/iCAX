#include "pch.h"
#include "GeometryAlgo.h"


namespace
{
    bool IsFinite(const double value)
    {
        return std::isfinite(value);
    }

    double Square(const double value)
    {
        return value * value;
    }

    iCAX::GeometryData::Point3 Blend(
        const iCAX::GeometryData::Point3& left,
        const iCAX::GeometryData::Point3& right,
        const double parameter)
    {
        return {
            left.X + (right.X - left.X) * parameter,
            left.Y + (right.Y - left.Y) * parameter,
            left.Z + (right.Z - left.Z) * parameter
        };
    }

    iCAX::GeometryData::Direction2 DirectionOrDefault(const iCAX::GeometryData::Vector2& vector)
    {
        const auto length = std::sqrt(vector.X * vector.X + vector.Y * vector.Y);
        if (length <= 1.0e-12)
        {
            return {};
        }

        return { vector.X / length, vector.Y / length };
    }

    iCAX::GeometryData::Direction3 DirectionOrDefault(const iCAX::GeometryData::Vector3& vector)
    {
        const auto length = std::sqrt(vector.X * vector.X + vector.Y * vector.Y + vector.Z * vector.Z);
        if (length <= 1.0e-12)
        {
            return {};
        }

        return { vector.X / length, vector.Y / length, vector.Z / length };
    }

    bool ValidateBSplineCore(
        const std::int32_t degree,
        const std::size_t poleCount,
        const std::vector<double>& knots,
        const std::vector<std::int32_t>& multiplicities,
        std::string& error)
    {
        if (degree <= 0)
        {
            error = "B-spline degree must be greater than zero.";
            return false;
        }

        if (poleCount < static_cast<std::size_t>(degree + 1))
        {
            error = "B-spline pole count must be at least degree + 1.";
            return false;
        }

        if (knots.empty())
        {
            error = "B-spline knot vector must not be empty.";
            return false;
        }

        if (knots.size() != multiplicities.size())
        {
            error = "B-spline knot count must match multiplicity count.";
            return false;
        }

        for (std::size_t index = 1; index < knots.size(); ++index)
        {
            if (knots[index] < knots[index - 1])
            {
                error = "B-spline knots must be non-decreasing.";
                return false;
            }
        }

        const auto totalMultiplicity = std::accumulate(
            multiplicities.begin(),
            multiplicities.end(),
            std::int32_t { 0 });

        if (totalMultiplicity != static_cast<std::int32_t>(poleCount) + degree + 1)
        {
            error = "B-spline multiplicity sum must equal pole count + degree + 1.";
            return false;
        }

        error.clear();
        return true;
    }

    template <typename TRecord>
    bool CollectIds(
        const std::vector<TRecord>& records,
        const char* recordName,
        std::unordered_set<std::uint64_t>& ids,
        std::string& error)
    {
        for (const auto& record : records)
        {
            if (record.Id == 0)
            {
                error = std::string(recordName) + " id must not be zero.";
                return false;
            }

            if (!ids.insert(record.Id).second)
            {
                error = std::string(recordName) + " id is duplicated.";
                return false;
            }
        }

        return true;
    }

    bool RequireId(
        const std::unordered_set<std::uint64_t>& ids,
        const std::uint64_t id,
        const char* ownerName,
        const char* targetName,
        std::string& error)
    {
        if (id == 0)
        {
            error = std::string(ownerName) + " references empty " + targetName + " id.";
            return false;
        }

        if (!ids.contains(id))
        {
            error = std::string(ownerName) + " references missing " + targetName + " id.";
            return false;
        }

        return true;
    }

    bool RequireOptionalId(
        const std::unordered_set<std::uint64_t>& ids,
        const std::uint64_t id,
        const char* ownerName,
        const char* targetName,
        std::string& error)
    {
        if (id == 0)
        {
            return true;
        }

        return RequireId(ids, id, ownerName, targetName, error);
    }

    bool ValidateShapeRef(
        const iCAX::GeometryData::BRepShapeRef& shape,
        const std::unordered_set<std::uint64_t>& vertexIds,
        const std::unordered_set<std::uint64_t>& edgeIds,
        const std::unordered_set<std::uint64_t>& wireIds,
        const std::unordered_set<std::uint64_t>& faceIds,
        const std::unordered_set<std::uint64_t>& shellIds,
        const std::unordered_set<std::uint64_t>& solidIds,
        const std::unordered_set<std::uint64_t>& compSolidIds,
        const std::unordered_set<std::uint64_t>& compoundIds,
        const char* ownerName,
        std::string& error)
    {
        using iCAX::GeometryData::EBRepShapeKind;

        switch (shape.Kind)
        {
        case EBRepShapeKind::Vertex:
            return RequireId(vertexIds, shape.Id, ownerName, "BRepVertex", error);
        case EBRepShapeKind::Edge:
            return RequireId(edgeIds, shape.Id, ownerName, "BRepEdge", error);
        case EBRepShapeKind::Wire:
            return RequireId(wireIds, shape.Id, ownerName, "BRepWire", error);
        case EBRepShapeKind::Face:
            return RequireId(faceIds, shape.Id, ownerName, "BRepFace", error);
        case EBRepShapeKind::Shell:
            return RequireId(shellIds, shape.Id, ownerName, "BRepShell", error);
        case EBRepShapeKind::Solid:
            return RequireId(solidIds, shape.Id, ownerName, "BRepSolid", error);
        case EBRepShapeKind::CompSolid:
            return RequireId(compSolidIds, shape.Id, ownerName, "BRepCompSolid", error);
        case EBRepShapeKind::Compound:
            return RequireId(compoundIds, shape.Id, ownerName, "BRepCompound", error);
        default:
            error = std::string(ownerName) + " has unsupported shape kind.";
            return false;
        }
    }
}

namespace iCAX::GeometryAlgo
{
    bool IsValid(const GeometryData::Point2& point)
    {
        return IsFinite(point.X) && IsFinite(point.Y);
    }

    bool IsValid(const GeometryData::Point3& point)
    {
        return IsFinite(point.X) && IsFinite(point.Y) && IsFinite(point.Z);
    }

    bool IsValid(const GeometryData::Vector2& vector)
    {
        return IsFinite(vector.X) && IsFinite(vector.Y);
    }

    bool IsValid(const GeometryData::Vector3& vector)
    {
        return IsFinite(vector.X) && IsFinite(vector.Y) && IsFinite(vector.Z);
    }

    bool IsValid(const GeometryData::Quaternion& quaternion)
    {
        return IsFinite(quaternion.X)
            && IsFinite(quaternion.Y)
            && IsFinite(quaternion.Z)
            && IsFinite(quaternion.W);
    }

    GeometryData::Vector2 ToVector(const GeometryData::Point2& from, const GeometryData::Point2& to)
    {
        return { to.X - from.X, to.Y - from.Y };
    }

    GeometryData::Vector3 ToVector(const GeometryData::Point3& from, const GeometryData::Point3& to)
    {
        return { to.X - from.X, to.Y - from.Y, to.Z - from.Z };
    }

    GeometryData::Point2 Translate(const GeometryData::Point2& point, const GeometryData::Vector2& vector)
    {
        return { point.X + vector.X, point.Y + vector.Y };
    }

    GeometryData::Point3 Translate(const GeometryData::Point3& point, const GeometryData::Vector3& vector)
    {
        return { point.X + vector.X, point.Y + vector.Y, point.Z + vector.Z };
    }

    double Dot(const GeometryData::Vector2& left, const GeometryData::Vector2& right)
    {
        return left.X * right.X + left.Y * right.Y;
    }

    double Dot(const GeometryData::Vector3& left, const GeometryData::Vector3& right)
    {
        return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
    }

    double Cross(const GeometryData::Vector2& left, const GeometryData::Vector2& right)
    {
        return left.X * right.Y - left.Y * right.X;
    }

    GeometryData::Vector3 Cross(const GeometryData::Vector3& left, const GeometryData::Vector3& right)
    {
        return {
            left.Y * right.Z - left.Z * right.Y,
            left.Z * right.X - left.X * right.Z,
            left.X * right.Y - left.Y * right.X
        };
    }

    double LengthSquared(const GeometryData::Vector2& vector)
    {
        return Dot(vector, vector);
    }

    double LengthSquared(const GeometryData::Vector3& vector)
    {
        return Dot(vector, vector);
    }

    double Length(const GeometryData::Vector2& vector)
    {
        return std::sqrt(LengthSquared(vector));
    }

    double Length(const GeometryData::Vector3& vector)
    {
        return std::sqrt(LengthSquared(vector));
    }

    bool TryNormalize(const GeometryData::Vector2& vector, GeometryData::Direction2& direction, std::string& error, const double tolerance)
    {
        const auto length = Length(vector);
        if (length <= tolerance)
        {
            error = "Vector2 length is too small to normalize.";
            return false;
        }

        direction = { vector.X / length, vector.Y / length };
        error.clear();
        return true;
    }

    bool TryNormalize(const GeometryData::Vector3& vector, GeometryData::Direction3& direction, std::string& error, const double tolerance)
    {
        const auto length = Length(vector);
        if (length <= tolerance)
        {
            error = "Vector3 length is too small to normalize.";
            return false;
        }

        direction = { vector.X / length, vector.Y / length, vector.Z / length };
        error.clear();
        return true;
    }

    GeometryData::Vector2 Reversed(const GeometryData::Vector2& vector)
    {
        return { -vector.X, -vector.Y };
    }

    GeometryData::Vector3 Reversed(const GeometryData::Vector3& vector)
    {
        return { -vector.X, -vector.Y, -vector.Z };
    }

    double DistanceSquared(const GeometryData::Point2& left, const GeometryData::Point2& right)
    {
        return Square(left.X - right.X) + Square(left.Y - right.Y);
    }

    double DistanceSquared(const GeometryData::Point3& left, const GeometryData::Point3& right)
    {
        return Square(left.X - right.X) + Square(left.Y - right.Y) + Square(left.Z - right.Z);
    }

    double Distance(const GeometryData::Point2& left, const GeometryData::Point2& right)
    {
        return std::sqrt(DistanceSquared(left, right));
    }

    double Distance(const GeometryData::Point3& left, const GeometryData::Point3& right)
    {
        return std::sqrt(DistanceSquared(left, right));
    }

    GeometryData::Transform2 MakeTranslation2(const GeometryData::Vector2& offset)
    {
        GeometryData::Transform2 transform;
        transform.Matrix.Values[0][2] = offset.X;
        transform.Matrix.Values[1][2] = offset.Y;
        return transform;
    }

    GeometryData::Transform3 MakeTranslation3(const GeometryData::Vector3& offset)
    {
        GeometryData::Transform3 transform;
        transform.Matrix.Values[0][3] = offset.X;
        transform.Matrix.Values[1][3] = offset.Y;
        transform.Matrix.Values[2][3] = offset.Z;
        return transform;
    }

    GeometryData::Transform2 MakeScale2(const double x, const double y)
    {
        GeometryData::Transform2 transform;
        transform.Matrix.Values[0][0] = x;
        transform.Matrix.Values[1][1] = y;
        return transform;
    }

    GeometryData::Transform3 MakeScale3(const double x, const double y, const double z)
    {
        GeometryData::Transform3 transform;
        transform.Matrix.Values[0][0] = x;
        transform.Matrix.Values[1][1] = y;
        transform.Matrix.Values[2][2] = z;
        return transform;
    }

    GeometryData::Transform2 MakeRotation2(const double angle)
    {
        GeometryData::Transform2 transform;
        const auto c = std::cos(angle);
        const auto s = std::sin(angle);
        transform.Matrix.Values[0][0] = c;
        transform.Matrix.Values[0][1] = -s;
        transform.Matrix.Values[1][0] = s;
        transform.Matrix.Values[1][1] = c;
        return transform;
    }

    bool TryMakeRotation3(const GeometryData::Vector3& axis, const double angle, GeometryData::Transform3& transform, std::string& error)
    {
        GeometryData::Direction3 direction;
        if (!TryNormalize(axis, direction, error))
        {
            return false;
        }

        const auto x = direction.X;
        const auto y = direction.Y;
        const auto z = direction.Z;
        const auto c = std::cos(angle);
        const auto s = std::sin(angle);
        const auto t = 1.0 - c;

        transform = {};
        transform.Matrix.Values[0][0] = t * x * x + c;
        transform.Matrix.Values[0][1] = t * x * y - s * z;
        transform.Matrix.Values[0][2] = t * x * z + s * y;
        transform.Matrix.Values[1][0] = t * x * y + s * z;
        transform.Matrix.Values[1][1] = t * y * y + c;
        transform.Matrix.Values[1][2] = t * y * z - s * x;
        transform.Matrix.Values[2][0] = t * x * z - s * y;
        transform.Matrix.Values[2][1] = t * y * z + s * x;
        transform.Matrix.Values[2][2] = t * z * z + c;
        error.clear();
        return true;
    }

    GeometryData::Transform2 Multiply(const GeometryData::Transform2& left, const GeometryData::Transform2& right)
    {
        GeometryData::Transform2 result;
        for (std::size_t row = 0; row < 3; ++row)
        {
            for (std::size_t column = 0; column < 3; ++column)
            {
                result.Matrix.Values[row][column] = 0.0;
                for (std::size_t index = 0; index < 3; ++index)
                {
                    result.Matrix.Values[row][column] += left.Matrix.Values[row][index] * right.Matrix.Values[index][column];
                }
            }
        }

        return result;
    }

    GeometryData::Transform3 Multiply(const GeometryData::Transform3& left, const GeometryData::Transform3& right)
    {
        GeometryData::Transform3 result;
        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                result.Matrix.Values[row][column] = 0.0;
                for (std::size_t index = 0; index < 4; ++index)
                {
                    result.Matrix.Values[row][column] += left.Matrix.Values[row][index] * right.Matrix.Values[index][column];
                }
            }
        }

        return result;
    }

    GeometryData::Point2 Apply(const GeometryData::Transform2& transform, const GeometryData::Point2& point)
    {
        return {
            transform.Matrix.Values[0][0] * point.X + transform.Matrix.Values[0][1] * point.Y + transform.Matrix.Values[0][2],
            transform.Matrix.Values[1][0] * point.X + transform.Matrix.Values[1][1] * point.Y + transform.Matrix.Values[1][2]
        };
    }

    GeometryData::Point3 Apply(const GeometryData::Transform3& transform, const GeometryData::Point3& point)
    {
        return {
            transform.Matrix.Values[0][0] * point.X + transform.Matrix.Values[0][1] * point.Y + transform.Matrix.Values[0][2] * point.Z + transform.Matrix.Values[0][3],
            transform.Matrix.Values[1][0] * point.X + transform.Matrix.Values[1][1] * point.Y + transform.Matrix.Values[1][2] * point.Z + transform.Matrix.Values[1][3],
            transform.Matrix.Values[2][0] * point.X + transform.Matrix.Values[2][1] * point.Y + transform.Matrix.Values[2][2] * point.Z + transform.Matrix.Values[2][3]
        };
    }

    GeometryData::Vector2 Apply(const GeometryData::Transform2& transform, const GeometryData::Vector2& vector)
    {
        return {
            transform.Matrix.Values[0][0] * vector.X + transform.Matrix.Values[0][1] * vector.Y,
            transform.Matrix.Values[1][0] * vector.X + transform.Matrix.Values[1][1] * vector.Y
        };
    }

    GeometryData::Vector3 Apply(const GeometryData::Transform3& transform, const GeometryData::Vector3& vector)
    {
        return {
            transform.Matrix.Values[0][0] * vector.X + transform.Matrix.Values[0][1] * vector.Y + transform.Matrix.Values[0][2] * vector.Z,
            transform.Matrix.Values[1][0] * vector.X + transform.Matrix.Values[1][1] * vector.Y + transform.Matrix.Values[1][2] * vector.Z,
            transform.Matrix.Values[2][0] * vector.X + transform.Matrix.Values[2][1] * vector.Y + transform.Matrix.Values[2][2] * vector.Z
        };
    }

    GeometryData::Direction2 Apply(const GeometryData::Transform2& transform, const GeometryData::Direction2& direction)
    {
        return DirectionOrDefault(Apply(transform, GeometryData::Vector2 { direction.X, direction.Y }));
    }

    GeometryData::Direction3 Apply(const GeometryData::Transform3& transform, const GeometryData::Direction3& direction)
    {
        return DirectionOrDefault(Apply(transform, GeometryData::Vector3 { direction.X, direction.Y, direction.Z }));
    }

    GeometryData::Quaternion MakeQuaternionFromAxisAngle(const GeometryData::Direction3& axis, const double angle)
    {
        const auto half = angle * 0.5;
        const auto s = std::sin(half);
        return {
            axis.X * s,
            axis.Y * s,
            axis.Z * s,
            std::cos(half)
        };
    }

    GeometryData::Quaternion MakeQuaternionFromEulerZYX(const double roll, const double pitch, const double yaw)
    {
        const auto cr = std::cos(roll * 0.5);
        const auto sr = std::sin(roll * 0.5);
        const auto cp = std::cos(pitch * 0.5);
        const auto sp = std::sin(pitch * 0.5);
        const auto cy = std::cos(yaw * 0.5);
        const auto sy = std::sin(yaw * 0.5);

        return {
            sr * cp * cy - cr * sp * sy,
            cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy,
            cr * cp * cy + sr * sp * sy
        };
    }

    GeometryData::Quaternion Multiply(const GeometryData::Quaternion& left, const GeometryData::Quaternion& right)
    {
        return {
            left.W * right.X + left.X * right.W + left.Y * right.Z - left.Z * right.Y,
            left.W * right.Y - left.X * right.Z + left.Y * right.W + left.Z * right.X,
            left.W * right.Z + left.X * right.Y - left.Y * right.X + left.Z * right.W,
            left.W * right.W - left.X * right.X - left.Y * right.Y - left.Z * right.Z
        };
    }

    GeometryData::Quaternion Conjugate(const GeometryData::Quaternion& quaternion)
    {
        return { -quaternion.X, -quaternion.Y, -quaternion.Z, quaternion.W };
    }

    double MagnitudeSquared(const GeometryData::Quaternion& quaternion)
    {
        return Square(quaternion.X) + Square(quaternion.Y) + Square(quaternion.Z) + Square(quaternion.W);
    }

    double Magnitude(const GeometryData::Quaternion& quaternion)
    {
        return std::sqrt(MagnitudeSquared(quaternion));
    }

    bool TryNormalize(const GeometryData::Quaternion& quaternion, GeometryData::Quaternion& normalized, std::string& error, const double tolerance)
    {
        const auto magnitude = Magnitude(quaternion);
        if (magnitude <= tolerance)
        {
            error = "Quaternion magnitude is too small to normalize.";
            return false;
        }

        normalized = {
            quaternion.X / magnitude,
            quaternion.Y / magnitude,
            quaternion.Z / magnitude,
            quaternion.W / magnitude
        };
        error.clear();
        return true;
    }

    bool TryInverse(const GeometryData::Quaternion& quaternion, GeometryData::Quaternion& inverse, std::string& error, const double tolerance)
    {
        const auto magnitudeSquared = MagnitudeSquared(quaternion);
        if (magnitudeSquared <= tolerance)
        {
            error = "Quaternion magnitude is too small to invert.";
            return false;
        }

        const auto conjugate = Conjugate(quaternion);
        inverse = {
            conjugate.X / magnitudeSquared,
            conjugate.Y / magnitudeSquared,
            conjugate.Z / magnitudeSquared,
            conjugate.W / magnitudeSquared
        };
        error.clear();
        return true;
    }

    GeometryData::Vector3 Apply(const GeometryData::Quaternion& quaternion, const GeometryData::Vector3& vector)
    {
        GeometryData::Quaternion normalized;
        std::string error;
        if (!TryNormalize(quaternion, normalized, error))
        {
            return vector;
        }

        const GeometryData::Quaternion source { vector.X, vector.Y, vector.Z, 0.0 };
        const auto rotated = Multiply(Multiply(normalized, source), Conjugate(normalized));
        return { rotated.X, rotated.Y, rotated.Z };
    }

    GeometryData::Direction3 Apply(const GeometryData::Quaternion& quaternion, const GeometryData::Direction3& direction)
    {
        return DirectionOrDefault(Apply(quaternion, GeometryData::Vector3 { direction.X, direction.Y, direction.Z }));
    }

    GeometryData::Quaternion Slerp(const GeometryData::Quaternion& start, const GeometryData::Quaternion& end, const double factor)
    {
        GeometryData::Quaternion q0;
        GeometryData::Quaternion q1;
        std::string error;

        if (!TryNormalize(start, q0, error) || !TryNormalize(end, q1, error))
        {
            return {};
        }

        auto dot = q0.X * q1.X + q0.Y * q1.Y + q0.Z * q1.Z + q0.W * q1.W;
        if (dot < 0.0)
        {
            q1 = { -q1.X, -q1.Y, -q1.Z, -q1.W };
            dot = -dot;
        }

        constexpr double LinearThreshold = 0.9995;
        if (dot > LinearThreshold)
        {
            GeometryData::Quaternion result {
                q0.X + factor * (q1.X - q0.X),
                q0.Y + factor * (q1.Y - q0.Y),
                q0.Z + factor * (q1.Z - q0.Z),
                q0.W + factor * (q1.W - q0.W)
            };
            TryNormalize(result, result, error);
            return result;
        }

        const auto theta0 = std::acos(std::clamp(dot, -1.0, 1.0));
        const auto theta = theta0 * factor;
        const auto sinTheta = std::sin(theta);
        const auto sinTheta0 = std::sin(theta0);
        const auto s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
        const auto s1 = sinTheta / sinTheta0;

        return {
            s0 * q0.X + s1 * q1.X,
            s0 * q0.Y + s1 * q1.Y,
            s0 * q0.Z + s1 * q1.Z,
            s0 * q0.W + s1 * q1.W
        };
    }

    double SignedDistance(const GeometryData::Plane3& plane, const GeometryData::Point3& point)
    {
        return Dot(
            ToVector(plane.Location, point),
            GeometryData::Vector3 { plane.Normal.X, plane.Normal.Y, plane.Normal.Z });
    }

    GeometryData::Point3 Project(const GeometryData::Plane3& plane, const GeometryData::Point3& point)
    {
        const auto distance = SignedDistance(plane, point);
        return {
            point.X - plane.Normal.X * distance,
            point.Y - plane.Normal.Y * distance,
            point.Z - plane.Normal.Z * distance
        };
    }

    bool IsOn(const GeometryData::Plane3& plane, const GeometryData::Point3& point, const double tolerance)
    {
        return std::fabs(SignedDistance(plane, point)) <= tolerance;
    }

    GeometryData::Point2 Center(const GeometryData::BoundingBox2& box)
    {
        return { (box.Min.X + box.Max.X) * 0.5, (box.Min.Y + box.Max.Y) * 0.5 };
    }

    GeometryData::Point3 Center(const GeometryData::BoundingBox3& box)
    {
        return {
            (box.Min.X + box.Max.X) * 0.5,
            (box.Min.Y + box.Max.Y) * 0.5,
            (box.Min.Z + box.Max.Z) * 0.5
        };
    }

    bool Contains(const GeometryData::BoundingBox2& box, const GeometryData::Point2& point)
    {
        return !box.Empty
            && point.X >= box.Min.X
            && point.X <= box.Max.X
            && point.Y >= box.Min.Y
            && point.Y <= box.Max.Y;
    }

    bool Contains(const GeometryData::BoundingBox3& box, const GeometryData::Point3& point)
    {
        return !box.Empty
            && point.X >= box.Min.X
            && point.X <= box.Max.X
            && point.Y >= box.Min.Y
            && point.Y <= box.Max.Y
            && point.Z >= box.Min.Z
            && point.Z <= box.Max.Z;
    }

    GeometryData::BoundingBox2 Extend(const GeometryData::BoundingBox2& box, const GeometryData::Point2& point)
    {
        if (box.Empty)
        {
            return { point, point, false };
        }

        return {
            { std::min(box.Min.X, point.X), std::min(box.Min.Y, point.Y) },
            { std::max(box.Max.X, point.X), std::max(box.Max.Y, point.Y) },
            false
        };
    }

    GeometryData::BoundingBox3 Extend(const GeometryData::BoundingBox3& box, const GeometryData::Point3& point)
    {
        if (box.Empty)
        {
            return { point, point, false };
        }

        return {
            { std::min(box.Min.X, point.X), std::min(box.Min.Y, point.Y), std::min(box.Min.Z, point.Z) },
            { std::max(box.Max.X, point.X), std::max(box.Max.Y, point.Y), std::max(box.Max.Z, point.Z) },
            false
        };
    }

    GeometryData::BoundingBox2 Union(const GeometryData::BoundingBox2& left, const GeometryData::BoundingBox2& right)
    {
        if (left.Empty)
        {
            return right;
        }

        if (right.Empty)
        {
            return left;
        }

        return {
            { std::min(left.Min.X, right.Min.X), std::min(left.Min.Y, right.Min.Y) },
            { std::max(left.Max.X, right.Max.X), std::max(left.Max.Y, right.Max.Y) },
            false
        };
    }

    GeometryData::BoundingBox3 Union(const GeometryData::BoundingBox3& left, const GeometryData::BoundingBox3& right)
    {
        if (left.Empty)
        {
            return right;
        }

        if (right.Empty)
        {
            return left;
        }

        return {
            { std::min(left.Min.X, right.Min.X), std::min(left.Min.Y, right.Min.Y), std::min(left.Min.Z, right.Min.Z) },
            { std::max(left.Max.X, right.Max.X), std::max(left.Max.Y, right.Max.Y), std::max(left.Max.Z, right.Max.Z) },
            false
        };
    }

    GeometryData::Point2 Evaluate(const GeometryData::Line2& line, const double parameter)
    {
        return {
            line.Axis.Location.X + line.Axis.Direction.X * parameter,
            line.Axis.Location.Y + line.Axis.Direction.Y * parameter
        };
    }

    GeometryData::Point3 Evaluate(const GeometryData::Line3& line, const double parameter)
    {
        return {
            line.Axis.Location.X + line.Axis.Direction.X * parameter,
            line.Axis.Location.Y + line.Axis.Direction.Y * parameter,
            line.Axis.Location.Z + line.Axis.Direction.Z * parameter
        };
    }

    GeometryData::Point2 Evaluate(const GeometryData::Ray2& ray, const double parameter)
    {
        return Evaluate(GeometryData::Line2 { ray.Axis }, ray.First + parameter);
    }

    GeometryData::Point3 Evaluate(const GeometryData::Ray3& ray, const double parameter)
    {
        return Evaluate(GeometryData::Line3 { ray.Axis }, ray.First + parameter);
    }

    GeometryData::Point2 Evaluate(const GeometryData::Segment2& segment, const double parameter)
    {
        return {
            segment.Start.X + (segment.End.X - segment.Start.X) * parameter,
            segment.Start.Y + (segment.End.Y - segment.Start.Y) * parameter
        };
    }

    GeometryData::Point3 Evaluate(const GeometryData::Segment3& segment, const double parameter)
    {
        return Blend(segment.Start, segment.End, parameter);
    }

    GeometryData::Point2 Evaluate(const GeometryData::Circle2& circle, const double angle)
    {
        const auto cosValue = std::cos(angle);
        const auto sinValue = std::sin(angle);

        return {
            circle.Placement.Location.X + circle.Radius * (cosValue * circle.Placement.XDirection.X + sinValue * circle.Placement.YDirection.X),
            circle.Placement.Location.Y + circle.Radius * (cosValue * circle.Placement.XDirection.Y + sinValue * circle.Placement.YDirection.Y)
        };
    }

    GeometryData::Point3 Evaluate(const GeometryData::Circle3& circle, const double angle)
    {
        const auto cosValue = std::cos(angle);
        const auto sinValue = std::sin(angle);

        return {
            circle.Placement.Location.X + circle.Radius * (cosValue * circle.Placement.XDirection.X + sinValue * circle.Placement.YDirection.X),
            circle.Placement.Location.Y + circle.Radius * (cosValue * circle.Placement.XDirection.Y + sinValue * circle.Placement.YDirection.Y),
            circle.Placement.Location.Z + circle.Radius * (cosValue * circle.Placement.XDirection.Z + sinValue * circle.Placement.YDirection.Z)
        };
    }

    GeometryData::Point2 Evaluate(const GeometryData::Arc2& arc, const double parameter)
    {
        const auto delta = arc.EndAngle - arc.StartAngle;
        const auto angle = arc.CounterClockwise
            ? arc.StartAngle + delta * parameter
            : arc.StartAngle - delta * parameter;
        return Evaluate(arc.Basis, angle);
    }

    GeometryData::Point3 Evaluate(const GeometryData::Arc3& arc, const double parameter)
    {
        const auto delta = arc.EndAngle - arc.StartAngle;
        const auto angle = arc.CounterClockwise
            ? arc.StartAngle + delta * parameter
            : arc.StartAngle - delta * parameter;
        return Evaluate(arc.Basis, angle);
    }

    bool Validate(const GeometryData::BSpline2& curve, std::string& error)
    {
        return ValidateBSplineCore(curve.Degree, curve.Poles.size(), curve.Knots, curve.Multiplicities, error);
    }

    bool Validate(const GeometryData::BSpline3& curve, std::string& error)
    {
        return ValidateBSplineCore(curve.Degree, curve.Poles.size(), curve.Knots, curve.Multiplicities, error);
    }

    bool Validate(const GeometryData::NURBS2& curve, std::string& error)
    {
        if (curve.Weights.size() != curve.Poles.size())
        {
            error = "NURBS weight count must match pole count.";
            return false;
        }

        return ValidateBSplineCore(curve.Degree, curve.Poles.size(), curve.Knots, curve.Multiplicities, error);
    }

    bool Validate(const GeometryData::NURBS3& curve, std::string& error)
    {
        if (curve.Weights.size() != curve.Poles.size())
        {
            error = "NURBS weight count must match pole count.";
            return false;
        }

        return ValidateBSplineCore(curve.Degree, curve.Poles.size(), curve.Knots, curve.Multiplicities, error);
    }

    bool Validate(const GeometryData::BSplineSurface3& surface, std::string& error)
    {
        if (surface.UCount == 0 || surface.VCount == 0)
        {
            error = "B-spline surface grid size must not be zero.";
            return false;
        }

        const auto poleCount = static_cast<std::size_t>(surface.UCount) * static_cast<std::size_t>(surface.VCount);
        if (surface.Poles.size() != poleCount)
        {
            error = "B-spline surface pole count must match UCount * VCount.";
            return false;
        }

        if (!surface.Weights.empty() && surface.Weights.size() != poleCount)
        {
            error = "B-spline surface weight count must be zero or match pole count.";
            return false;
        }

        if (!ValidateBSplineCore(surface.UDegree, surface.UCount, surface.UKnots, surface.UMultiplicities, error))
        {
            return false;
        }

        return ValidateBSplineCore(surface.VDegree, surface.VCount, surface.VKnots, surface.VMultiplicities, error);
    }

    bool Validate(const GeometryData::BRepModel& model, std::string& error)
    {
        std::unordered_set<std::uint64_t> curve2Ids;
        std::unordered_set<std::uint64_t> curve3Ids;
        std::unordered_set<std::uint64_t> surface3Ids;
        std::unordered_set<std::uint64_t> triangulation3Ids;
        std::unordered_set<std::uint64_t> vertexIds;
        std::unordered_set<std::uint64_t> edgeIds;
        std::unordered_set<std::uint64_t> degeneratedEdgeIds;
        std::unordered_set<std::uint64_t> wireIds;
        std::unordered_set<std::uint64_t> faceIds;
        std::unordered_set<std::uint64_t> shellIds;
        std::unordered_set<std::uint64_t> solidIds;
        std::unordered_set<std::uint64_t> compSolidIds;
        std::unordered_set<std::uint64_t> compoundIds;

        if (!CollectIds(model.Curves2, "Curve2Record", curve2Ids, error)
            || !CollectIds(model.Curves3, "Curve3Record", curve3Ids, error)
            || !CollectIds(model.Surfaces3, "Surface3Record", surface3Ids, error)
            || !CollectIds(model.Triangulations3, "Triangulation3Record", triangulation3Ids, error)
            || !CollectIds(model.Vertices, "BRepVertex", vertexIds, error)
            || !CollectIds(model.Edges, "BRepEdge", edgeIds, error)
            || !CollectIds(model.Wires, "BRepWire", wireIds, error)
            || !CollectIds(model.Faces, "BRepFace", faceIds, error)
            || !CollectIds(model.Shells, "BRepShell", shellIds, error)
            || !CollectIds(model.Solids, "BRepSolid", solidIds, error)
            || !CollectIds(model.CompSolids, "BRepCompSolid", compSolidIds, error)
            || !CollectIds(model.Compounds, "BRepCompound", compoundIds, error))
        {
            return false;
        }

        for (const auto& edge : model.Edges)
        {
            if (!edge.Degenerated && !RequireId(curve3Ids, edge.Curve3Id, "BRepEdge", "Curve3Record", error))
            {
                return false;
            }

            if (edge.Degenerated)
            {
                degeneratedEdgeIds.insert(edge.Id);
            }

            if (!RequireId(vertexIds, edge.StartVertexId, "BRepEdge", "BRepVertex", error)
                || !RequireId(vertexIds, edge.EndVertexId, "BRepEdge", "BRepVertex", error))
            {
                return false;
            }

            if (edge.Curve3Id != 0 && !RequireId(curve3Ids, edge.Curve3Id, "BRepEdge", "Curve3Record", error))
            {
                return false;
            }
        }

        for (const auto& wire : model.Wires)
        {
            for (const auto& coedge : wire.Coedges)
            {
                if (!RequireId(edgeIds, coedge.EdgeId, "BRepCoedge", "BRepEdge", error)
                    || !RequireOptionalId(curve2Ids, coedge.Curve2Id, "BRepCoedge", "Curve2Record", error)
                    || !RequireOptionalId(vertexIds, coedge.StartVertexId, "BRepCoedge", "BRepVertex", error)
                    || !RequireOptionalId(vertexIds, coedge.EndVertexId, "BRepCoedge", "BRepVertex", error))
                {
                    return false;
                }

                if (degeneratedEdgeIds.contains(coedge.EdgeId) && coedge.Curve2Id == 0)
                {
                    error = "Degenerated BRepCoedge must reference a Curve2Record.";
                    return false;
                }
            }
        }

        for (const auto& face : model.Faces)
        {
            if (!RequireId(surface3Ids, face.Surface3Id, "BRepFace", "Surface3Record", error))
            {
                return false;
            }

            if (!RequireOptionalId(triangulation3Ids, face.Triangulation3Id, "BRepFace", "Triangulation3Record", error))
            {
                return false;
            }

            for (const auto wireId : face.WireIds)
            {
                if (!RequireId(wireIds, wireId, "BRepFace", "BRepWire", error))
                {
                    return false;
                }
            }
        }

        for (const auto& shell : model.Shells)
        {
            for (const auto faceId : shell.FaceIds)
            {
                if (!RequireId(faceIds, faceId, "BRepShell", "BRepFace", error))
                {
                    return false;
                }
            }
        }

        for (const auto& solid : model.Solids)
        {
            for (const auto shellId : solid.ShellIds)
            {
                if (!RequireId(shellIds, shellId, "BRepSolid", "BRepShell", error))
                {
                    return false;
                }
            }
        }

        for (const auto& compSolid : model.CompSolids)
        {
            for (const auto solidId : compSolid.SolidIds)
            {
                if (!RequireId(solidIds, solidId, "BRepCompSolid", "BRepSolid", error))
                {
                    return false;
                }
            }
        }

        for (const auto& compound : model.Compounds)
        {
            for (const auto& child : compound.Children)
            {
                if (!ValidateShapeRef(
                    child,
                    vertexIds,
                    edgeIds,
                    wireIds,
                    faceIds,
                    shellIds,
                    solidIds,
                    compSolidIds,
                    compoundIds,
                    "BRepCompound",
                    error))
                {
                    return false;
                }
            }
        }

        for (const auto& shape : model.RootShapes)
        {
            if (!ValidateShapeRef(
                shape,
                vertexIds,
                edgeIds,
                wireIds,
                faceIds,
                shellIds,
                solidIds,
                compSolidIds,
                compoundIds,
                "BRepModel",
                error))
            {
                return false;
            }
        }

        for (const auto faceId : model.RootFaceIds)
        {
            if (!RequireId(faceIds, faceId, "BRepModel", "BRepFace", error))
            {
                return false;
            }
        }

        for (const auto shellId : model.RootShellIds)
        {
            if (!RequireId(shellIds, shellId, "BRepModel", "BRepShell", error))
            {
                return false;
            }
        }

        for (const auto solidId : model.RootSolidIds)
        {
            if (!RequireId(solidIds, solidId, "BRepModel", "BRepSolid", error))
            {
                return false;
            }
        }

        error.clear();
        return true;
    }

    bool TryEvaluate(
        const GeometryData::Curve3& curve,
        const double parameter,
        GeometryData::Point3& point,
        std::string& error,
        const GeometryAdapter::IGeometryKernelAdapter* adapter)
    {
        bool handled = false;
        bool success = false;

        std::visit(
            [&](const auto& value)
            {
                using TValue = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<TValue, GeometryData::Line3>)
                {
                    point = Evaluate(value, parameter);
                    error.clear();
                    handled = true;
                    success = true;
                }
                else if constexpr (std::is_same_v<TValue, GeometryData::Ray3>)
                {
                    point = Evaluate(value, parameter);
                    error.clear();
                    handled = true;
                    success = true;
                }
                else if constexpr (std::is_same_v<TValue, GeometryData::Segment3>)
                {
                    point = Evaluate(value, parameter);
                    error.clear();
                    handled = true;
                    success = true;
                }
                else if constexpr (std::is_same_v<TValue, GeometryData::Circle3>)
                {
                    point = Evaluate(value, parameter);
                    error.clear();
                    handled = true;
                    success = true;
                }
                else if constexpr (std::is_same_v<TValue, GeometryData::Arc3>)
                {
                    point = Evaluate(value, parameter);
                    error.clear();
                    handled = true;
                    success = true;
                }
                else if constexpr (std::is_same_v<TValue, GeometryData::Polyline3>)
                {
                    handled = true;
                    if (value.Points.empty())
                    {
                        error = "Polyline has no points.";
                        success = false;
                        return;
                    }

                    if (value.Points.size() == 1)
                    {
                        point = value.Points.front();
                        error.clear();
                        success = true;
                        return;
                    }

                    const auto scaled = std::clamp(parameter, 0.0, 1.0) * static_cast<double>(value.Points.size() - 1);
                    const auto maxIndex = value.Points.size() - 2;
                    const auto index = std::min(static_cast<std::size_t>(std::floor(scaled)), maxIndex);
                    const auto local = scaled - static_cast<double>(index);
                    point = Blend(value.Points[index], value.Points[index + 1], local);
                    error.clear();
                    success = true;
                }
                else if constexpr (std::is_same_v<TValue, GeometryData::BSpline3>)
                {
                    handled = true;
                    if (adapter == nullptr)
                    {
                        error = "BSpline3 evaluation requires a geometry kernel adapter.";
                        success = false;
                        return;
                    }

                    success = adapter->TryEvaluate(value, parameter, point, error);
                }
                else if constexpr (std::is_same_v<TValue, GeometryData::NURBS3>)
                {
                    handled = true;
                    if (adapter == nullptr)
                    {
                        error = "NURBS3 evaluation requires a geometry kernel adapter.";
                        success = false;
                        return;
                    }

                    success = adapter->TryEvaluate(value, parameter, point, error);
                }
            },
            curve);

        if (!handled)
        {
            error = "Curve kind is not supported by GeometryAlgo::TryEvaluate.";
            return false;
        }

        return success;
    }
}
