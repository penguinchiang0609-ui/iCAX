#include "pch.h"
#include "RecoveredPartDrawingFeatures.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <variant>

namespace iCAX::TubeDesigner
{
namespace
{
    namespace Tube = iCAX::GeometryData::Tube;
    using iCAX::Data::ObjectMap;

    constexpr double kPi = 3.14159265358979323846;

    struct SPoint final
    {
        double X, Y, Z;
    };

    SPoint WorldPoint(const iCAX::GeometryData::Placement3& Frame_,
        const iCAX::GeometryData::Point2& SectionPoint_, double Station_)
    {
        return {
            Frame_.Location.X + Frame_.XDirection.X * SectionPoint_.X
                + Frame_.YDirection.X * SectionPoint_.Y + Frame_.ZDirection.X * Station_,
            Frame_.Location.Y + Frame_.XDirection.Y * SectionPoint_.X
                + Frame_.YDirection.Y * SectionPoint_.Y + Frame_.ZDirection.Y * Station_,
            Frame_.Location.Z + Frame_.XDirection.Z * SectionPoint_.X
                + Frame_.YDirection.Z * SectionPoint_.Y + Frame_.ZDirection.Z * Station_
        };
    }

    bool ValidBase(const Tube::SExtrudedRegionNode& Base_, double Length_, double Tolerance_)
    {
        const auto _Axis = Base_.Frame.ZDirection;
        if (std::abs(std::abs(_Axis.X) - 1.0) > 1.0e-6
            || std::abs(_Axis.Y) > 1.0e-6 || std::abs(_Axis.Z) > 1.0e-6)
            return false;
        const double _First = Base_.Frame.Location.X + _Axis.X * Base_.First;
        const double _Last = Base_.Frame.Location.X + _Axis.X * Base_.Last;
        return std::abs(std::min(_First, _Last)) <= Tolerance_
            && std::abs(std::max(_First, _Last) - Length_) <= Tolerance_;
    }

    bool CollectRemovals(const std::map<std::string, const Tube::SSolidNode*>& Nodes_,
        const std::string& ID_, std::vector<const Tube::SSolidNode*>& Leaves_,
        std::set<std::string>& Visited_)
    {
        const auto _Found = Nodes_.find(ID_);
        if (_Found == Nodes_.end() || !Visited_.insert(ID_).second)
            return false;
        const auto* _Node = _Found->second;
        if (const auto* _Boolean = std::get_if<Tube::SBooleanNode>(&_Node->Data))
        {
            if (_Boolean->Operation != Tube::EBooleanOperation::Union
                || _Boolean->Children.empty()) return false;
            for (const auto& _Child : _Boolean->Children)
                if (!CollectRemovals(Nodes_, _Child, Leaves_, Visited_)) return false;
        }
        else Leaves_.push_back(_Node);
        return true;
    }

    bool IsRemoved(const Tube::SHalfSpaceNode& Cut_, double X_)
    {
        const auto& _Plane = Cut_.Boundary;
        const double _Signed = _Plane.Normal.X * (X_ - _Plane.Location.X)
            - _Plane.Normal.Y * _Plane.Location.Y
            - _Plane.Normal.Z * _Plane.Location.Z;
        return Cut_.KeepNegativeSide ? _Signed < 0 : _Signed > 0;
    }

    bool MapEndTrim(const Tube::SSolidNode& Node_, const Tube::SHalfSpaceNode& Cut_,
        double Length_, double Tolerance_, ObjectMap& Ends_)
    {
        if (!Node_.Relations || !Node_.Relations->MaterialEffect
            || Node_.Relations->MaterialEffect->Role != Tube::EFeatureMaterialRole::Truncation)
            return false;
        const auto& _Plane = Cut_.Boundary;
        const double _NX = _Plane.Normal.X;
        if (!std::isfinite(_NX) || std::abs(_NX) < std::cos(80.0 * kPi / 180.0))
            return false;
        const double _SlopeY = -_Plane.Normal.Y / _NX;
        const double _SlopeZ = -_Plane.Normal.Z / _NX;
        const double _CenterX = _Plane.Location.X
            + (_Plane.Normal.Y * _Plane.Location.Y + _Plane.Normal.Z * _Plane.Location.Z) / _NX;
        const double _Angle = std::atan(std::hypot(_SlopeY, _SlopeZ)) * 180.0 / kPi;
        const double _Rotation = _Angle <= 1.0e-9 ? 0.0 : std::atan2(_SlopeZ, _SlopeY) * 180.0 / kPi;
        if (!std::isfinite(_CenterX) || !std::isfinite(_Angle)
            || !std::isfinite(_Rotation) || _CenterX < -Tolerance_
            || _CenterX > Length_ + Tolerance_)
            return false;
        const double _Probe = std::max(1.0, 10.0 * Tolerance_);
        const bool _StartRemoved = IsRemoved(Cut_, -_Probe);
        const bool _EndRemoved = IsRemoved(Cut_, Length_ + _Probe);
        if (_StartRemoved == _EndRemoved) return false;
        const char* _End = _StartRemoved ? "start" : "end";
        if (Ends_.contains(_End)) return false;
        const double _Trim = _StartRemoved ? _CenterX : Length_ - _CenterX;
        if (_Trim < -Tolerance_ || _Trim >= Length_) return false;
        Ends_[_End] = ObjectMap{
            {"type", std::string("template")},
            {"toolRef", ObjectMap{{"id", std::string("end-miter")}, {"scope", std::string("system")}}},
            {"toolParameters", ObjectMap{{"angle", _Angle}}},
            {"trim", std::max(0.0, _Trim)},
            {"rotation", _Rotation},
            {"datum", std::string("center")}
        };
        return true;
    }

    struct SEndStepEdge final
    {
        using P = iCAX::GeometryData::Point2;
        P Start, End, Center;
        double Radius = 0, First = 0, Last = 0;
        bool IsCircle = false;
    };

    double EndStepDistance(const SEndStepEdge::P& A_, const SEndStepEdge::P& B_)
    {
        return std::hypot(A_.X-B_.X, A_.Y-B_.Y);
    }

    bool EndStepAngleContains(const SEndStepEdge& Edge_, double Angle_, double Tolerance_)
    {
        Angle_ += std::ceil((Edge_.First-Angle_-Tolerance_)/(2*kPi))*2*kPi;
        return Angle_ <= Edge_.Last+Tolerance_;
    }

    std::optional<std::vector<SEndStepEdge>> EndStepSection(
        const Tube::SExtrudedRegionNode& Extrusion_, double Tolerance_)
    {
        const auto& f=Extrusion_.Frame;
        const auto finite=[](const auto& p) { return std::isfinite(p.X)&&std::isfinite(p.Y)&&std::isfinite(p.Z); };
        const auto dot=[](const auto& a,const auto& b) { return a.X*b.X+a.Y*b.Y+a.Z*b.Z; };
        if (!finite(f.Location)||!finite(f.XDirection)||!finite(f.YDirection)||!finite(f.ZDirection)
            || std::abs(std::abs(f.ZDirection.X)-1)>1e-6
            || std::abs(f.ZDirection.Y)>1e-6||std::abs(f.ZDirection.Z)>1e-6
            || std::abs(dot(f.XDirection,f.XDirection)-1)>1e-6
            || std::abs(dot(f.YDirection,f.YDirection)-1)>1e-6
            || std::abs(dot(f.XDirection,f.YDirection))>1e-6
            || std::abs(f.XDirection.X)>1e-6||std::abs(f.YDirection.X)>1e-6
            || Extrusion_.Section.Boundaries.size()!=1) return std::nullopt;
        const auto project=[&](const SEndStepEdge::P& p) {
            const auto w=WorldPoint(f,p,0); return SEndStepEdge::P{w.Y,w.Z};
        };
        std::vector<SEndStepEdge> edges;
        for (const auto& curve:Extrusion_.Section.Boundaries.front().Curves) {
            const auto& range=curve.Segment.Range;
            if (!std::isfinite(range.First)||!std::isfinite(range.Last)||range.Last<=range.First)
                return std::nullopt;
            SEndStepEdge edge;
            if (const auto* line=std::get_if<iCAX::GeometryData::Segment2>(&curve.Segment.Curve)) {
                edge.Start=project(line->Start); edge.End=project(line->End);
            } else if (const auto* circle=std::get_if<iCAX::GeometryData::Circle2>(&curve.Segment.Curve)) {
                const auto& p=circle->Placement;
                if (!std::isfinite(circle->Radius)||circle->Radius<=Tolerance_
                    || range.Last-range.First>2*kPi+1e-6) return std::nullopt;
                edge.Center=project(p.Location); edge.Radius=circle->Radius; edge.IsCircle=true;
                const auto u=project({p.Location.X+p.XDirection.X,p.Location.Y+p.XDirection.Y});
                const auto v=project({p.Location.X+p.YDirection.X,p.Location.Y+p.YDirection.Y});
                const double ux=u.X-edge.Center.X,uy=u.Y-edge.Center.Y,vx=v.X-edge.Center.X,vy=v.Y-edge.Center.Y;
                if (std::abs(ux*ux+uy*uy-1)>1e-6||std::abs(vx*vx+vy*vy-1)>1e-6
                    ||std::abs(ux*vx+uy*vy)>1e-6) return std::nullopt;
                const double angle=std::atan2(uy,ux),sign=ux*vy-uy*vx>0?1.:-1.;
                edge.First=std::min(angle+sign*range.First,angle+sign*range.Last);
                edge.Last=std::max(angle+sign*range.First,angle+sign*range.Last);
                edge.Start={edge.Center.X+edge.Radius*std::cos(edge.First),edge.Center.Y+edge.Radius*std::sin(edge.First)};
                edge.End={edge.Center.X+edge.Radius*std::cos(edge.Last),edge.Center.Y+edge.Radius*std::sin(edge.Last)};
            } else return std::nullopt; // No sampled or unsupported inherited boundary.
            if (!std::isfinite(edge.Start.X)||!std::isfinite(edge.Start.Y)
                ||!std::isfinite(edge.End.X)||!std::isfinite(edge.End.Y)
                ||(!edge.IsCircle&&EndStepDistance(edge.Start,edge.End)<=Tolerance_)) return std::nullopt;
            edges.push_back(edge);
        }
        if (edges.size()<3) return std::nullopt;
        return edges;
    }

    bool EndStepInherited(const SEndStepEdge& Edge_, const SEndStepEdge& Base_, double Tolerance_)
    {
        if (Edge_.IsCircle!=Base_.IsCircle) return false;
        if (Edge_.IsCircle) {
            if (EndStepDistance(Edge_.Center,Base_.Center)>Tolerance_
                ||std::abs(Edge_.Radius-Base_.Radius)>Tolerance_) return false;
            const double angleTolerance=Tolerance_/Base_.Radius;
            if (Base_.Last-Base_.First>=2*kPi-angleTolerance) return true;
            const double shift=std::round((Base_.First-Edge_.First)/(2*kPi))*2*kPi;
            for (double turn:{-2*kPi,0.,2*kPi})
                if (Edge_.First+shift+turn>=Base_.First-angleTolerance
                    &&Edge_.Last+shift+turn<=Base_.Last+angleTolerance) return true;
            return false;
        }
        const double dx=Base_.End.X-Base_.Start.X,dy=Base_.End.Y-Base_.Start.Y,length=std::hypot(dx,dy);
        for (const auto& p:{Edge_.Start,Edge_.End}) {
            const double x=p.X-Base_.Start.X,y=p.Y-Base_.Start.Y,along=(x*dx+y*dy)/length;
            if (std::abs(x*dy-y*dx)>Tolerance_*length||along<-Tolerance_||along>length+Tolerance_) return false;
        }
        return true;
    }

    bool MapEndStep(const Tube::SSolidNode& Node_, const Tube::SExtrudedRegionNode& Cut_,
        const Tube::SExtrudedRegionNode& Base_, double Length_, double Tolerance_, ObjectMap& Ends_)
    {
        const auto source=Node_.Metadata.find("source"),proof=Node_.Metadata.find("independentReplay");
        const auto mode=Node_.Metadata.find("recognitionMode");
        const bool completeProof=proof!=Node_.Metadata.end()
            && (proof->second=="complete-removal"
                || (proof->second=="complete-subset" && mode!=Node_.Metadata.end()
                    && mode->second=="global-subset"));
        if (source==Node_.Metadata.end()||source->second!="occ.removal-extrusion"
            ||!completeProof
            ||!std::isfinite(Cut_.First)||!std::isfinite(Cut_.Last)||Cut_.Last<=Cut_.First) return false;
        const auto base=EndStepSection(Base_,Tolerance_),cut=EndStepSection(Cut_,Tolerance_);
        if (!base||!cut) return false;
        const double a=Cut_.Frame.Location.X+Cut_.Frame.ZDirection.X*Cut_.First;
        const double b=Cut_.Frame.Location.X+Cut_.Frame.ZDirection.X*Cut_.Last;
        const double first=std::min(a,b),last=std::max(a,b);
        const bool start=std::abs(first)<=Tolerance_,end=std::abs(last-Length_)<=Tolerance_;
        if (start==end||first<-Tolerance_||last>Length_+Tolerance_) return false;
        const double depth=start?last:Length_-first;
        const char* key=start?"start":"end";
        if (Ends_.contains(key)||depth<0.01||depth>=Length_-Tolerance_) return false;

        // All new boundaries must lie on one cutting plane. An open channel
        // can have a separate seam on each flange; every other edge must be
        // inherited from the verified blank.
        std::vector<const SEndStepEdge*> seams;
        for (const auto& edge:*cut) {
            if (std::any_of(base->begin(),base->end(),[&](const auto& inherited) {
                return EndStepInherited(edge,inherited,Tolerance_); })) continue;
            if (edge.IsCircle) return false;
            seams.push_back(&edge);
        }
        if (seams.empty()) return false;
        const auto* seam=seams.front();
        const double length=EndStepDistance(seam->Start,seam->End);
        double ny=(seam->End.Y-seam->Start.Y)/length,nz=(seam->Start.X-seam->End.X)/length;
        if ((std::abs(ny)>=std::abs(nz)?ny:nz)<0) { ny=-ny; nz=-nz; }
        const double split=ny*seam->Start.X+nz*seam->Start.Y;
        const auto support=[&](const SEndStepEdge::P& p) { return ny*p.X+nz*p.Y; };
        for (const auto* boundary:seams)
            if (std::abs(support(boundary->Start)-split)>Tolerance_
                || std::abs(support(boundary->End)-split)>Tolerance_) return false;
        double low=split,high=split;
        for (const auto& edge:*cut) {
            low=std::min({low,support(edge.Start),support(edge.End)});
            high=std::max({high,support(edge.Start),support(edge.End)});
            if (edge.IsCircle) for (double angle:{std::atan2(nz,ny),std::atan2(nz,ny)+kPi}) {
                if (!EndStepAngleContains(edge,angle,Tolerance_/edge.Radius)) continue;
                const double value=support(edge.Center)+edge.Radius*(ny*std::cos(angle)+nz*std::sin(angle));
                low=std::min(low,value); high=std::max(high,value);
            }
        }
        const bool negative=low<split-Tolerance_,positive=high>split+Tolerance_;
        if (negative==positive) return false;

        // Sorted transverse crossings alternate outside/inside the one closed
        // outer loop. Every resulting material interval must have exactly one
        // matching seam: cutting only one flange cannot become a full step.
        // Multiple-seam vertex/tangent cases remain unsupported rather than
        // guessing the parity of a touching boundary.
        std::vector<SEndStepEdge::P> crossings;
        const auto add=[&](const SEndStepEdge::P& p) {
            if (std::none_of(crossings.begin(),crossings.end(),[&](const auto& q) {
                return EndStepDistance(p,q)<=Tolerance_; })) crossings.push_back(p);
        };
        for (const auto& edge:*base) {
            if (edge.IsCircle) {
                const double distance=split-support(edge.Center);
                if (std::abs(distance)>edge.Radius+Tolerance_) continue;
                if (seams.size()>1 && std::abs(std::abs(distance)-edge.Radius)<=Tolerance_)
                    return false;
                const double alpha=std::acos(std::clamp(distance/edge.Radius,-1.,1.));
                for (double angle:{std::atan2(nz,ny)-alpha,std::atan2(nz,ny)+alpha})
                    if (EndStepAngleContains(edge,angle,Tolerance_/edge.Radius)) {
                        const SEndStepEdge::P point{edge.Center.X+edge.Radius*std::cos(angle),edge.Center.Y+edge.Radius*std::sin(angle)};
                        if (seams.size()>1 && (EndStepDistance(point,edge.Start)<=Tolerance_
                            || EndStepDistance(point,edge.End)<=Tolerance_)) return false;
                        add(point);
                    }
            } else {
                const double d0=support(edge.Start)-split,d1=support(edge.End)-split;
                if (std::abs(d0)<=Tolerance_&&std::abs(d1)<=Tolerance_) return false;
                if (seams.size()>1 && (std::abs(d0)<=Tolerance_||std::abs(d1)<=Tolerance_)) return false;
                if (std::abs(d0)<=Tolerance_) add(edge.Start);
                if (std::abs(d1)<=Tolerance_) add(edge.End);
                if (d0*d1<0) {
                    const double t=d0/(d0-d1);
                    add({edge.Start.X+t*(edge.End.X-edge.Start.X),edge.Start.Y+t*(edge.End.Y-edge.Start.Y)});
                }
            }
        }
        if (crossings.size()!=2*seams.size()) return false;
        std::sort(crossings.begin(),crossings.end(),[&](const auto& p,const auto& q) {
            return -nz*p.X+ny*p.Y < -nz*q.X+ny*q.Y;
        });
        std::set<const SEndStepEdge*> matched;
        for (size_t i=0;i<crossings.size();i+=2) {
            const auto found=std::find_if(seams.begin(),seams.end(),[&](const auto* boundary) {
                return (EndStepDistance(crossings[i],boundary->Start)<=Tolerance_
                        &&EndStepDistance(crossings[i+1],boundary->End)<=Tolerance_)
                    || (EndStepDistance(crossings[i],boundary->End)<=Tolerance_
                        &&EndStepDistance(crossings[i+1],boundary->Start)<=Tolerance_);
            });
            if (found==seams.end()||!matched.insert(*found).second) return false;
        }
        Ends_[key]=ObjectMap{{"type",std::string("template")},
            {"toolRef",ObjectMap{{"id",std::string("end-step-z")},{"scope",std::string("system")}}},
            {"toolParameters",ObjectMap{{"depth",depth},{"splitOffset",split},{"hand",std::string(negative?"negative":"positive")}}},
            {"trim",0.},{"rotation",std::atan2(nz,ny)*180/kPi},{"datum",std::string("long")}};
        return true;
    }

    bool MapCircularSideHole(const Tube::SSolidNode& Node_,
        const Tube::SExtrudedRegionNode& Cut_, double Length_, double Tolerance_,
        iCAX::Data::VariantArray& Features_, bool Opposite_ = false)
    {
        if (!Node_.Relations || !Node_.Relations->MaterialEffect
            || Node_.Relations->MaterialEffect->Role != Tube::EFeatureMaterialRole::Penetration
            || !Node_.RemovalSemantics
            || Cut_.Section.Boundaries.size() != 1
            || Cut_.Section.Boundaries.front().Curves.size() != 1)
            return false;
        const auto _Extent = Node_.RemovalSemantics->Extent;
        if (_Extent != Tube::ERemovalExtentKind::Through
            && _Extent != Tube::ERemovalExtentKind::Blind) return false;
        // Multiple disjoint spans need the narrowly checked opposite-wall
        // interpretation below; no other cross-material grouping is editable.
        if (Opposite_ ? (_Extent != Tube::ERemovalExtentKind::Through
                || Node_.Relations->MaterialSpans.size() != 2)
            : Node_.Relations->MaterialSpans.size() > 1) return false;
        double _Depth = 0.0;
        if (_Extent == Tube::ERemovalExtentKind::Blind)
        {
            if (Node_.Relations->MaterialSpans.size() != 1) return false;
            const auto& _Span = Node_.Relations->MaterialSpans.front();
            const bool _StartAtHost = _Span.Start.Kind == Tube::EMaterialSpanEndpointKind::HostBoundary
                && _Span.End.Kind == Tube::EMaterialSpanEndpointKind::FeatureTermination;
            const bool _EndAtHost = _Span.End.Kind == Tube::EMaterialSpanEndpointKind::HostBoundary
                && _Span.Start.Kind == Tube::EMaterialSpanEndpointKind::FeatureTermination;
            if (!_StartAtHost && !_EndAtHost) return false;
            _Depth = std::abs(_Span.End.Station.Value - _Span.Start.Station.Value);
            if (!std::isfinite(_Depth) || _Depth <= Tolerance_) return false;
        }
        const auto* _Circle = std::get_if<iCAX::GeometryData::Circle2>(
            &Cut_.Section.Boundaries.front().Curves.front().Segment.Curve);
        const auto& _Range = Cut_.Section.Boundaries.front().Curves.front().Segment.Range;
        if (!_Circle || !std::isfinite(_Circle->Radius)
            || _Circle->Radius <= Tolerance_
            || !std::isfinite(_Range.First) || !std::isfinite(_Range.Last)
            || std::abs(std::abs(_Range.Last - _Range.First) - 2.0 * kPi) > 1.0e-5)
            return false;

        const auto& _Axis = Cut_.Frame.ZDirection;
        const auto _First = WorldPoint(Cut_.Frame, _Circle->Placement.Location, Cut_.First);
        const auto _Last = WorldPoint(Cut_.Frame, _Circle->Placement.Location, Cut_.Last);
        if (std::abs(_First.X - _Last.X) > Tolerance_
            || !std::isfinite(_First.X) || _First.X <= Tolerance_
            || _First.X >= Length_ - Tolerance_) return false;

        // A cylindrical generator is padded beyond the material, so its end
        // points cannot identify the entry side of an open profile or a web
        // crossing the section centre. Observed material stations are world
        // projections on the cut axis. Only one proven base-material interval
        // may supply this alternative entry-side interpretation.
        const auto _ObservedEntrySide = [&](double AxisComponent_) {
            if (Opposite_ || Node_.Relations->MaterialSpans.size() != 1) return 0;
            const auto& _Span = Node_.Relations->MaterialSpans.front();
            const auto _Host = [](const Tube::SMaterialSpanEndpoint& Endpoint_) {
                return Endpoint_.Kind == Tube::EMaterialSpanEndpointKind::HostBoundary
                    && Endpoint_.Boundary && Endpoint_.Boundary->HostNodeID == "base";
            };
            const double _A = _Span.Start.Station.Value * AxisComponent_;
            const double _B = _Span.End.Station.Value * AxisComponent_;
            if (!std::isfinite(_A) || !std::isfinite(_B)
                || std::abs(_B - _A) <= Tolerance_) return 0;
            if (_Extent == Tube::ERemovalExtentKind::Through
                && _Host(_Span.Start) && _Host(_Span.End))
            {
                // A centred web is accessible from either side; use the
                // positive side deterministically. The full editor replay
                // still rejects any other material blocking that approach.
                return std::max(_A, _B) <= Tolerance_ ? -1 : 1;
            }
            if (_Extent == Tube::ERemovalExtentKind::Blind)
            {
                if (_Host(_Span.Start)
                    && _Span.End.Kind == Tube::EMaterialSpanEndpointKind::FeatureTermination)
                    return _A > _B ? 1 : -1;
                if (_Host(_Span.End)
                    && _Span.Start.Kind == Tube::EMaterialSpanEndpointKind::FeatureTermination)
                    return _B > _A ? 1 : -1;
            }
            return 0;
        };

        std::string _Face;
        double _Offset = 0.0;
        if (std::abs(_Axis.Z) >= 1.0 - 1.0e-6
            && std::abs(_Axis.X) <= 1.0e-6
            && std::abs(_Axis.Y) <= 1.0e-6)
        {
            const double _Min = std::min(_First.Z, _Last.Z);
            const double _Max = std::max(_First.Z, _Last.Z);
            const auto _ObservedSide = _ObservedEntrySide(_Axis.Z);
            if (Opposite_ && _Min < -Tolerance_ && _Max > Tolerance_) _Face = "top";
            else if (_ObservedSide != 0) _Face = _ObservedSide > 0 ? "top" : "bottom";
            else if (!Opposite_ && _Min >= -Tolerance_ && _Max > Tolerance_) _Face = "top";
            else if (!Opposite_ && _Max <= Tolerance_ && _Min < -Tolerance_) _Face = "bottom";
            _Offset = _First.Y;
        }
        else if (std::abs(_Axis.Y) >= 1.0 - 1.0e-6
            && std::abs(_Axis.X) <= 1.0e-6
            && std::abs(_Axis.Z) <= 1.0e-6)
        {
            const double _Min = std::min(_First.Y, _Last.Y);
            const double _Max = std::max(_First.Y, _Last.Y);
            const auto _ObservedSide = _ObservedEntrySide(_Axis.Y);
            if (Opposite_ && _Min < -Tolerance_ && _Max > Tolerance_) _Face = "right";
            else if (_ObservedSide != 0) _Face = _ObservedSide > 0 ? "right" : "left";
            else if (!Opposite_ && _Min >= -Tolerance_ && _Max > Tolerance_) _Face = "right";
            else if (!Opposite_ && _Max <= Tolerance_ && _Min < -Tolerance_) _Face = "left";
            _Offset = _First.Z;
        }
        // The editor reflects the second cutter across both transverse axes.
        // Two coaxial BRep openings have the same placement only at zero offset.
        if (_Face.empty() || !std::isfinite(_Offset)
            || (Opposite_ && std::abs(_Offset) > Tolerance_)) return false;

        Features_.emplace_back(ObjectMap{
            {"id", std::string("recovered-") + std::to_string(Features_.size() + 1)},
            {"type", std::string("circle")},
            {"toolRef", ObjectMap{{"id", std::string("circle")}, {"scope", std::string("system")}}},
            {"toolParameters", ObjectMap{{"diameter", 2.0 * _Circle->Radius}}},
            {"face", _Face},
            {"reference", std::string("start")},
            {"layoutDatum", std::string("base")},
            {"station", _First.X},
            {"offset", _Offset},
            {"diameter", 2.0 * _Circle->Radius},
            {"blindHole", _Extent == Tube::ERemovalExtentKind::Blind},
            {"cutDepth", _Depth},
            {"opposite", Opposite_},
            {"enabled", true}
        });
        return true;
    }

    // The machining definition stores exact curves, never a sampled contour or
    // the imported BRep. Curve parameters stay in their original parameterization.
    bool MapCurvePocket(const Tube::SSolidNode& Node_,
        const Tube::SExtrudedRegionNode& Cut_, double Length_, double Tolerance_,
        iCAX::Data::VariantArray& Features_)
    {
        using namespace iCAX::GeometryData;
        using iCAX::Data::VariantArray;
        const auto _Source = Node_.Metadata.find("source");
        const auto _Proof = Node_.Metadata.find("independentReplay");
        if (_Source == Node_.Metadata.end() || !Node_.Relations || !Node_.Relations->MaterialEffect
            || Node_.Relations->MaterialEffect->Role != Tube::EFeatureMaterialRole::Penetration
            || !Node_.RemovalSemantics || Cut_.Section.Boundaries.empty()) return false;
        const bool _Blind = _Source->second == "occ.planar-cap-extrusion"
            && Node_.RemovalSemantics->Extent == Tube::ERemovalExtentKind::Blind;
        // These finite cutters have been replayed against the entire removed
        // component. Their end planes are the actual material boundaries;
        // unlike a padded surface generator, they need no inferred wall span.
        const bool _FiniteThrough = _Source->second == "occ.removal-extrusion"
            && _Proof != Node_.Metadata.end() && _Proof->second == "complete-removal"
            && Node_.RemovalSemantics->Extent == Tube::ERemovalExtentKind::Through
            && Node_.Relations->MaterialSpans.empty();
        if (!_Blind && !_FiniteThrough) return false;
        double _EntryStation = 0.0, _EndStation = 0.0, _Depth = 0.0;
        if (_Blind) {
            if (Node_.Relations->MaterialSpans.size() != 1) return false;
            const auto& _Span = Node_.Relations->MaterialSpans.front();
            const auto _Host = [](const Tube::SMaterialSpanEndpoint& E_) {
                return E_.Kind == Tube::EMaterialSpanEndpointKind::HostBoundary
                    && E_.Boundary && E_.Boundary->HostNodeID == "base";
            };
            const auto* _Entry = _Host(_Span.Start) ? &_Span.Start : _Host(_Span.End) ? &_Span.End : nullptr;
            const auto* _End = _Entry == &_Span.Start ? &_Span.End : &_Span.Start;
            if (!_Entry || _End->Kind != Tube::EMaterialSpanEndpointKind::FeatureTermination
                || _Entry->Station.Observability != Tube::EParameterObservability::Observed
                || _End->Station.Observability != Tube::EParameterObservability::Observed) return false;
            _EntryStation = _Entry->Station.Value; _EndStation = _End->Station.Value;
            _Depth = std::abs(_EntryStation - _EndStation);
            if (!std::isfinite(_Depth) || _Depth <= Tolerance_) return false;
        }
        const auto& _Frame = Cut_.Frame;
        const auto _Dot = [](const auto& A_, const auto& B_) { return A_.X*B_.X + A_.Y*B_.Y + A_.Z*B_.Z; };
        const auto _Finite = [](const auto& P_) { return std::isfinite(P_.X) && std::isfinite(P_.Y) && std::isfinite(P_.Z); };
        if (!_Finite(_Frame.Location) || !_Finite(_Frame.XDirection) || !_Finite(_Frame.YDirection)
            || !_Finite(_Frame.ZDirection) || !std::isfinite(Cut_.First) || !std::isfinite(Cut_.Last)
            || Cut_.Last - Cut_.First <= Tolerance_
            || std::abs(_Dot(_Frame.XDirection,_Frame.XDirection)-1.0)>1e-6
            || std::abs(_Dot(_Frame.YDirection,_Frame.YDirection)-1.0)>1e-6
            || std::abs(_Dot(_Frame.ZDirection,_Frame.ZDirection)-1.0)>1e-6
            || std::abs(_Dot(_Frame.XDirection,_Frame.YDirection))>1e-6
            || std::abs(_Dot(_Frame.XDirection,_Frame.ZDirection))>1e-6
            || std::abs(_Dot(_Frame.YDirection,_Frame.ZDirection))>1e-6) return false;
        const auto& _Axis = _Frame.ZDirection;
        const bool _Z = std::abs(_Axis.Z) >= 1.0-1e-6 && std::abs(_Axis.X)<=1e-6 && std::abs(_Axis.Y)<=1e-6;
        const bool _Y = std::abs(_Axis.Y) >= 1.0-1e-6 && std::abs(_Axis.X)<=1e-6 && std::abs(_Axis.Z)<=1e-6;
        if (!_Z && !_Y) return false;
        const double _OriginStation = _Dot(_Frame.Location,_Axis);
        const double _Min = _OriginStation + std::min(Cut_.First,Cut_.Last);
        const double _Max = _OriginStation + std::max(Cut_.First,Cut_.Last);
        if (!std::isfinite(_Min) || !std::isfinite(_Max)) return false;
        if (_Blind && (_Min > std::min(_EntryStation,_EndStation)+Tolerance_
            || _Max < std::max(_EntryStation,_EndStation)-Tolerance_)) return false;
        const double _AxisComponent = _Z ? _Axis.Z : _Axis.Y;
        // Choose the accessible outer side. A centred web uses the positive
        // side; the mandatory full editor replay rejects blocked approaches
        // or a cutter reaching an additional wall.
        const bool _Positive = _Blind ? (_EntryStation-_EndStation)*_AxisComponent>0
            : std::max(_Min*_AxisComponent,_Max*_AxisComponent)>Tolerance_;
        const std::string _Face = _Z ? (_Positive?"top":"bottom") : (_Positive?"right":"left");
        const auto _Project = [&](const Point2& P_) -> Point2 {
            const auto _World = WorldPoint(_Frame,P_,0);
            return {_World.X,_Z?_World.Y:_World.Z};
        };
        double _X0=(std::numeric_limits<double>::max)(), _V0=_X0, _X1=-_X0, _V1=-_X0;
        const auto _Bound = [&](const Point2& P_) {
            const auto _P=_Project(P_);
            if (!std::isfinite(_P.X)||!std::isfinite(_P.Y)) return false;
            _X0=std::min(_X0,_P.X); _X1=std::max(_X1,_P.X);
            _V0=std::min(_V0,_P.Y); _V1=std::max(_V1,_P.Y); return true;
        };
        for (const auto& _Loop:Cut_.Section.Boundaries) {
            if (_Loop.Curves.empty()) return false;
            for (const auto& _Edge:_Loop.Curves) {
                if (!std::isfinite(_Edge.Segment.Range.First)||!std::isfinite(_Edge.Segment.Range.Last)
                    || _Edge.Segment.Range.Last<=_Edge.Segment.Range.First) return false;
                const bool _Supported=std::visit([&](const auto& C_) {
                    using T=std::decay_t<decltype(C_)>;
                    if constexpr(std::is_same_v<T,Segment2>) return _Bound(C_.Start)&&_Bound(C_.End);
                    else if constexpr(std::is_same_v<T,BSpline2>||std::is_same_v<T,NURBS2>) {
                        if (C_.Poles.size()<2) return false;
                        for (const auto& _P:C_.Poles) if (!_Bound(_P)) return false;
                        return true;
                    } else if constexpr(std::is_same_v<T,Circle2>||std::is_same_v<T,Ellipse2>) {
                        if (_Edge.Segment.Range.Last-_Edge.Segment.Range.First>2*kPi+1e-6) return false;
                        const double _A=[&]{if constexpr(std::is_same_v<T,Circle2>) return C_.Radius; else return C_.MajorRadius;}();
                        const double _B=[&]{if constexpr(std::is_same_v<T,Circle2>) return C_.Radius; else return C_.MinorRadius;}();
                        if (!std::isfinite(_A)||!std::isfinite(_B)||_A<=Tolerance_||_B<=Tolerance_) return false;
                        for (double _U:{-1.,1.}) for (double _V:{-1.,1.})
                            if (!_Bound({C_.Placement.Location.X+_U*_A*C_.Placement.XDirection.X+_V*_B*C_.Placement.YDirection.X,
                                C_.Placement.Location.Y+_U*_A*C_.Placement.XDirection.Y+_V*_B*C_.Placement.YDirection.Y})) return false;
                        return true;
                    } else return false;
                },_Edge.Segment.Curve);
                if (!_Supported) return false;
            }
        }
        const double _ControlCenter=(_X0+_X1)/2, _Offset=(_V0+_V1)/2;
        if (!std::isfinite(_ControlCenter)||!std::isfinite(_Offset)) return false;
        // A trimmed spline may have poles far outside its retained interval.
        // This is only an editable translation origin, not a geometry bound.
        const double _Station=std::clamp(_ControlCenter,0.0,Length_);
        const auto _Point = [&](const Point2& P_) { const auto _P=_Project(P_); return VariantArray{_P.X-_Station,_P.Y-_Offset}; };
        VariantArray _Contours;
        for (const auto& _Loop:Cut_.Section.Boundaries) {
            VariantArray _Segments;
            for (const auto& _Edge:_Loop.Curves) {
                const auto& _Range=_Edge.Segment.Range;
                std::visit([&](const auto& C_) {
                    using T=std::decay_t<decltype(C_)>;
                    if constexpr(std::is_same_v<T,Segment2>) {
                        _Segments.emplace_back(ObjectMap{{"kind",std::string("line")},
                            {"start",_Point(_Range.Reversed?C_.End:C_.Start)},
                            {"end",_Point(_Range.Reversed?C_.Start:C_.End)}});
                    } else if constexpr(std::is_same_v<T,BSpline2>||std::is_same_v<T,NURBS2>) {
                        VariantArray _Poles,_Knots,_Multiplicities;
                        for (const auto& _P:C_.Poles) _Poles.emplace_back(_Point(_P));
                        for (auto _K:C_.Knots) _Knots.emplace_back(_K);
                        for (auto _M:C_.Multiplicities) _Multiplicities.emplace_back(_M);
                        ObjectMap _Item{{"kind",std::string(std::is_same_v<T,NURBS2>?"nurbs":"bspline")},
                            {"controlPoints",_Poles},{"degree",C_.Degree},{"knots",_Knots},{"multiplicities",_Multiplicities},
                            {"periodic",C_.Periodic},{"startParameter",_Range.First},{"endParameter",_Range.Last},
                            {"reversed",_Range.Reversed}};
                        if constexpr(std::is_same_v<T,NURBS2>) {
                            VariantArray _Weights; for(auto _W:C_.Weights) _Weights.emplace_back(_W); _Item["weights"]=_Weights;
                        }
                        _Segments.emplace_back(std::move(_Item));
                    } else if constexpr(std::is_same_v<T,Circle2>) {
                        const auto _At=[&](double T_) { return _Point({C_.Placement.Location.X+C_.Radius*(C_.Placement.XDirection.X*std::cos(T_)+C_.Placement.YDirection.X*std::sin(T_)),
                            C_.Placement.Location.Y+C_.Radius*(C_.Placement.XDirection.Y*std::cos(T_)+C_.Placement.YDirection.Y*std::sin(T_))}); };
                        const double _From=_Range.Reversed?_Range.Last:_Range.First, _To=_Range.Reversed?_Range.First:_Range.Last;
                        const int _Count=std::max(1,static_cast<int>(std::ceil(std::abs(_To-_From)/kPi)));
                        for(int _I=0;_I<_Count;++_I) {
                            const double _A=_From+(_To-_From)*_I/_Count, _B=_From+(_To-_From)*(_I+1)/_Count;
                            _Segments.emplace_back(ObjectMap{{"kind",std::string("arc")},{"start",_At(_A)},
                                {"middle",_At((_A+_B)/2)},{"end",_At(_B)}});
                        }
                    } else if constexpr(std::is_same_v<T,Ellipse2>) {
                        const auto _Center=_Project(C_.Placement.Location);
                        const auto _X=_Project({C_.Placement.Location.X+C_.Placement.XDirection.X,C_.Placement.Location.Y+C_.Placement.XDirection.Y});
                        const auto _Y=_Project({C_.Placement.Location.X+C_.Placement.YDirection.X,C_.Placement.Location.Y+C_.Placement.YDirection.Y});
                        const bool _Mirror=(_X.X-_Center.X)*(_Y.Y-_Center.Y)-(_X.Y-_Center.Y)*(_Y.X-_Center.X)<0;
                        _Segments.emplace_back(ObjectMap{{"kind",std::string("ellipseArc")},{"center",_Point(C_.Placement.Location)},
                            {"majorRadius",C_.MajorRadius},{"minorRadius",C_.MinorRadius},
                            {"rotation",std::atan2(_X.Y-_Center.Y,_X.X-_Center.X)},
                            {"startAngle",_Mirror?-_Range.Last:_Range.First},{"endAngle",_Mirror?-_Range.First:_Range.Last},
                            {"reversed",_Range.Reversed!=_Mirror}});
                    }
                },_Edge.Segment.Curve);
            }
            _Contours.emplace_back(ObjectMap{{"kind",std::string("path")},{"segments",_Segments}});
        }
        Features_.emplace_back(ObjectMap{
            {"id",std::string("recovered-")+std::to_string(Features_.size()+1)},
            {"type",std::string("curve-pocket")},
            {"toolTarget",std::string("side")},
            {"recordKind",std::string("profile")},
            {"toolRef",ObjectMap{{"id",std::string("curve-pocket")},{"scope",std::string("system")}}},
            {"toolParameters",ObjectMap{}}, {"section",ObjectMap{{"source",std::string("recovered")},
                {"name",std::string("识别曲线")},{"profile",ObjectMap{{"contours",_Contours}}}}},
            {"face",_Face},{"reference",std::string("start")},{"layoutDatum",std::string("base")},
            {"station",_Station},{"offset",_Offset},{"rotation",0.0},{"blindHole",_Blind},
            {"cutDepth",_Depth},{"opposite",false},{"enabled",true}});
        return true;
    }

    bool MapOppositeWallCircle(const Tube::SSolidNode& AlternativeNode_,
        const Tube::SAlternativeNode& Alternative_,
        const std::map<std::string, const Tube::SSolidNode*>& Nodes_,
        double Length_, double Tolerance_, iCAX::Data::VariantArray& Features_)
    {
        const auto _Source = AlternativeNode_.Metadata.find("source");
        if (_Source == AlternativeNode_.Metadata.end()
            || _Source->second != "occ.part-surface-group-alternative"
            || Alternative_.RecommendedCandidateID != "single-cross-material-generator"
            || (!Alternative_.SelectedCandidateID.empty()
                && Alternative_.SelectedCandidateID != Alternative_.RecommendedCandidateID)
            || Alternative_.Candidates.size() != 2
            || !AlternativeNode_.RemovalSemantics
            || AlternativeNode_.RemovalSemantics->Extent != Tube::ERemovalExtentKind::Through)
            return false;
        const auto _Candidate = std::find_if(Alternative_.Candidates.begin(),
            Alternative_.Candidates.end(), [](const auto& Candidate_) {
                return Candidate_.ID == "single-cross-material-generator";
            });
        if (_Candidate == Alternative_.Candidates.end()) return false;
        const auto _Generator = Nodes_.find(_Candidate->CandidateNodeID);
        if (_Generator == Nodes_.end()) return false;
        const auto* _Node = _Generator->second;
        const auto* _Cut = std::get_if<Tube::SExtrudedRegionNode>(&_Node->Data);
        const auto _GeneratorSource = _Node->Metadata.find("source");
        if (!_Cut || _GeneratorSource == _Node->Metadata.end()
            || _GeneratorSource->second != "occ.part-surface-group"
            || !_Node->Relations || !_Node->RemovalSemantics
            || _Node->RemovalSemantics->Extent != Tube::ERemovalExtentKind::Through
            || _Node->Relations->MaterialSpans.size() != 2)
            return false;
        const auto& _Spans = _Node->Relations->MaterialSpans;
        std::array<std::pair<double, double>, 2> _Ranges;
        for (std::size_t _Index = 0; _Index < 2; ++_Index)
        {
            const auto& _Span = _Spans[_Index];
            if (_Span.Start.Kind != Tube::EMaterialSpanEndpointKind::HostBoundary
                || _Span.End.Kind != Tube::EMaterialSpanEndpointKind::HostBoundary
                || !_Span.Start.Boundary || !_Span.End.Boundary
                || _Span.Start.Boundary->HostNodeID != "base"
                || _Span.End.Boundary->HostNodeID != "base") return false;
            const double _A = _Span.Start.Station.Value;
            const double _B = _Span.End.Station.Value;
            if (!std::isfinite(_A) || !std::isfinite(_B)
                || std::abs(_B - _A) <= Tolerance_) return false;
            _Ranges[_Index] = { std::min(_A, _B), std::max(_A, _B) };
        }
        if (_Ranges[0].first > _Ranges[1].first) std::swap(_Ranges[0], _Ranges[1]);
        // The signed stations are world projections on the perpendicular cut
        // axis. In the centred drawing frame, one wall must lie on each side.
        if (_Ranges[0].second >= -Tolerance_
            || _Ranges[1].first <= Tolerance_) return false;
        const double _FirstWall = _Ranges[0].second - _Ranges[0].first;
        const double _SecondWall = _Ranges[1].second - _Ranges[1].first;
        if (std::abs(_FirstWall - _SecondWall)
            > std::max(10.0 * Tolerance_, 1.0e-3 * std::max(_FirstWall, _SecondWall)))
            return false;
        if (MapCircularSideHole(*_Node, *_Cut, Length_, Tolerance_, Features_, true))
            return true;

        // An eccentric coaxial pair cannot use the editor's opposite tool:
        // that tool reflects both transverse coordinates. Keep the observed
        // common centreline and enter each proven wall independently instead.
        // Each independent tool stops after the first material interval; the
        // caller still compares the complete editor replay with the source.
        if (_Cut->Section.Boundaries.size() != 1
            || _Cut->Section.Boundaries.front().Curves.size() != 1) return false;
        const auto* _Circle = std::get_if<iCAX::GeometryData::Circle2>(
            &_Cut->Section.Boundaries.front().Curves.front().Segment.Curve);
        if (!_Circle) return false;
        const auto _FirstPoint = WorldPoint(_Cut->Frame, _Circle->Placement.Location, _Cut->First);
        const auto _LastPoint = WorldPoint(_Cut->Frame, _Circle->Placement.Location, _Cut->Last);
        const auto& _Axis = _Cut->Frame.ZDirection;
        const auto _Projection = [&_Axis](const SPoint& Point_) {
            return Point_.X * _Axis.X + Point_.Y * _Axis.Y + Point_.Z * _Axis.Z;
        };
        const double _FirstStation = _Projection(_FirstPoint);
        const double _LastStation = _Projection(_LastPoint);
        if (!std::isfinite(_FirstStation) || !std::isfinite(_LastStation)
            || std::min(_FirstStation, _LastStation) > _Ranges[0].first + Tolerance_
            || std::max(_FirstStation, _LastStation) < _Ranges[1].second - Tolerance_)
            return false;

        iCAX::Data::VariantArray _Independent;
        for (const auto& _Span : _Spans)
        {
            if (_Span.Start.Station.Observability != Tube::EParameterObservability::Observed
                || _Span.End.Station.Observability != Tube::EParameterObservability::Observed)
                return false;
            auto _WallNode = *_Node;
            _WallNode.Relations->MaterialSpans = { _Span };
            if (!MapCircularSideHole(_WallNode, *_Cut, Length_, Tolerance_, _Independent))
                return false;
        }
        const auto _FirstFeature = _Independent[0].To<ObjectMap>();
        const auto _SecondFeature = _Independent[1].To<ObjectMap>();
        const auto _FirstFace = _FirstFeature.at("face").To<std::string>();
        const auto _SecondFace = _SecondFeature.at("face").To<std::string>();
        const bool _OppositeFaces = (_FirstFace == "top" && _SecondFace == "bottom")
            || (_FirstFace == "bottom" && _SecondFace == "top")
            || (_FirstFace == "left" && _SecondFace == "right")
            || (_FirstFace == "right" && _SecondFace == "left");
        const double _Offset = _FirstFeature.at("offset").To<double>();
        if (!_OppositeFaces || std::abs(_Offset) <= Tolerance_
            || std::abs(_Offset - _SecondFeature.at("offset").To<double>()) > Tolerance_
            || std::abs(_FirstFeature.at("station").To<double>()
                - _SecondFeature.at("station").To<double>()) > Tolerance_) return false;

        // Both faces use the same signed global transverse offset. Flipping
        // its sign on the negative face would move the second hole off-axis.
        for (auto& _Value : _Independent)
        {
            auto _Feature = _Value.To<ObjectMap>();
            _Feature["id"] = std::string("recovered-") + std::to_string(Features_.size() + 1);
            Features_.emplace_back(std::move(_Feature));
        }
        return true;
    }

    bool MapExactCurveAlternative(const Tube::SSolidNode& Node_,
        const Tube::SAlternativeNode& Alternative_,
        const std::map<std::string, const Tube::SSolidNode*>& Nodes_,
        double Length_, double Tolerance_, iCAX::Data::VariantArray& Features_)
    {
        const auto _Source = Node_.Metadata.find("source");
        if (_Source == Node_.Metadata.end()
            || _Source->second != "occ.enumerated-hypotheses") return false;
        // Equivalent construction directions need not all be expressible by
        // the side editor. Select only a complete, independently replayed exact
        // region; a confidence score or an approximate alternative is no proof.
        for (const auto& _Candidate : Alternative_.Candidates)
        {
            if (!Alternative_.SelectedCandidateID.empty()
                && _Candidate.ID != Alternative_.SelectedCandidateID) continue;
            const auto _Found = Nodes_.find(_Candidate.CandidateNodeID);
            if (_Found == Nodes_.end()) continue;
            const auto& _Node = *_Found->second;
            const auto* _Cut = std::get_if<Tube::SExtrudedRegionNode>(&_Node.Data);
            const auto _Proof = _Node.Metadata.find("independentReplay");
            if (!_Cut || _Proof == _Node.Metadata.end()
                || _Proof->second != "complete-removal") continue;
            if (MapCurvePocket(_Node, *_Cut, Length_, Tolerance_, Features_))
                return true;
        }
        return false;
    }
}

SRecoveredPartDrawingFeatures MapNeutralTubeFeatures(
    const Tube::CTubeNeutralGeometry& Geometry_, double BlankLength_, double Tolerance_)
{
    SRecoveredPartDrawingFeatures _Result;
    if (!std::isfinite(BlankLength_) || BlankLength_ <= 0.0
        || !std::isfinite(Tolerance_) || Tolerance_ <= 0.0)
    {
        _Result.UnsupportedNodeIDs.push_back("invalid-dimensions");
        return _Result;
    }
    std::map<std::string, const Tube::SSolidNode*> _Nodes;
    for (const auto& _Node : Geometry_.SolidNodes)
    {
        if (_Node.ID.empty() || !_Nodes.emplace(_Node.ID, &_Node).second)
        {
            _Result.UnsupportedNodeIDs.push_back("duplicate-or-empty-node-id");
            return _Result;
        }
    }
    const auto _Base = _Nodes.find(Geometry_.BaseNodeID);
    if (_Base == _Nodes.end())
    {
        _Result.UnsupportedNodeIDs.push_back(Geometry_.BaseNodeID);
        return _Result;
    }
    const auto* _Extrusion = std::get_if<Tube::SExtrudedRegionNode>(&_Base->second->Data);
    if (!_Extrusion || !ValidBase(*_Extrusion, BlankLength_, Tolerance_))
    {
        _Result.UnsupportedNodeIDs.push_back(Geometry_.BaseNodeID);
        return _Result;
    }
    if (Geometry_.RecognitionStatus == Tube::ERecognitionStatus::Partial
        || Geometry_.RecognitionStatus == Tube::ERecognitionStatus::Failed)
    {
        _Result.UnsupportedNodeIDs.push_back(Geometry_.RootNodeID);
        return _Result;
    }
    if (Geometry_.RootNodeID == Geometry_.BaseNodeID)
    {
        _Result.Complete = true;
        return _Result;
    }
    const auto _Root = _Nodes.find(Geometry_.RootNodeID);
    const auto* _Difference = _Root == _Nodes.end() ? nullptr
        : std::get_if<Tube::SBooleanNode>(&_Root->second->Data);
    if (!_Difference || _Difference->Operation != Tube::EBooleanOperation::Difference
        || _Difference->Children.size() != 2
        || _Difference->Children.front() != Geometry_.BaseNodeID)
    {
        _Result.UnsupportedNodeIDs.push_back(Geometry_.RootNodeID);
        return _Result;
    }
    std::vector<const Tube::SSolidNode*> _Leaves;
    std::set<std::string> _Visited;
    if (!CollectRemovals(_Nodes, _Difference->Children[1], _Leaves, _Visited))
    {
        _Result.UnsupportedNodeIDs.push_back(_Difference->Children[1]);
        return _Result;
    }
    for (const auto* _Node : _Leaves)
    {
        bool _Mapped = false;
        if (const auto* _HalfSpace = std::get_if<Tube::SHalfSpaceNode>(&_Node->Data))
            _Mapped = MapEndTrim(*_Node, *_HalfSpace, BlankLength_, Tolerance_, _Result.Ends);
        else if (const auto* _Cut = std::get_if<Tube::SExtrudedRegionNode>(&_Node->Data))
            _Mapped = MapEndStep(*_Node, *_Cut, *_Extrusion, BlankLength_, Tolerance_, _Result.Ends)
                || MapCircularSideHole(*_Node, *_Cut, BlankLength_, Tolerance_, _Result.Features)
                || MapCurvePocket(*_Node, *_Cut, BlankLength_, Tolerance_, _Result.Features);
        else if (const auto* _Alternative = std::get_if<Tube::SAlternativeNode>(&_Node->Data))
            _Mapped = MapOppositeWallCircle(*_Node, *_Alternative, _Nodes,
                BlankLength_, Tolerance_, _Result.Features)
                || MapExactCurveAlternative(*_Node, *_Alternative, _Nodes,
                    BlankLength_, Tolerance_, _Result.Features);
        if (!_Mapped) _Result.UnsupportedNodeIDs.push_back(_Node->ID);
    }
    _Result.Complete = _Result.UnsupportedNodeIDs.empty();
    return _Result;
}
}
