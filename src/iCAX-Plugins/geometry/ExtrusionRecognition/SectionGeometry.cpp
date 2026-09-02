#include "pch.h"
#include "SectionGeometry.h"

namespace iCAX::ExtrusionRecognition
{
namespace
{
    using namespace iCAX::GeometryData;

    struct STriangle final
    {
        Point3 A;
        Point3 B;
        Point3 C;
    };

    struct SSegment2 final
    {
        Point2 A;
        Point2 B;
        bool bUsed = false;
    };

    struct SSlice final
    {
        SSectionSnapshot Section;
        double dStation = 0.0;
    };

    Vector3 Add(IN const Vector3& A_, IN const Vector3& B_)
    {
        return { A_.X + B_.X, A_.Y + B_.Y, A_.Z + B_.Z };
    }

    Vector3 Scale(IN const Vector3& V_, IN double dScale_)
    {
        return { V_.X * dScale_, V_.Y * dScale_, V_.Z * dScale_ };
    }

    Vector3 ToVector(IN const Point3& Point_)
    {
        return { Point_.X, Point_.Y, Point_.Z };
    }

    Vector3 Difference(IN const Point3& A_, IN const Point3& B_)
    {
        return { A_.X - B_.X, A_.Y - B_.Y, A_.Z - B_.Z };
    }

    double Dot(IN const Vector3& A_, IN const Vector3& B_)
    {
        return A_.X * B_.X + A_.Y * B_.Y + A_.Z * B_.Z;
    }

    Vector3 Cross(IN const Vector3& A_, IN const Vector3& B_)
    {
        return {
            A_.Y * B_.Z - A_.Z * B_.Y,
            A_.Z * B_.X - A_.X * B_.Z,
            A_.X * B_.Y - A_.Y * B_.X
        };
    }

    double Length(IN const Vector3& V_)
    {
        return std::sqrt(Dot(V_, V_));
    }

    bool TryNormalize(IN const Vector3& V_, OUT Direction3& Direction_)
    {
        const auto _Length = Length(V_);
        if (_Length <= 1.0e-12)
        {
            return false;
        }
        Direction_ = { V_.X / _Length, V_.Y / _Length, V_.Z / _Length };
        return true;
    }

    Vector3 ToVector(IN const Direction3& Direction_)
    {
        return { Direction_.X, Direction_.Y, Direction_.Z };
    }

    double Distance2(IN const Point2& A_, IN const Point2& B_)
    {
        return std::hypot(A_.X - B_.X, A_.Y - B_.Y);
    }

    double SignedArea(IN const std::vector<Point2>& Points_)
    {
        if (Points_.size() < 3)
        {
            return 0.0;
        }
        double _Area = 0.0;
        for (std::size_t _Index = 0; _Index < Points_.size(); ++_Index)
        {
            const auto& _A = Points_[_Index];
            const auto& _B = Points_[(_Index + 1) % Points_.size()];
            _Area += _A.X * _B.Y - _B.X * _A.Y;
        }
        return 0.5 * _Area;
    }

    Point2 PolygonCentroid(IN const std::vector<Point2>& Points_)
    {
        Point2 _Result;
        const auto _Area = SignedArea(Points_);
        if (std::abs(_Area) <= 1.0e-12)
        {
            if (Points_.empty())
            {
                return _Result;
            }
            for (const auto& _Point : Points_)
            {
                _Result.X += _Point.X;
                _Result.Y += _Point.Y;
            }
            _Result.X /= static_cast<double>(Points_.size());
            _Result.Y /= static_cast<double>(Points_.size());
            return _Result;
        }
        double _X = 0.0;
        double _Y = 0.0;
        for (std::size_t _Index = 0; _Index < Points_.size(); ++_Index)
        {
            const auto& _A = Points_[_Index];
            const auto& _B = Points_[(_Index + 1) % Points_.size()];
            const auto _Cross = _A.X * _B.Y - _B.X * _A.Y;
            _X += (_A.X + _B.X) * _Cross;
            _Y += (_A.Y + _B.Y) * _Cross;
        }
        return { _X / (6.0 * _Area), _Y / (6.0 * _Area) };
    }

    std::pair<Direction2, Direction2> PrincipalAxes(
        IN const std::vector<Point2>& Points_)
    {
        if (Points_.empty())
        {
            return { { 1.0, 0.0 }, { 0.0, 1.0 } };
        }
        Point2 _Center;
        for (const auto& _Point : Points_)
        {
            _Center.X += _Point.X;
            _Center.Y += _Point.Y;
        }
        _Center.X /= static_cast<double>(Points_.size());
        _Center.Y /= static_cast<double>(Points_.size());
        double _XX = 0.0;
        double _XY = 0.0;
        double _YY = 0.0;
        for (const auto& _Point : Points_)
        {
            const auto _X = _Point.X - _Center.X;
            const auto _Y = _Point.Y - _Center.Y;
            _XX += _X * _X;
            _XY += _X * _Y;
            _YY += _Y * _Y;
        }
        const auto _Angle = 0.5 * std::atan2(2.0 * _XY, _XX - _YY);
        Direction2 _Long{ std::cos(_Angle), std::sin(_Angle) };
        return { _Long, { -_Long.Y, _Long.X } };
    }

    std::vector<STriangle> CollectTriangles(IN const BRepModel& Geometry_)
    {
        std::vector<STriangle> _Triangles;
        for (const auto& _Record : Geometry_.Triangulations3)
        {
            const auto& _Mesh = _Record.Geometry;
            for (const auto& _Triangle : _Mesh.Triangles)
            {
                if (_Triangle[0] >= _Mesh.Vertices.size()
                    || _Triangle[1] >= _Mesh.Vertices.size()
                    || _Triangle[2] >= _Mesh.Vertices.size())
                {
                    continue;
                }
                _Triangles.push_back({
                    _Mesh.Vertices[_Triangle[0]],
                    _Mesh.Vertices[_Triangle[1]],
                    _Mesh.Vertices[_Triangle[2]]
                });
            }
        }
        return _Triangles;
    }

    struct SAxisEvidence final
    {
        Direction3 Direction;
        double dWeight = 0.0;
    };

    double TriangleArea(
        IN const Point3& A_,
        IN const Point3& B_,
        IN const Point3& C_)
    {
        return 0.5 * Length(Cross(Difference(B_, A_), Difference(C_, A_)));
    }

    double TriangulationArea(IN const Triangulation3& Mesh_)
    {
        double _Area = 0.0;
        for (const auto& _Triangle : Mesh_.Triangles)
        {
            if (_Triangle[0] >= Mesh_.Vertices.size()
                || _Triangle[1] >= Mesh_.Vertices.size()
                || _Triangle[2] >= Mesh_.Vertices.size())
            {
                continue;
            }
            _Area += TriangleArea(
                Mesh_.Vertices[_Triangle[0]],
                Mesh_.Vertices[_Triangle[1]],
                Mesh_.Vertices[_Triangle[2]]);
        }
        return _Area;
    }

    void AddAxisEvidence(
        IN OUT std::vector<SAxisEvidence>& Evidence_,
        IN const Direction3& Direction_,
        IN double dWeight_,
        IN const Direction3& PrincipalAxis_)
    {
        Direction3 _Direction;
        if (!TryNormalize(ToVector(Direction_), _Direction))
        {
            return;
        }
        auto _Dot = Dot(ToVector(_Direction), ToVector(PrincipalAxis_));
        // BRep 解析证据只用来把 PCA 方向吸附到附近的精确构造方向，不能把
        // 一个短而宽的零件任意跳到完全不同的截面边方向。
        constexpr double _MinimumSnapDot = 0.992546151641322; // cos(7 degrees)
        if (std::abs(_Dot) < _MinimumSnapDot)
        {
            return;
        }
        if (_Dot < 0.0)
        {
            _Direction = { -_Direction.X, -_Direction.Y, -_Direction.Z };
        }
        const auto _Weight = std::max(1.0e-9, dWeight_)
            * (0.5 + 0.5 * std::abs(_Dot));
        for (auto& _Existing : Evidence_)
        {
            if (std::abs(Dot(
                ToVector(_Existing.Direction),
                ToVector(_Direction))) >= 1.0 - 1.0e-8)
            {
                _Existing.dWeight += _Weight;
                return;
            }
        }
        Evidence_.push_back({ _Direction, _Weight });
    }

    Direction3 RefineAxisFromBRepEvidence(
        IN const BRepModel& Geometry_,
        IN const Direction3& PrincipalAxis_,
        OUT bool& bRefined_)
    {
        std::unordered_map<std::uint64_t, const Triangulation3*> _Triangulations;
        for (const auto& _Record : Geometry_.Triangulations3)
        {
            _Triangulations[_Record.Id] = &_Record.Geometry;
        }
        std::unordered_map<std::uint64_t, const Surface3*> _Surfaces;
        for (const auto& _Record : Geometry_.Surfaces3)
        {
            _Surfaces[_Record.Id] = &_Record.Geometry;
        }
        std::unordered_map<std::uint64_t, const Curve3*> _Curves;
        for (const auto& _Record : Geometry_.Curves3)
        {
            _Curves[_Record.Id] = &_Record.Geometry;
        }

        struct SPlaneEvidence final
        {
            Direction3 Normal;
            double dArea = 0.0;
        };
        std::vector<SPlaneEvidence> _Planes;
        std::vector<SAxisEvidence> _Evidence;
        for (const auto& _Face : Geometry_.Faces)
        {
            const auto _SurfaceIter = _Surfaces.find(_Face.Surface3Id);
            if (_SurfaceIter == _Surfaces.end())
            {
                continue;
            }
            const auto _MeshIter = _Triangulations.find(_Face.Triangulation3Id);
            const auto _Area = _MeshIter == _Triangulations.end()
                ? 1.0
                : std::max(1.0e-9, TriangulationArea(*_MeshIter->second));
            std::visit([&_Evidence, &_Planes, _Area, &PrincipalAxis_](IN const auto& Surface_)
            {
                using T = std::decay_t<decltype(Surface_)>;
                if constexpr (std::is_same_v<T, CylindricalSurface3>)
                {
                    AddAxisEvidence(
                        _Evidence,
                        Surface_.Placement.ZDirection,
                        _Area,
                        PrincipalAxis_);
                }
                else if constexpr (std::is_same_v<T, PlaneSurface3>)
                {
                    _Planes.push_back({ Surface_.Placement.ZDirection, _Area });
                }
            }, *_SurfaceIter->second);
        }

        // 平面碎壁本身没有拉伸参数方向；任意两组非平行侧壁法向的交线，
        // 就是它们共享的平移方向。用面面积作为证据权重累加同向候选。
        for (std::size_t _Left = 0; _Left < _Planes.size(); ++_Left)
        {
            for (std::size_t _Right = _Left + 1; _Right < _Planes.size(); ++_Right)
            {
                Direction3 _Direction;
                if (!TryNormalize(Cross(
                    ToVector(_Planes[_Left].Normal),
                    ToVector(_Planes[_Right].Normal)), _Direction))
                {
                    continue;
                }
                AddAxisEvidence(
                    _Evidence,
                    _Direction,
                    std::sqrt(_Planes[_Left].dArea * _Planes[_Right].dArea),
                    PrincipalAxis_);
            }
        }

        // 线边不依赖面被拆成多少块；同向线边的长度总和是另一组稳定证据。
        for (const auto& _Edge : Geometry_.Edges)
        {
            const auto _CurveIter = _Curves.find(_Edge.Curve3Id);
            if (_CurveIter == _Curves.end())
            {
                continue;
            }
            std::visit([&_Evidence, &_Edge, &PrincipalAxis_](IN const auto& Curve_)
            {
                using T = std::decay_t<decltype(Curve_)>;
                if constexpr (std::is_same_v<T, Line3>)
                {
                    AddAxisEvidence(
                        _Evidence,
                        Curve_.Axis.Direction,
                        std::abs(_Edge.Range.Last - _Edge.Range.First),
                        PrincipalAxis_);
                }
                else if constexpr (std::is_same_v<T, Segment3>)
                {
                    Direction3 _Direction;
                    const auto _Delta = Difference(Curve_.End, Curve_.Start);
                    if (TryNormalize(_Delta, _Direction))
                    {
                        AddAxisEvidence(
                            _Evidence,
                            _Direction,
                            Length(_Delta),
                            PrincipalAxis_);
                    }
                }
            }, *_CurveIter->second);
        }

        if (_Evidence.empty())
        {
            bRefined_ = false;
            return PrincipalAxis_;
        }
        const auto _Best = std::max_element(
            _Evidence.begin(),
            _Evidence.end(),
            [](IN const auto& Left_, IN const auto& Right_)
            {
                return Left_.dWeight < Right_.dWeight;
            });
        bRefined_ = std::abs(Dot(
            ToVector(_Best->Direction),
            ToVector(PrincipalAxis_))) < 1.0 - 1.0e-12;
        return _Best->Direction;
    }

    std::optional<Direction3> RecognizeAxis(
        IN const BRepModel& Geometry_,
        IN const std::vector<STriangle>& Triangles_,
        OUT bool& bRefined_)
    {
        if (Triangles_.empty())
        {
            return std::nullopt;
        }
        Point3 _Center;
        std::size_t _Count = 0;
        for (const auto& _Triangle : Triangles_)
        {
            for (const auto& _Point : { _Triangle.A, _Triangle.B, _Triangle.C })
            {
                _Center.X += _Point.X;
                _Center.Y += _Point.Y;
                _Center.Z += _Point.Z;
                ++_Count;
            }
        }
        _Center.X /= static_cast<double>(_Count);
        _Center.Y /= static_cast<double>(_Count);
        _Center.Z /= static_cast<double>(_Count);
        double _Cov[3][3]{};
        for (const auto& _Triangle : Triangles_)
        {
            for (const auto& _Point : { _Triangle.A, _Triangle.B, _Triangle.C })
            {
                const double _V[3]{
                    _Point.X - _Center.X,
                    _Point.Y - _Center.Y,
                    _Point.Z - _Center.Z
                };
                for (int _Row = 0; _Row < 3; ++_Row)
                {
                    for (int _Column = 0; _Column < 3; ++_Column)
                    {
                        _Cov[_Row][_Column] += _V[_Row] * _V[_Column];
                    }
                }
            }
        }
        int _LargestDiagonal = 0;
        if (_Cov[1][1] > _Cov[_LargestDiagonal][_LargestDiagonal])
        {
            _LargestDiagonal = 1;
        }
        if (_Cov[2][2] > _Cov[_LargestDiagonal][_LargestDiagonal])
        {
            _LargestDiagonal = 2;
        }
        Vector3 _Vector{};
        (&_Vector.X)[_LargestDiagonal] = 1.0;
        for (int _Iteration = 0; _Iteration < 64; ++_Iteration)
        {
            Vector3 _Next{
                _Cov[0][0] * _Vector.X + _Cov[0][1] * _Vector.Y + _Cov[0][2] * _Vector.Z,
                _Cov[1][0] * _Vector.X + _Cov[1][1] * _Vector.Y + _Cov[1][2] * _Vector.Z,
                _Cov[2][0] * _Vector.X + _Cov[2][1] * _Vector.Y + _Cov[2][2] * _Vector.Z
            };
            Direction3 _Direction;
            if (!TryNormalize(_Next, _Direction))
            {
                return std::nullopt;
            }
            _Vector = ToVector(_Direction);
        }
        Direction3 _Axis;
        if (!TryNormalize(_Vector, _Axis))
        {
            return std::nullopt;
        }
        const std::array<double, 3> _Components{
            std::abs(_Axis.X), std::abs(_Axis.Y), std::abs(_Axis.Z)
        };
        const auto _Largest = static_cast<std::size_t>(std::distance(
            _Components.begin(),
            std::max_element(_Components.begin(), _Components.end())));
        if ((&_Axis.X)[_Largest] < 0.0)
        {
            _Axis = { -_Axis.X, -_Axis.Y, -_Axis.Z };
        }
        return RefineAxisFromBRepEvidence(Geometry_, _Axis, bRefined_);
    }

    std::pair<Direction3, Direction3> MakeSectionBasis(IN const Direction3& Axis_)
    {
        Vector3 _Reference{ 1.0, 0.0, 0.0 };
        if (std::abs(Dot(_Reference, ToVector(Axis_))) > 0.9)
        {
            _Reference = { 0.0, 0.0, 1.0 };
        }
        const auto _Projection = Scale(
            ToVector(Axis_),
            Dot(_Reference, ToVector(Axis_)));
        Direction3 _X;
        TryNormalize(Add(_Reference, Scale(_Projection, -1.0)), _X);
        Direction3 _Z;
        TryNormalize(Cross(ToVector(_X), ToVector(Axis_)), _Z);
        return { _X, _Z };
    }

    Point2 ToSectionPoint(
        IN const Point3& Point_,
        IN const Direction3& X_,
        IN const Direction3& Z_)
    {
        const auto _Vector = ToVector(Point_);
        return { Dot(_Vector, ToVector(X_)), Dot(_Vector, ToVector(Z_)) };
    }

    void AddUniquePoint(
        IN OUT std::vector<Point3>& Points_,
        IN const Point3& Point_,
        IN double dTolerance_)
    {
        const auto _Exists = std::any_of(
            Points_.begin(),
            Points_.end(),
            [&Point_, dTolerance_](IN const Point3& Existing_) {
                return Length(Difference(Existing_, Point_)) <= dTolerance_;
            });
        if (!_Exists)
        {
            Points_.push_back(Point_);
        }
    }

    std::optional<Point3> IntersectEdge(
        IN const Point3& A_,
        IN const Point3& B_,
        IN double dA_,
        IN double dB_,
        IN double dTolerance_)
    {
        if (std::abs(dA_) <= dTolerance_ && std::abs(dB_) <= dTolerance_)
        {
            return std::nullopt;
        }
        if (std::abs(dA_) <= dTolerance_)
        {
            return A_;
        }
        if (std::abs(dB_) <= dTolerance_)
        {
            return B_;
        }
        if ((dA_ < 0.0) == (dB_ < 0.0))
        {
            return std::nullopt;
        }
        const auto _Factor = dA_ / (dA_ - dB_);
        return Point3{
            A_.X + (B_.X - A_.X) * _Factor,
            A_.Y + (B_.Y - A_.Y) * _Factor,
            A_.Z + (B_.Z - A_.Z) * _Factor
        };
    }

    std::vector<SSegment2> SliceTriangles(
        IN const std::vector<STriangle>& Triangles_,
        IN const Direction3& Axis_,
        IN const Direction3& X_,
        IN const Direction3& Z_,
        IN double dStation_,
        IN double dTolerance_)
    {
        std::vector<SSegment2> _Segments;
        for (const auto& _Triangle : Triangles_)
        {
            const auto _Distance = [&Axis_, dStation_](IN const Point3& Point_) {
                return Dot(ToVector(Point_), ToVector(Axis_)) - dStation_;
            };
            const auto _DA = _Distance(_Triangle.A);
            const auto _DB = _Distance(_Triangle.B);
            const auto _DC = _Distance(_Triangle.C);
            std::vector<Point3> _Points;
            for (const auto& _Edge : std::array{
                std::tuple{ _Triangle.A, _Triangle.B, _DA, _DB },
                std::tuple{ _Triangle.B, _Triangle.C, _DB, _DC },
                std::tuple{ _Triangle.C, _Triangle.A, _DC, _DA } })
            {
                if (const auto _Point = IntersectEdge(
                    std::get<0>(_Edge),
                    std::get<1>(_Edge),
                    std::get<2>(_Edge),
                    std::get<3>(_Edge),
                    dTolerance_))
                {
                    AddUniquePoint(_Points, *_Point, dTolerance_);
                }
            }
            if (_Points.size() == 2)
            {
                const auto _A = ToSectionPoint(_Points[0], X_, Z_);
                const auto _B = ToSectionPoint(_Points[1], X_, Z_);
                if (Distance2(_A, _B) > dTolerance_)
                {
                    _Segments.push_back({ _A, _B, false });
                }
            }
        }
        return _Segments;
    }

    std::vector<std::vector<Point2>> StitchLoops(
        IN OUT std::vector<SSegment2>& Segments_,
        IN double dTolerance_)
    {
        std::vector<std::vector<Point2>> _Loops;
        const auto _JoinTolerance = std::max(dTolerance_ * 5.0, 1.0e-7);
        for (auto& _Seed : Segments_)
        {
            if (_Seed.bUsed)
            {
                continue;
            }
            _Seed.bUsed = true;
            std::vector<Point2> _Chain{ _Seed.A, _Seed.B };
            for (std::size_t _Step = 0; _Step < Segments_.size(); ++_Step)
            {
                if (Distance2(_Chain.back(), _Chain.front()) <= _JoinTolerance)
                {
                    _Chain.pop_back();
                    break;
                }
                auto* _pBest = static_cast<SSegment2*>(nullptr);
                bool _bReverse = false;
                auto _BestDistance = (std::numeric_limits<double>::max)();
                for (auto& _Candidate : Segments_)
                {
                    if (_Candidate.bUsed)
                    {
                        continue;
                    }
                    const auto _ToA = Distance2(_Chain.back(), _Candidate.A);
                    const auto _ToB = Distance2(_Chain.back(), _Candidate.B);
                    if (_ToA < _BestDistance)
                    {
                        _BestDistance = _ToA;
                        _pBest = &_Candidate;
                        _bReverse = false;
                    }
                    if (_ToB < _BestDistance)
                    {
                        _BestDistance = _ToB;
                        _pBest = &_Candidate;
                        _bReverse = true;
                    }
                }
                if (!_pBest || _BestDistance > _JoinTolerance)
                {
                    break;
                }
                _pBest->bUsed = true;
                _Chain.push_back(_bReverse ? _pBest->A : _pBest->B);
            }
            if (_Chain.size() >= 3
                && Distance2(_Chain.front(), _Chain.back()) <= _JoinTolerance)
            {
                _Chain.pop_back();
            }
            if (_Chain.size() >= 3
                && std::abs(SignedArea(_Chain)) > dTolerance_ * dTolerance_)
            {
                _Loops.emplace_back(std::move(_Chain));
            }
        }
        return _Loops;
    }

    std::optional<SSlice> MakeSlice(
        IN const std::vector<STriangle>& Triangles_,
        IN const Direction3& Axis_,
        IN const Direction3& X_,
        IN const Direction3& Z_,
        IN double dStation_,
        IN double dTolerance_)
    {
        auto _Segments = SliceTriangles(
            Triangles_, Axis_, X_, Z_, dStation_, dTolerance_);
        auto _Loops = StitchLoops(_Segments, dTolerance_);
        if (_Loops.empty())
        {
            return std::nullopt;
        }
        std::sort(_Loops.begin(), _Loops.end(), [](
            IN const std::vector<Point2>& A_,
            IN const std::vector<Point2>& B_) {
            return std::abs(SignedArea(A_)) > std::abs(SignedArea(B_));
        });

        SSlice _Slice;
        _Slice.dStation = dStation_;
        _Slice.Section.Bounds.Empty = true;
        for (std::size_t _Index = 0; _Index < _Loops.size(); ++_Index)
        {
            SSectionLoop2 _Loop;
            _Loop.ID = _Index == 0
                ? std::string("outer")
                : "inner-" + std::to_string(_Index);
            _Loop.Points = std::move(_Loops[_Index]);
            _Loop.bInner = _Index != 0;
            for (const auto& _Point : _Loop.Points)
            {
                if (_Slice.Section.Bounds.Empty)
                {
                    _Slice.Section.Bounds.Min = _Point;
                    _Slice.Section.Bounds.Max = _Point;
                    _Slice.Section.Bounds.Empty = false;
                }
                else
                {
                    _Slice.Section.Bounds.Min.X = std::min(_Slice.Section.Bounds.Min.X, _Point.X);
                    _Slice.Section.Bounds.Min.Y = std::min(_Slice.Section.Bounds.Min.Y, _Point.Y);
                    _Slice.Section.Bounds.Max.X = std::max(_Slice.Section.Bounds.Max.X, _Point.X);
                    _Slice.Section.Bounds.Max.Y = std::max(_Slice.Section.Bounds.Max.Y, _Point.Y);
                }
            }
            const auto _Area = std::abs(SignedArea(_Loop.Points));
            _Slice.Section.dMaterialArea += _Loop.bInner ? -_Area : _Area;
            _Slice.Section.Loops.push_back(std::move(_Loop));
        }
        if (_Slice.Section.dMaterialArea <= dTolerance_ * dTolerance_)
        {
            return std::nullopt;
        }
        _Slice.Section.nCavityCount = _Slice.Section.Loops.size() - 1;
        _Slice.Section.Centroid = PolygonCentroid(_Slice.Section.Loops.front().Points);
        const auto [_Long, _Short] = PrincipalAxes(_Slice.Section.Loops.front().Points);
        _Slice.Section.PrincipalLongAxis = _Long;
        _Slice.Section.PrincipalShortAxis = _Short;
        return _Slice;
    }

    double SectionWidth(IN const SSectionSnapshot& Section_)
    {
        return Section_.Bounds.Empty
            ? 0.0
            : Section_.Bounds.Max.X - Section_.Bounds.Min.X;
    }

    double SectionHeight(IN const SSectionSnapshot& Section_)
    {
        return Section_.Bounds.Empty
            ? 0.0
            : Section_.Bounds.Max.Y - Section_.Bounds.Min.Y;
    }

    bool NearlyEqualSectionValue(
        IN double dLeft_,
        IN double dRight_,
        IN double dAbsoluteTolerance_,
        IN double dRelativeTolerance_)
    {
        return std::abs(dLeft_ - dRight_) <= std::max(
            dAbsoluteTolerance_,
            std::max(std::abs(dLeft_), std::abs(dRight_)) * dRelativeTolerance_);
    }

    std::vector<double> SortedLoopAreas(IN const SSectionSnapshot& Section_)
    {
        std::vector<double> _Areas;
        _Areas.reserve(Section_.Loops.size());
        for (const auto& _Loop : Section_.Loops)
        {
            _Areas.push_back(std::abs(SignedArea(_Loop.Points)));
        }
        std::sort(_Areas.begin(), _Areas.end(), std::greater<double>());
        return _Areas;
    }

    bool AreStableSections(
        IN const SSectionSnapshot& Left_,
        IN const SSectionSnapshot& Right_,
        IN const SRecognitionOptions& Options_)
    {
        if (Left_.Loops.size() != Right_.Loops.size()
            || Left_.nCavityCount != Right_.nCavityCount
            || Left_.Bounds.Empty != Right_.Bounds.Empty)
        {
            return false;
        }

        const auto _Scale = std::max({
            SectionWidth(Left_),
            SectionHeight(Left_),
            SectionWidth(Right_),
            SectionHeight(Right_),
            1.0
        });
        const auto _LinearTolerance = std::max(
            Options_.dLinearTolerance * 4.0,
            _Scale * Options_.dStableSectionRelativeTolerance);
        const auto _AreaTolerance = std::max(
            Options_.dLinearTolerance * _Scale * 8.0,
            _Scale * _Scale * Options_.dStableSectionRelativeTolerance);

        if (!NearlyEqualSectionValue(
                SectionWidth(Left_),
                SectionWidth(Right_),
                _LinearTolerance,
                Options_.dStableSectionRelativeTolerance)
            || !NearlyEqualSectionValue(
                SectionHeight(Left_),
                SectionHeight(Right_),
                _LinearTolerance,
                Options_.dStableSectionRelativeTolerance)
            || !NearlyEqualSectionValue(
                Left_.dMaterialArea,
                Right_.dMaterialArea,
                _AreaTolerance,
                Options_.dStableSectionRelativeTolerance))
        {
            return false;
        }

        const auto _LeftAreas = SortedLoopAreas(Left_);
        const auto _RightAreas = SortedLoopAreas(Right_);
        for (std::size_t _Index = 0; _Index < _LeftAreas.size(); ++_Index)
        {
            if (!NearlyEqualSectionValue(
                _LeftAreas[_Index],
                _RightAreas[_Index],
                _AreaTolerance,
                Options_.dStableSectionRelativeTolerance))
            {
                return false;
            }
        }
        return true;
    }

    class CCoordinateNormalizer final
    {
    public:
        CCoordinateNormalizer(
            IN const SExtrusionAnalysis& Analysis_,
            IN const SSectionPlacement& Placement_,
            IN const Direction3& TargetAxis_)
            : m_Analysis(Analysis_), m_Placement(Placement_)
        {
            if (!TryNormalize(ToVector(TargetAxis_), m_TargetY))
            {
                throw std::invalid_argument("Target extrusion axis is invalid");
            }
            const auto [_X, _Z] = MakeSectionBasis(m_TargetY);
            m_TargetX = _X;
            m_TargetZ = _Z;
        }

        Point3 Point(IN const Point3& Point_) const
        {
            const auto _Vector = ToVector(Point_);
            const auto _SectionX = Dot(_Vector, ToVector(m_Analysis.SectionX))
                - m_Placement.Origin.X;
            const auto _SectionZ = Dot(_Vector, ToVector(m_Analysis.SectionZ))
                - m_Placement.Origin.Y;
            const auto _X = _SectionX * m_Placement.XDirection.X
                + _SectionZ * m_Placement.XDirection.Y;
            const auto _Z = _SectionX * m_Placement.ZDirection.X
                + _SectionZ * m_Placement.ZDirection.Y;
            const auto _Y = Dot(_Vector, ToVector(m_Analysis.Axis))
                - m_Analysis.dFirst;
            const auto _World = Add(
                Add(Scale(ToVector(m_TargetX), _X), Scale(ToVector(m_TargetY), _Y)),
                Scale(ToVector(m_TargetZ), _Z));
            return { _World.X, _World.Y, _World.Z };
        }

        Direction3 Direction(IN const Direction3& Direction_) const
        {
            const auto _Vector = ToVector(Direction_);
            const auto _SectionX = Dot(_Vector, ToVector(m_Analysis.SectionX));
            const auto _SectionZ = Dot(_Vector, ToVector(m_Analysis.SectionZ));
            const auto _X = _SectionX * m_Placement.XDirection.X
                + _SectionZ * m_Placement.XDirection.Y;
            const auto _Z = _SectionX * m_Placement.ZDirection.X
                + _SectionZ * m_Placement.ZDirection.Y;
            const auto _Y = Dot(_Vector, ToVector(m_Analysis.Axis));
            const auto _World = Add(
                Add(Scale(ToVector(m_TargetX), _X), Scale(ToVector(m_TargetY), _Y)),
                Scale(ToVector(m_TargetZ), _Z));
            Direction3 _Result;
            if (!TryNormalize(_World, _Result))
            {
                return Direction_;
            }
            return _Result;
        }

        Vector3 Vector(IN const Vector3& Vector_) const
        {
            const auto _Length = Length(Vector_);
            Direction3 _Direction;
            if (_Length <= 1.0e-12 || !TryNormalize(Vector_, _Direction))
            {
                return {};
            }
            const auto _Result = Direction(_Direction);
            return Scale(ToVector(_Result), _Length);
        }

        Placement3 Placement(IN const Placement3& Placement_) const
        {
            return {
                Point(Placement_.Location),
                Direction(Placement_.XDirection),
                Direction(Placement_.YDirection),
                Direction(Placement_.ZDirection)
            };
        }

    private:
        const SExtrusionAnalysis& m_Analysis;
        const SSectionPlacement& m_Placement;
        Direction3 m_TargetX;
        Direction3 m_TargetY;
        Direction3 m_TargetZ;
    };

    void TransformCurve3(IN OUT Curve3& Curve_, IN const CCoordinateNormalizer& Transform_)
    {
        std::visit([&Transform_](IN OUT auto& CurveValue_) {
            using T = std::decay_t<decltype(CurveValue_)>;
            if constexpr (std::is_same_v<T, Line3> || std::is_same_v<T, Ray3>)
            {
                CurveValue_.Axis.Location = Transform_.Point(CurveValue_.Axis.Location);
                CurveValue_.Axis.Direction = Transform_.Direction(CurveValue_.Axis.Direction);
            }
            else if constexpr (std::is_same_v<T, Segment3>)
            {
                CurveValue_.Start = Transform_.Point(CurveValue_.Start);
                CurveValue_.End = Transform_.Point(CurveValue_.End);
            }
            else if constexpr (std::is_same_v<T, Circle3> || std::is_same_v<T, Ellipse3>)
            {
                CurveValue_.Placement = Transform_.Placement(CurveValue_.Placement);
            }
            else if constexpr (std::is_same_v<T, Arc3>)
            {
                CurveValue_.Basis.Placement = Transform_.Placement(CurveValue_.Basis.Placement);
            }
            else if constexpr (std::is_same_v<T, EllipseArc3>)
            {
                CurveValue_.Basis.Placement = Transform_.Placement(CurveValue_.Basis.Placement);
            }
            else if constexpr (std::is_same_v<T, Polyline3>)
            {
                for (auto& _Point : CurveValue_.Points) _Point = Transform_.Point(_Point);
            }
            else if constexpr (std::is_same_v<T, Bezier3>
                || std::is_same_v<T, BSpline3>
                || std::is_same_v<T, NURBS3>)
            {
                for (auto& _Point : CurveValue_.Poles) _Point = Transform_.Point(_Point);
            }
            else if constexpr (std::is_same_v<T, Clothoid3>)
            {
                CurveValue_.Placement = Transform_.Placement(CurveValue_.Placement);
            }
        }, Curve_);
    }

    void TransformSurface3(IN OUT Surface3& Surface_, IN const CCoordinateNormalizer& Transform_)
    {
        std::visit([&Transform_](IN OUT auto& SurfaceValue_) {
            using T = std::decay_t<decltype(SurfaceValue_)>;
            if constexpr (std::is_same_v<T, BSplineSurface3>)
            {
                for (auto& _Point : SurfaceValue_.Poles) _Point = Transform_.Point(_Point);
            }
            else
            {
                SurfaceValue_.Placement = Transform_.Placement(SurfaceValue_.Placement);
            }
        }, Surface_);
    }
}

bool TryAnalyzeExtrusion(
    IN const BRepModel& Geometry_,
    IN const SRecognitionOptions& Options_,
    OUT SExtrusionAnalysis& Analysis_,
    OUT ERecognitionStatus& Status_,
    OUT std::vector<std::string>& Diagnostics_)
{
    if (Options_.dLinearTolerance <= 0.0
        || Options_.dStableSectionRelativeTolerance <= 0.0
        || Options_.nSliceSampleCount < 3
        || Options_.nMinimumStableSliceCount < 2
        || Options_.nMinimumStableSliceCount > Options_.nSliceSampleCount)
    {
        Status_ = ERecognitionStatus::InvalidRequest;
        Diagnostics_.push_back("Recognition tolerance and slice sample count are invalid");
        return false;
    }
    if (Geometry_.RootSolidIds.size() > 1 || Geometry_.Solids.size() > 1)
    {
        Status_ = ERecognitionStatus::MultipleSolidCandidates;
        Diagnostics_.push_back("Extrusion recognition requires one solid candidate");
        return false;
    }
    const auto _Triangles = CollectTriangles(Geometry_);
    if (_Triangles.empty())
    {
        Status_ = ERecognitionStatus::InvalidGeometry;
        Diagnostics_.push_back("BRep has no triangulation for section reconstruction");
        return false;
    }
    bool _AxisRefined = false;
    const auto _Axis = RecognizeAxis(Geometry_, _Triangles, _AxisRefined);
    if (!_Axis)
    {
        Status_ = ERecognitionStatus::AxisNotFound;
        Diagnostics_.push_back("No stable principal extrusion axis was found");
        return false;
    }
    Analysis_.Axis = *_Axis;
    if (_AxisRefined)
    {
        Diagnostics_.push_back(
            "Principal axis snapped to exact BRep side-wall/edge consensus");
    }
    const auto [_SectionX, _SectionZ] = MakeSectionBasis(*_Axis);
    Analysis_.SectionX = _SectionX;
    Analysis_.SectionZ = _SectionZ;
    Analysis_.dFirst = (std::numeric_limits<double>::max)();
    Analysis_.dLast = (std::numeric_limits<double>::lowest)();
    for (const auto& _Triangle : _Triangles)
    {
        for (const auto& _Point : { _Triangle.A, _Triangle.B, _Triangle.C })
        {
            const auto _Station = Dot(ToVector(_Point), ToVector(*_Axis));
            Analysis_.dFirst = std::min(Analysis_.dFirst, _Station);
            Analysis_.dLast = std::max(Analysis_.dLast, _Station);
        }
    }
    if (Analysis_.dLast - Analysis_.dFirst <= Options_.dLinearTolerance)
    {
        Status_ = ERecognitionStatus::NotExtrusionBased;
        Diagnostics_.push_back("Geometry has no usable extent along the detected axis");
        return false;
    }

    std::vector<SSlice> _Slices;
    _Slices.reserve(Options_.nSliceSampleCount);
    for (std::size_t _Index = 0; _Index < Options_.nSliceSampleCount; ++_Index)
    {
        const auto _Fraction = (static_cast<double>(_Index) + 0.5)
            / static_cast<double>(Options_.nSliceSampleCount);
        const auto _Station = Analysis_.dFirst
            + (Analysis_.dLast - Analysis_.dFirst) * _Fraction;
        auto _Slice = MakeSlice(
            _Triangles,
            *_Axis,
            _SectionX,
            _SectionZ,
            _Station,
            Options_.dLinearTolerance);
        if (!_Slice)
        {
            continue;
        }
        _Slices.push_back(std::move(*_Slice));
    }
    if (_Slices.empty())
    {
        Status_ = ERecognitionStatus::SectionExtractionFailed;
        Diagnostics_.push_back("No closed material section was reconstructed");
        return false;
    }

    // 拉伸体的决定性事实不是“某处能切出闭环”，而是沿轴前进时存在稳定不变的
    // 材料截面。局部孔、盲孔、截断和坡口允许破坏部分采样站，但至少应保留一段
    // 连续稳定区间。球体、锥体等只会得到连续变化的截面，因此在这里被拒绝。
    std::optional<std::pair<std::size_t, std::size_t>> _BestRun;
    std::size_t _RunFirst = 0;
    for (std::size_t _Index = 1; _Index <= _Slices.size(); ++_Index)
    {
        const auto _Continues = _Index < _Slices.size()
            && AreStableSections(
                _Slices[_Index - 1].Section,
                _Slices[_Index].Section,
                Options_);
        if (_Continues)
        {
            continue;
        }

        const auto _RunCount = _Index - _RunFirst;
        if (_RunCount >= Options_.nMinimumStableSliceCount)
        {
            if (!_BestRun)
            {
                _BestRun = std::pair{ _RunFirst, _Index };
            }
            else
            {
                const auto& _Current = _Slices[_RunFirst].Section;
                const auto& _Best = _Slices[_BestRun->first].Section;
                const auto _BestCount = _BestRun->second - _BestRun->first;
                // 毛坯截面优先：先取材料面积大的稳定组，面积相同时再取更长的组。
                if (_Current.dMaterialArea > _Best.dMaterialArea
                    || (NearlyEqualSectionValue(
                            _Current.dMaterialArea,
                            _Best.dMaterialArea,
                            Options_.dLinearTolerance,
                            Options_.dStableSectionRelativeTolerance)
                        && _RunCount > _BestCount))
                {
                    _BestRun = std::pair{ _RunFirst, _Index };
                }
            }
        }
        _RunFirst = _Index;
    }

    if (!_BestRun)
    {
        Status_ = ERecognitionStatus::NotExtrusionBased;
        Diagnostics_.push_back(
            "Axial slices do not contain a stable repeated material section");
        return false;
    }

    auto _BestSlice = _BestRun->first;
    for (auto _Index = _BestRun->first + 1; _Index < _BestRun->second; ++_Index)
    {
        if (_Slices[_Index].Section.dMaterialArea
            > _Slices[_BestSlice].Section.dMaterialArea)
        {
            _BestSlice = _Index;
        }
    }
    Analysis_.Section = std::move(_Slices[_BestSlice].Section);
    Diagnostics_.push_back(
        "Reconstructed section from " + std::to_string(_Slices.size())
        + " valid axial slices; stable run="
        + std::to_string(_BestRun->second - _BestRun->first));
    Status_ = ERecognitionStatus::Success;
    return true;
}

bool TryNormalizeExtrusion(
    IN const BRepModel& Geometry_,
    IN const SExtrusionAnalysis& Analysis_,
    IN const SSectionPlacement& Placement_,
    IN const Direction3& TargetAxis_,
    OUT BRepModel& Normalized_,
    OUT std::string& strError_)
{
    try
    {
        const CCoordinateNormalizer _Transform(
            Analysis_, Placement_, TargetAxis_);
        Normalized_ = Geometry_;
        for (auto& _Vertex : Normalized_.Vertices)
        {
            _Vertex.Position = _Transform.Point(_Vertex.Position);
        }
        for (auto& _Curve : Normalized_.Curves3)
        {
            TransformCurve3(_Curve.Geometry, _Transform);
        }
        for (auto& _Surface : Normalized_.Surfaces3)
        {
            TransformSurface3(_Surface.Geometry, _Transform);
        }
        for (auto& _Triangulation : Normalized_.Triangulations3)
        {
            for (auto& _Point : _Triangulation.Geometry.Vertices)
            {
                _Point = _Transform.Point(_Point);
            }
            for (auto& _Normal : _Triangulation.Geometry.Normals)
            {
                _Normal = _Transform.Vector(_Normal);
            }
        }
        const Transform3 _Identity;
        for (auto& _Root : Normalized_.RootShapes)
        {
            _Root.Location = _Identity;
        }
        for (auto& _Compound : Normalized_.Compounds)
        {
            for (auto& _Child : _Compound.Children)
            {
                _Child.Location = _Identity;
            }
        }
        Normalized_.Metadata.Tags.push_back("extrusion.normalized");
        return true;
    }
    catch (const std::exception& Error_)
    {
        strError_ = Error_.what();
        return false;
    }
}
}
