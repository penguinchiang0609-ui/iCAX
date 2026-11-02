#pragma once
#include "GeometryData/TubeNeutralPersistence.h"
#include <string_view>

namespace iCAX::GeometryData {
inline auto FieldNames(Point2*) { return std::array{std::string_view("X"), std::string_view("Y")}; }
inline std::string_view TypeName(Point2*) { return "Point2"; }
inline auto FieldNames(TextureCoordinate2*) { return std::array{std::string_view("U"), std::string_view("V")}; }
inline std::string_view TypeName(TextureCoordinate2*) { return "TextureCoordinate2"; }
inline auto FieldNames(Point3*) { return std::array{std::string_view("X"), std::string_view("Y"), std::string_view("Z")}; }
inline std::string_view TypeName(Point3*) { return "Point3"; }
inline auto FieldNames(Vector2*) { return std::array{std::string_view("X"), std::string_view("Y")}; }
inline std::string_view TypeName(Vector2*) { return "Vector2"; }
inline auto FieldNames(Vector3*) { return std::array{std::string_view("X"), std::string_view("Y"), std::string_view("Z")}; }
inline std::string_view TypeName(Vector3*) { return "Vector3"; }
inline auto FieldNames(Direction2*) { return std::array{std::string_view("X"), std::string_view("Y")}; }
inline std::string_view TypeName(Direction2*) { return "Direction2"; }
inline auto FieldNames(Direction3*) { return std::array{std::string_view("X"), std::string_view("Y"), std::string_view("Z")}; }
inline std::string_view TypeName(Direction3*) { return "Direction3"; }
inline auto FieldNames(Matrix3x3*) { return std::array{std::string_view("Values")}; }
inline std::string_view TypeName(Matrix3x3*) { return "Matrix3x3"; }
inline auto FieldNames(Matrix4x4*) { return std::array{std::string_view("Values")}; }
inline std::string_view TypeName(Matrix4x4*) { return "Matrix4x4"; }
inline auto FieldNames(Transform2*) { return std::array{std::string_view("Matrix")}; }
inline std::string_view TypeName(Transform2*) { return "Transform2"; }
inline auto FieldNames(Transform3*) { return std::array{std::string_view("Matrix")}; }
inline std::string_view TypeName(Transform3*) { return "Transform3"; }
inline auto FieldNames(Quaternion*) { return std::array{std::string_view("X"), std::string_view("Y"), std::string_view("Z"), std::string_view("W")}; }
inline std::string_view TypeName(Quaternion*) { return "Quaternion"; }
inline auto FieldNames(Axis2*) { return std::array{std::string_view("Location"), std::string_view("Direction")}; }
inline std::string_view TypeName(Axis2*) { return "Axis2"; }
inline auto FieldNames(Axis3*) { return std::array{std::string_view("Location"), std::string_view("Direction")}; }
inline std::string_view TypeName(Axis3*) { return "Axis3"; }
inline auto FieldNames(Placement2*) { return std::array{std::string_view("Location"), std::string_view("XDirection"), std::string_view("YDirection")}; }
inline std::string_view TypeName(Placement2*) { return "Placement2"; }
inline auto FieldNames(Placement3*) { return std::array{std::string_view("Location"), std::string_view("XDirection"), std::string_view("YDirection"), std::string_view("ZDirection")}; }
inline std::string_view TypeName(Placement3*) { return "Placement3"; }
inline auto FieldNames(Plane3*) { return std::array{std::string_view("Location"), std::string_view("Normal"), std::string_view("XDirection")}; }
inline std::string_view TypeName(Plane3*) { return "Plane3"; }
inline auto FieldNames(BoundingBox2*) { return std::array{std::string_view("Min"), std::string_view("Max"), std::string_view("Empty")}; }
inline std::string_view TypeName(BoundingBox2*) { return "BoundingBox2"; }
inline auto FieldNames(BoundingBox3*) { return std::array{std::string_view("Min"), std::string_view("Max"), std::string_view("Empty")}; }
inline std::string_view TypeName(BoundingBox3*) { return "BoundingBox3"; }
inline auto FieldNames(OrientedBox2*) { return std::array{std::string_view("Placement"), std::string_view("HalfWidth"), std::string_view("HalfHeight")}; }
inline std::string_view TypeName(OrientedBox2*) { return "OrientedBox2"; }
inline auto FieldNames(OrientedBox3*) { return std::array{std::string_view("Placement"), std::string_view("HalfWidth"), std::string_view("HalfDepth"), std::string_view("HalfHeight")}; }
inline std::string_view TypeName(OrientedBox3*) { return "OrientedBox3"; }
inline auto FieldNames(ParameterRange*) { return std::array{std::string_view("First"), std::string_view("Last"), std::string_view("Reversed")}; }
inline std::string_view TypeName(ParameterRange*) { return "ParameterRange"; }
inline auto FieldNames(SurfaceParameterRange*) { return std::array{std::string_view("UFirst"), std::string_view("ULast"), std::string_view("VFirst"), std::string_view("VLast"), std::string_view("UReversed"), std::string_view("VReversed")}; }
inline std::string_view TypeName(SurfaceParameterRange*) { return "SurfaceParameterRange"; }
inline auto FieldNames(Line2*) { return std::array{std::string_view("Axis")}; }
inline std::string_view TypeName(Line2*) { return "Line2"; }
inline auto FieldNames(Line3*) { return std::array{std::string_view("Axis")}; }
inline std::string_view TypeName(Line3*) { return "Line3"; }
inline auto FieldNames(Ray2*) { return std::array{std::string_view("Axis"), std::string_view("First")}; }
inline std::string_view TypeName(Ray2*) { return "Ray2"; }
inline auto FieldNames(Ray3*) { return std::array{std::string_view("Axis"), std::string_view("First")}; }
inline std::string_view TypeName(Ray3*) { return "Ray3"; }
inline auto FieldNames(Segment2*) { return std::array{std::string_view("Start"), std::string_view("End")}; }
inline std::string_view TypeName(Segment2*) { return "Segment2"; }
inline auto FieldNames(Segment3*) { return std::array{std::string_view("Start"), std::string_view("End")}; }
inline std::string_view TypeName(Segment3*) { return "Segment3"; }
inline auto FieldNames(Circle2*) { return std::array{std::string_view("Placement"), std::string_view("Radius")}; }
inline std::string_view TypeName(Circle2*) { return "Circle2"; }
inline auto FieldNames(Circle3*) { return std::array{std::string_view("Placement"), std::string_view("Radius")}; }
inline std::string_view TypeName(Circle3*) { return "Circle3"; }
inline auto FieldNames(Arc2*) { return std::array{std::string_view("Basis"), std::string_view("StartAngle"), std::string_view("EndAngle"), std::string_view("CounterClockwise")}; }
inline std::string_view TypeName(Arc2*) { return "Arc2"; }
inline auto FieldNames(Arc3*) { return std::array{std::string_view("Basis"), std::string_view("StartAngle"), std::string_view("EndAngle"), std::string_view("CounterClockwise")}; }
inline std::string_view TypeName(Arc3*) { return "Arc3"; }
inline auto FieldNames(Ellipse2*) { return std::array{std::string_view("Placement"), std::string_view("MajorRadius"), std::string_view("MinorRadius")}; }
inline std::string_view TypeName(Ellipse2*) { return "Ellipse2"; }
inline auto FieldNames(Ellipse3*) { return std::array{std::string_view("Placement"), std::string_view("MajorRadius"), std::string_view("MinorRadius")}; }
inline std::string_view TypeName(Ellipse3*) { return "Ellipse3"; }
inline auto FieldNames(EllipseArc2*) { return std::array{std::string_view("Basis"), std::string_view("StartAngle"), std::string_view("EndAngle"), std::string_view("CounterClockwise")}; }
inline std::string_view TypeName(EllipseArc2*) { return "EllipseArc2"; }
inline auto FieldNames(EllipseArc3*) { return std::array{std::string_view("Basis"), std::string_view("StartAngle"), std::string_view("EndAngle"), std::string_view("CounterClockwise")}; }
inline std::string_view TypeName(EllipseArc3*) { return "EllipseArc3"; }
inline auto FieldNames(Polyline2*) { return std::array{std::string_view("Points"), std::string_view("Closed")}; }
inline std::string_view TypeName(Polyline2*) { return "Polyline2"; }
inline auto FieldNames(Polyline3*) { return std::array{std::string_view("Points"), std::string_view("Closed")}; }
inline std::string_view TypeName(Polyline3*) { return "Polyline3"; }
inline auto FieldNames(Bezier2*) { return std::array{std::string_view("Poles")}; }
inline std::string_view TypeName(Bezier2*) { return "Bezier2"; }
inline auto FieldNames(Bezier3*) { return std::array{std::string_view("Poles")}; }
inline std::string_view TypeName(Bezier3*) { return "Bezier3"; }
inline auto FieldNames(BSpline2*) { return std::array{std::string_view("Degree"), std::string_view("Poles"), std::string_view("Knots"), std::string_view("Multiplicities"), std::string_view("Periodic")}; }
inline std::string_view TypeName(BSpline2*) { return "BSpline2"; }
inline auto FieldNames(BSpline3*) { return std::array{std::string_view("Degree"), std::string_view("Poles"), std::string_view("Knots"), std::string_view("Multiplicities"), std::string_view("Periodic")}; }
inline std::string_view TypeName(BSpline3*) { return "BSpline3"; }
inline auto FieldNames(NURBS2*) { return std::array{std::string_view("Degree"), std::string_view("Poles"), std::string_view("Weights"), std::string_view("Knots"), std::string_view("Multiplicities"), std::string_view("Periodic")}; }
inline std::string_view TypeName(NURBS2*) { return "NURBS2"; }
inline auto FieldNames(NURBS3*) { return std::array{std::string_view("Degree"), std::string_view("Poles"), std::string_view("Weights"), std::string_view("Knots"), std::string_view("Multiplicities"), std::string_view("Periodic")}; }
inline std::string_view TypeName(NURBS3*) { return "NURBS3"; }
inline auto FieldNames(Clothoid2*) { return std::array{std::string_view("Placement"), std::string_view("StartCurvature"), std::string_view("EndCurvature"), std::string_view("Length")}; }
inline std::string_view TypeName(Clothoid2*) { return "Clothoid2"; }
inline auto FieldNames(Clothoid3*) { return std::array{std::string_view("Placement"), std::string_view("StartCurvature"), std::string_view("EndCurvature"), std::string_view("Length")}; }
inline std::string_view TypeName(Clothoid3*) { return "Clothoid3"; }
inline auto FieldNames(CurveSegment2*) { return std::array{std::string_view("Curve"), std::string_view("Range")}; }
inline std::string_view TypeName(CurveSegment2*) { return "CurveSegment2"; }
inline auto FieldNames(CurveSegment3*) { return std::array{std::string_view("Curve"), std::string_view("Range")}; }
inline std::string_view TypeName(CurveSegment3*) { return "CurveSegment3"; }
inline auto FieldNames(CompositeCurve2*) { return std::array{std::string_view("Segments"), std::string_view("Closed")}; }
inline std::string_view TypeName(CompositeCurve2*) { return "CompositeCurve2"; }
inline auto FieldNames(CompositeCurve3*) { return std::array{std::string_view("Segments"), std::string_view("Closed")}; }
inline std::string_view TypeName(CompositeCurve3*) { return "CompositeCurve3"; }
inline auto FieldNames(Loop2*) { return std::array{std::string_view("Segments")}; }
inline std::string_view TypeName(Loop2*) { return "Loop2"; }
inline auto FieldNames(Region2*) { return std::array{std::string_view("OuterLoops"), std::string_view("InnerLoops")}; }
inline std::string_view TypeName(Region2*) { return "Region2"; }
inline auto FieldNames(Rect2*) { return std::array{std::string_view("Placement"), std::string_view("Width"), std::string_view("Height")}; }
inline std::string_view TypeName(Rect2*) { return "Rect2"; }
inline auto FieldNames(Box3*) { return std::array{std::string_view("Placement"), std::string_view("Width"), std::string_view("Depth"), std::string_view("Height")}; }
inline std::string_view TypeName(Box3*) { return "Box3"; }
inline auto FieldNames(PlaneSurface3*) { return std::array{std::string_view("Placement")}; }
inline std::string_view TypeName(PlaneSurface3*) { return "PlaneSurface3"; }
inline auto FieldNames(CylindricalSurface3*) { return std::array{std::string_view("Placement"), std::string_view("Radius")}; }
inline std::string_view TypeName(CylindricalSurface3*) { return "CylindricalSurface3"; }
inline auto FieldNames(ConicalSurface3*) { return std::array{std::string_view("Placement"), std::string_view("Radius"), std::string_view("SemiAngle")}; }
inline std::string_view TypeName(ConicalSurface3*) { return "ConicalSurface3"; }
inline auto FieldNames(SphericalSurface3*) { return std::array{std::string_view("Placement"), std::string_view("Radius")}; }
inline std::string_view TypeName(SphericalSurface3*) { return "SphericalSurface3"; }
inline auto FieldNames(ToroidalSurface3*) { return std::array{std::string_view("Placement"), std::string_view("MajorRadius"), std::string_view("MinorRadius")}; }
inline std::string_view TypeName(ToroidalSurface3*) { return "ToroidalSurface3"; }
inline auto FieldNames(BSplineSurface3*) { return std::array{std::string_view("UDegree"), std::string_view("VDegree"), std::string_view("UCount"), std::string_view("VCount"), std::string_view("Poles"), std::string_view("Weights"), std::string_view("UKnots"), std::string_view("VKnots"), std::string_view("UMultiplicities"), std::string_view("VMultiplicities"), std::string_view("UPeriodic"), std::string_view("VPeriodic")}; }
inline std::string_view TypeName(BSplineSurface3*) { return "BSplineSurface3"; }
inline auto FieldNames(RevolutionSurface3*) { return std::array{std::string_view("Basis"), std::string_view("Axis")}; }
inline std::string_view TypeName(RevolutionSurface3*) { return "RevolutionSurface3"; }
inline auto FieldNames(ExtrusionSurface3*) { return std::array{std::string_view("Basis"), std::string_view("Direction")}; }
inline std::string_view TypeName(ExtrusionSurface3*) { return "ExtrusionSurface3"; }
inline auto FieldNames(Triangulation3*) { return std::array{std::string_view("Vertices"), std::string_view("Normals"), std::string_view("TextureCoordinates"), std::string_view("Triangles"), std::string_view("TriangleFaceIds")}; }
inline std::string_view TypeName(Triangulation3*) { return "Triangulation3"; }
inline auto FieldNames(EntityMetadata*) { return std::array{std::string_view("Name"), std::string_view("SourceId"), std::string_view("Tags")}; }
inline std::string_view TypeName(EntityMetadata*) { return "EntityMetadata"; }
inline auto FieldNames(Curve2Record*) { return std::array{std::string_view("Id"), std::string_view("Geometry"), std::string_view("Metadata")}; }
inline std::string_view TypeName(Curve2Record*) { return "Curve2Record"; }
inline auto FieldNames(Curve3Record*) { return std::array{std::string_view("Id"), std::string_view("Geometry"), std::string_view("Metadata")}; }
inline std::string_view TypeName(Curve3Record*) { return "Curve3Record"; }
inline auto FieldNames(Surface3Record*) { return std::array{std::string_view("Id"), std::string_view("Geometry"), std::string_view("Metadata")}; }
inline std::string_view TypeName(Surface3Record*) { return "Surface3Record"; }
inline auto FieldNames(Triangulation3Record*) { return std::array{std::string_view("Id"), std::string_view("Geometry"), std::string_view("Metadata")}; }
inline std::string_view TypeName(Triangulation3Record*) { return "Triangulation3Record"; }
inline auto FieldNames(CTriangleMeshResource*) { return std::array{std::string_view("Mesh"), std::string_view("Metadata")}; }
inline std::string_view TypeName(CTriangleMeshResource*) { return "CTriangleMeshResource"; }
inline auto FieldNames(BRepShapeRef*) { return std::array{std::string_view("Kind"), std::string_view("Id"), std::string_view("Orientation"), std::string_view("Location")}; }
inline std::string_view TypeName(BRepShapeRef*) { return "BRepShapeRef"; }
inline auto FieldNames(BRepVertex*) { return std::array{std::string_view("Id"), std::string_view("Position"), std::string_view("Tolerance"), std::string_view("Metadata")}; }
inline std::string_view TypeName(BRepVertex*) { return "BRepVertex"; }
inline auto FieldNames(BRepEdge*) { return std::array{std::string_view("Id"), std::string_view("Curve3Id"), std::string_view("StartVertexId"), std::string_view("EndVertexId"), std::string_view("Range"), std::string_view("Tolerance"), std::string_view("SameParameter"), std::string_view("SameRange"), std::string_view("Degenerated"), std::string_view("Closed"), std::string_view("Metadata")}; }
inline std::string_view TypeName(BRepEdge*) { return "BRepEdge"; }
inline auto FieldNames(BRepCoedge*) { return std::array{std::string_view("EdgeId"), std::string_view("Curve2Id"), std::string_view("StartVertexId"), std::string_view("EndVertexId"), std::string_view("Range"), std::string_view("Orientation"), std::string_view("PCurveStored")}; }
inline std::string_view TypeName(BRepCoedge*) { return "BRepCoedge"; }
inline auto FieldNames(BRepWire*) { return std::array{std::string_view("Id"), std::string_view("Coedges"), std::string_view("Closed"), std::string_view("Metadata")}; }
inline std::string_view TypeName(BRepWire*) { return "BRepWire"; }
inline auto FieldNames(BRepFace*) { return std::array{std::string_view("Id"), std::string_view("Surface3Id"), std::string_view("Triangulation3Id"), std::string_view("WireIds"), std::string_view("WireOrientations"), std::string_view("Domain"), std::string_view("Orientation"), std::string_view("Tolerance"), std::string_view("NaturalRestriction"), std::string_view("SurfaceTrimmed"), std::string_view("Metadata")}; }
inline std::string_view TypeName(BRepFace*) { return "BRepFace"; }
inline auto FieldNames(BRepShell*) { return std::array{std::string_view("Id"), std::string_view("FaceIds"), std::string_view("FaceOrientations"), std::string_view("Closed"), std::string_view("Metadata")}; }
inline std::string_view TypeName(BRepShell*) { return "BRepShell"; }
inline auto FieldNames(BRepSolid*) { return std::array{std::string_view("Id"), std::string_view("ShellIds"), std::string_view("ShellOrientations"), std::string_view("Metadata")}; }
inline std::string_view TypeName(BRepSolid*) { return "BRepSolid"; }
inline auto FieldNames(BRepCompSolid*) { return std::array{std::string_view("Id"), std::string_view("SolidIds"), std::string_view("Metadata")}; }
inline std::string_view TypeName(BRepCompSolid*) { return "BRepCompSolid"; }
inline auto FieldNames(BRepCompound*) { return std::array{std::string_view("Id"), std::string_view("Children"), std::string_view("Metadata")}; }
inline std::string_view TypeName(BRepCompound*) { return "BRepCompound"; }
inline auto FieldNames(BRepModel*) { return std::array{std::string_view("Curves2"), std::string_view("Curves3"), std::string_view("Surfaces3"), std::string_view("Triangulations3"), std::string_view("Vertices"), std::string_view("Edges"), std::string_view("Wires"), std::string_view("Faces"), std::string_view("Shells"), std::string_view("Solids"), std::string_view("CompSolids"), std::string_view("Compounds"), std::string_view("RootShapes"), std::string_view("RootFaceIds"), std::string_view("RootShellIds"), std::string_view("RootSolidIds"), std::string_view("Metadata")}; }
inline std::string_view TypeName(BRepModel*) { return "BRepModel"; }
}
namespace iCAX::GeometryData::Tube {
inline auto FieldNames(SRegionCurve2*) { return std::array{std::string_view("ID"), std::string_view("Segment")}; }
inline std::string_view TypeName(SRegionCurve2*) { return "SRegionCurve2"; }
inline auto FieldNames(SRegionLoop2*) { return std::array{std::string_view("ID"), std::string_view("Curves")}; }
inline std::string_view TypeName(SRegionLoop2*) { return "SRegionLoop2"; }
inline auto FieldNames(CPlanarRegion2*) { return std::array{std::string_view("FillRule"), std::string_view("Boundaries")}; }
inline std::string_view TypeName(CPlanarRegion2*) { return "CPlanarRegion2"; }
inline auto FieldNames(SBoundarySurfaceRef*) { return std::array{std::string_view("ExtrusionNodeID"), std::string_view("LoopID"), std::string_view("CurveID")}; }
inline std::string_view TypeName(SBoundarySurfaceRef*) { return "SBoundarySurfaceRef"; }
inline auto FieldNames(SScalarParameter*) { return std::array{std::string_view("Value"), std::string_view("Observability"), std::string_view("Minimum"), std::string_view("Maximum"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SScalarParameter*) { return "SScalarParameter"; }
inline auto FieldNames(SMaterialBoundaryReference*) { return std::array{std::string_view("ID"), std::string_view("HostNodeID"), std::string_view("TravelOrdinal"), std::string_view("Transition"), std::string_view("WitnessStation"), std::string_view("Support"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SMaterialBoundaryReference*) { return "SMaterialBoundaryReference"; }
inline auto FieldNames(SSurfaceEvidenceReference*) { return std::array{std::string_view("SourceResourceID"), std::string_view("SourceShapeID"), std::string_view("Role"), std::string_view("Confidence"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SSurfaceEvidenceReference*) { return "SSurfaceEvidenceReference"; }
inline auto FieldNames(SMaterialSpanEndpoint*) { return std::array{std::string_view("Kind"), std::string_view("Station"), std::string_view("Boundary"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SMaterialSpanEndpoint*) { return "SMaterialSpanEndpoint"; }
inline auto FieldNames(SMaterialSpan*) { return std::array{std::string_view("ID"), std::string_view("Start"), std::string_view("End"), std::string_view("RemovalComponentOrdinals"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SMaterialSpan*) { return "SMaterialSpan"; }
inline auto FieldNames(SFeatureOpeningRef*) { return std::array{std::string_view("SourceFeatureNodeID"), std::string_view("OpeningID"), std::string_view("Boundary"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SFeatureOpeningRef*) { return "SFeatureOpeningRef"; }
inline auto FieldNames(SFeatureExtentConstraint*) { return std::array{std::string_view("Rule"), std::string_view("Direction"), std::string_view("HostNodeID"), std::string_view("PrimitiveFirst"), std::string_view("PrimitiveLast"), std::string_view("Depth"), std::string_view("Boundaries"), std::string_view("RecomputeAgainstHost"), std::string_view("Confidence"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SFeatureExtentConstraint*) { return "SFeatureExtentConstraint"; }
inline auto FieldNames(SHostPlacementConstraint*) { return std::array{std::string_view("Rule"), std::string_view("HostNodeID"), std::string_view("Support"), std::string_view("LocalFrame"), std::string_view("SupportU"), std::string_view("SupportV"), std::string_view("Confidence"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SHostPlacementConstraint*) { return "SHostPlacementConstraint"; }
inline auto FieldNames(SFeatureMaterialEffect*) { return std::array{std::string_view("Role"), std::string_view("HostNodeID"), std::string_view("SourceNodeIDs"), std::string_view("Confidence"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SFeatureMaterialEffect*) { return "SFeatureMaterialEffect"; }
inline auto FieldNames(SFeatureRelations*) { return std::array{std::string_view("DependsOnNodeIDs"), std::string_view("Extent"), std::string_view("Placement"), std::string_view("MaterialEffect"), std::string_view("SurfaceEvidence"), std::string_view("MaterialSpans"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SFeatureRelations*) { return "SFeatureRelations"; }
inline auto FieldNames(SSideAtlasDefinition*) { return std::array{std::string_view("ID"), std::string_view("LoopID"), std::string_view("SeamRule"), std::string_view("UPeriod"), std::string_view("VFirst"), std::string_view("VLast")}; }
inline std::string_view TypeName(SSideAtlasDefinition*) { return "SSideAtlasDefinition"; }
inline auto FieldNames(SSectionPrimitive*) { return std::array{std::string_view("ID"), std::string_view("Label"), std::string_view("LoopID"), std::string_view("Role"), std::string_view("Kind"), std::string_view("Center"), std::string_view("RotationRadians"), std::string_view("Width"), std::string_view("Height"), std::string_view("Radius"), std::string_view("Observability"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SSectionPrimitive*) { return "SSectionPrimitive"; }
inline auto FieldNames(SExtrudedRegionNode*) { return std::array{std::string_view("Section"), std::string_view("SectionPrimitives"), std::string_view("Frame"), std::string_view("First"), std::string_view("Last"), std::string_view("SideAtlases")}; }
inline std::string_view TypeName(SExtrudedRegionNode*) { return "SExtrudedRegionNode"; }
inline auto FieldNames(STaperedRegionNode*) { return std::array{std::string_view("Section"), std::string_view("SectionPrimitives"), std::string_view("Frame"), std::string_view("First"), std::string_view("Last"), std::string_view("Reference"), std::string_view("ScaleCenter"), std::string_view("FirstScale"), std::string_view("LastScale")}; }
inline std::string_view TypeName(STaperedRegionNode*) { return "STaperedRegionNode"; }
inline auto FieldNames(SHalfSpaceNode*) { return std::array{std::string_view("Boundary"), std::string_view("KeepNegativeSide")}; }
inline std::string_view TypeName(SHalfSpaceNode*) { return "SHalfSpaceNode"; }
inline auto FieldNames(SBooleanNode*) { return std::array{std::string_view("Operation"), std::string_view("Children")}; }
inline std::string_view TypeName(SBooleanNode*) { return "SBooleanNode"; }
inline auto FieldNames(STransformNode*) { return std::array{std::string_view("Child"), std::string_view("Transform")}; }
inline std::string_view TypeName(STransformNode*) { return "STransformNode"; }
inline auto FieldNames(SConnectedComponentNode*) { return std::array{std::string_view("Child"), std::string_view("Anchor")}; }
inline std::string_view TypeName(SConnectedComponentNode*) { return "SConnectedComponentNode"; }
inline auto FieldNames(SScalarGrid2*) { return std::array{std::string_view("UFirst"), std::string_view("ULast"), std::string_view("VFirst"), std::string_view("VLast"), std::string_view("UCount"), std::string_view("VCount"), std::string_view("Values")}; }
inline std::string_view TypeName(SScalarGrid2*) { return "SScalarGrid2"; }
inline auto FieldNames(SOpeningProfileKey*) { return std::array{std::string_view("ContourParameter"), std::string_view("Profile"), std::string_view("Confidence"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SOpeningProfileKey*) { return "SOpeningProfileKey"; }
inline auto FieldNames(SOpeningProfileField*) { return std::array{std::string_view("ContourFirst"), std::string_view("ContourLast"), std::string_view("Periodic"), std::string_view("Interpolation"), std::string_view("Keys"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SOpeningProfileField*) { return "SOpeningProfileField"; }
inline auto FieldNames(SWrappedVolumeNode*) { return std::array{std::string_view("Support"), std::string_view("UVRegion"), std::string_view("LowerNormalOffset"), std::string_view("UpperNormalOffset"), std::string_view("DepthRule")}; }
inline std::string_view TypeName(SWrappedVolumeNode*) { return "SWrappedVolumeNode"; }
inline auto FieldNames(STopologyBevelSelection*) { return std::array{std::string_view("EdgeID"),std::string_view("ReferenceFaceID"),std::string_view("EdgeName"),std::string_view("FaceName"),std::string_view("Side"),std::string_view("AngleDegrees"),std::string_view("RootFace"),std::string_view("GroupID"),std::string_view("WrappedSourceNodeID"),std::string_view("UVBoundary")}; }
inline std::string_view TypeName(STopologyBevelSelection*) { return "STopologyBevelSelection"; }
inline auto FieldNames(STopologyBevelNode*) { return std::array{std::string_view("Child"),std::string_view("SelectionShapeSignature"),std::string_view("Selections")}; }
inline std::string_view TypeName(STopologyBevelNode*) { return "STopologyBevelNode"; }
inline auto FieldNames(SOpeningProfileSweepNode*) { return std::array{std::string_view("SourceCutNodeID"), std::string_view("TargetOpening"), std::string_view("FrameRule"), std::string_view("ProfileField"), std::string_view("EvaluatedVolumeNodeID"), std::string_view("Observability"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SOpeningProfileSweepNode*) { return "SOpeningProfileSweepNode"; }
inline auto FieldNames(SCompositeSurfacePatch*) { return std::array{std::string_view("SourceShapeID"), std::string_view("Kind"), std::string_view("AdjacentShapeIDs"), std::string_view("Parameters"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SCompositeSurfacePatch*) { return "SCompositeSurfacePatch"; }
inline auto FieldNames(SCompositeVolumeNode*) { return std::array{std::string_view("BRepResourceID"), std::string_view("RootShapeIDs"), std::string_view("AnalyticPatches"), std::string_view("EditCapabilities"), std::string_view("Closed"), std::string_view("Exact"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SCompositeVolumeNode*) { return "SCompositeVolumeNode"; }
inline auto FieldNames(SResidualBRepNode*) { return std::array{std::string_view("BRepResourceID"), std::string_view("RootShapeIDs")}; }
inline std::string_view TypeName(SResidualBRepNode*) { return "SResidualBRepNode"; }
inline auto FieldNames(SInterpretationEvidence*) { return std::array{std::string_view("Code"), std::string_view("Message"), std::string_view("Confidence"), std::string_view("SourceShapeIDs")}; }
inline std::string_view TypeName(SInterpretationEvidence*) { return "SInterpretationEvidence"; }
inline auto FieldNames(SInterpretationEvaluation*) { return std::array{std::string_view("GeometryAccepted"), std::string_view("GeometryAcceptanceTolerance"), std::string_view("RelativeGeometryError"), std::string_view("RelativeOvercut"), std::string_view("RelativeUncovered"), std::string_view("EvidenceCoverage"), std::string_view("ParameterObservability"), std::string_view("ModelComplexity"), std::string_view("FieldComplexity"), std::string_view("PerturbationStability"), std::string_view("EditStability"), std::string_view("RecommendationScore"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SInterpretationEvaluation*) { return "SInterpretationEvaluation"; }
inline auto FieldNames(SInterpretationCandidate*) { return std::array{std::string_view("ID"), std::string_view("Label"), std::string_view("CandidateNodeID"), std::string_view("Optional"), std::string_view("Confidence"), std::string_view("RelativeGeometryError"), std::string_view("Evaluation"), std::string_view("Evidence"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SInterpretationCandidate*) { return "SInterpretationCandidate"; }
inline auto FieldNames(SAlternativeNode*) { return std::array{std::string_view("Candidates"), std::string_view("RecommendedCandidateID"), std::string_view("SelectedCandidateID"), std::string_view("SelectionSource")}; }
inline std::string_view TypeName(SAlternativeNode*) { return "SAlternativeNode"; }
inline auto FieldNames(SRemovalSemantics*) { return std::array{std::string_view("Extent"), std::string_view("BoundaryOpeningPatchCount"), std::string_view("InternalTerminationCount"), std::string_view("Confidence"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SRemovalSemantics*) { return "SRemovalSemantics"; }
inline auto FieldNames(SSolidNode*) { return std::array{std::string_view("ID"), std::string_view("Label"), std::string_view("Data"), std::string_view("Relations"), std::string_view("RemovalSemantics"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SSolidNode*) { return "SSolidNode"; }
inline auto FieldNames(SUVVectorGeometry*) { return std::array{std::string_view("Support"), std::string_view("Paths"), std::string_view("Regions"), std::string_view("UVTransform")}; }
inline std::string_view TypeName(SUVVectorGeometry*) { return "SUVVectorGeometry"; }
inline auto FieldNames(SUVImage*) { return std::array{std::string_view("Support"), std::string_view("ImageResourceID"), std::string_view("UVTransform"), std::string_view("PhysicalWidth"), std::string_view("PhysicalHeight"), std::string_view("AlphaMode"), std::string_view("SamplingMode")}; }
inline std::string_view TypeName(SUVImage*) { return "SUVImage"; }
inline auto FieldNames(SUVFeature*) { return std::array{std::string_view("ID"), std::string_view("Label"), std::string_view("Data"), std::string_view("Metadata")}; }
inline std::string_view TypeName(SUVFeature*) { return "SUVFeature"; }
inline auto FieldNames(SRecognitionDiagnostic*) { return std::array{std::string_view("Code"), std::string_view("Message"), std::string_view("Confidence"), std::string_view("SourceFaceIDs")}; }
inline std::string_view TypeName(SRecognitionDiagnostic*) { return "SRecognitionDiagnostic"; }
inline auto FieldNames(CTubeNeutralGeometry*) { return std::array{std::string_view("Version"), std::string_view("BaseNodeID"), std::string_view("RootNodeID"), std::string_view("SolidNodes"), std::string_view("UVFeatures"), std::string_view("RecognitionStatus"), std::string_view("Confidence"), std::string_view("RelativeVolumeError"), std::string_view("RelativeUnparameterizedVolume"), std::string_view("RelativeOpaqueResidualVolume"), std::string_view("Diagnostics"), std::string_view("Metadata")}; }
inline std::string_view TypeName(CTubeNeutralGeometry*) { return "CTubeNeutralGeometry"; }
inline auto FieldNames(CTubeBoundaryGeometry*) { return std::array{std::string_view("Section"), std::string_view("MachiningFaces")}; }
inline std::string_view TypeName(CTubeBoundaryGeometry*) { return "CTubeBoundaryGeometry"; }
inline auto FieldNames(CTubeCadDocument*) { return std::array{std::string_view("Revision"), std::string_view("Unit"), std::string_view("Geometry"), std::string_view("Placement"), std::string_view("Construction"), std::string_view("Bodies"), std::string_view("Metadata")}; }
inline std::string_view TypeName(CTubeCadDocument*) { return "CTubeCadDocument"; }
}
