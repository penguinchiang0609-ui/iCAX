#pragma once
#include "Database/ComponentHelper.h"
#include <memory>

namespace iCAX::PlaneCAM {
using Map = iCAX::Data::ObjectMap;
using Array = iCAX::Data::VariantArray;
using Text = std::string;
using UInt = unsigned long long;
using ID = iCAX::Data::uuid;
template <class T> iCAX::Data::PropertyValue Value(const T &v) { return v; }
template <class T> T From(const iCAX::Data::PropertyValue &v) {
  return v.To<T>();
}
template <class T> bool Equal(const T &a, const T &b) { return a == b; }
#define PLANE_FIELD(C, T, N, D)                                                \
  DECLARED_ICAX_FIELD(C, T, N, D, Equal<T>, Value<T>, From<T>)
struct SceneRuntime;
struct EditTaskRuntime;
class CPlaneSceneComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneSceneComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneSceneComponent)
public:
  std::shared_ptr<SceneRuntime> Runtime;
  std::shared_ptr<EditTaskRuntime> EditRuntime;
};
class CPlaneProjectComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneProjectComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneProjectComponent)
  PLANE_FIELD(CPlaneProjectComponent, UInt, Revision, 1ull)
  PLANE_FIELD(CPlaneProjectComponent, UInt, NestingRevision, 1ull)
  PLANE_FIELD(CPlaneProjectComponent, ID, DefaultLayerID, ID())
  PLANE_FIELD(CPlaneProjectComponent, UInt, LayerCatalogVersion, 0ull)
  PLANE_FIELD(CPlaneProjectComponent, double, PartGap, 0.)
  PLANE_FIELD(CPlaneProjectComponent, double, StockMargin, 0.)
  PLANE_FIELD(CPlaneProjectComponent, double, RotationStepDeg, 90.)
  PLANE_FIELD(CPlaneProjectComponent, double, TimeLimitSeconds, 1.)
  PLANE_FIELD(CPlaneProjectComponent, bool, AllowMirror, false)
};
class CPlaneObjectComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneObjectComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneObjectComponent)
  PLANE_FIELD(CPlaneObjectComponent, Text, Kind, Text())
  PLANE_FIELD(CPlaneObjectComponent, Text, Name, Text())
  PLANE_FIELD(CPlaneObjectComponent, Text, Note, Text())
  PLANE_FIELD(CPlaneObjectComponent, Text, SourceName, Text())
  PLANE_FIELD(CPlaneObjectComponent, UInt, Quantity, 1ull)
  PLANE_FIELD(CPlaneObjectComponent, UInt, GeometryVersion, 1ull)
  PLANE_FIELD(CPlaneObjectComponent, ID, LayerID, ID())
  PLANE_FIELD(CPlaneObjectComponent, ID, SketchID, ID())
  PLANE_FIELD(CPlaneObjectComponent, Text, Units, Text("mm"))
  PLANE_FIELD(CPlaneObjectComponent, Text, SourceUnits, Text("mm"))
  PLANE_FIELD(CPlaneObjectComponent, Text, UnitSource, Text("Native"))
  PLANE_FIELD(CPlaneObjectComponent, double, ApproximationBoundMm, 0.)
  PLANE_FIELD(CPlaneObjectComponent, double, Width, 0.)
  PLANE_FIELD(CPlaneObjectComponent, double, Height, 0.)
  PLANE_FIELD(CPlaneObjectComponent, UInt, HoleCount, 0ull)
  PLANE_FIELD(CPlaneObjectComponent, Map, SourceRef, Map())
  PLANE_FIELD(CPlaneObjectComponent, Map, GeometryRef, Map())
  PLANE_FIELD(CPlaneObjectComponent, Map, PreviewRef, Map())
};
class CPlaneArtworkComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneArtworkComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneArtworkComponent)
  PLANE_FIELD(CPlaneArtworkComponent, Map, BitmapRef, Map())
  PLANE_FIELD(CPlaneArtworkComponent, Map, TextureRef, Map())
  PLANE_FIELD(CPlaneArtworkComponent, Map, MaterialRef, Map())
  PLANE_FIELD(CPlaneArtworkComponent, Text, MaterialColor, Text())
  PLANE_FIELD(CPlaneArtworkComponent, UInt, PixelWidth, 0ull)
  PLANE_FIELD(CPlaneArtworkComponent, UInt, PixelHeight, 0ull)
  PLANE_FIELD(CPlaneArtworkComponent, double, BaseWidthMm, 0.)
  PLANE_FIELD(CPlaneArtworkComponent, double, BaseHeightMm, 0.)
  PLANE_FIELD(CPlaneArtworkComponent, double, X, 0.)
  PLANE_FIELD(CPlaneArtworkComponent, double, Y, 0.)
  PLANE_FIELD(CPlaneArtworkComponent, double, AngleDeg, 0.)
  PLANE_FIELD(CPlaneArtworkComponent, double, ScaleX, 1.)
  PLANE_FIELD(CPlaneArtworkComponent, double, ScaleY, 1.)
};
class CPlaneTextComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneTextComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneTextComponent)
  PLANE_FIELD(CPlaneTextComponent, Text, TextValue, Text())
  PLANE_FIELD(CPlaneTextComponent, Text, FontFamily, Text("Microsoft YaHei"))
  PLANE_FIELD(CPlaneTextComponent, double, FontSizeMm, 10.)
};
class CPlaneImageComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneImageComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneImageComponent)
  PLANE_FIELD(CPlaneImageComponent, Text, SourceName, Text())
};
class CPlaneMachiningSourceComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneMachiningSourceComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneMachiningSourceComponent)
  PLANE_FIELD(CPlaneMachiningSourceComponent, Text, SourceKind, Text())
  PLANE_FIELD(CPlaneMachiningSourceComponent, ID, SourceObjectID, ID())
  PLANE_FIELD(CPlaneMachiningSourceComponent, ID, PartID, ID())
  PLANE_FIELD(CPlaneMachiningSourceComponent, ID, ResultID, ID())
  PLANE_FIELD(CPlaneMachiningSourceComponent, ID, ResultBoardID, ID())
  PLANE_FIELD(CPlaneMachiningSourceComponent, ID, PlacementID, ID())
  PLANE_FIELD(CPlaneMachiningSourceComponent, UInt, InstanceIndex, 0ull)
  PLANE_FIELD(CPlaneMachiningSourceComponent, Map, SourceGeometryRef, Map())
  PLANE_FIELD(CPlaneMachiningSourceComponent, UInt, SourceGeometryVersion, 0ull)
  PLANE_FIELD(CPlaneMachiningSourceComponent, double, SourceX, 0.)
  PLANE_FIELD(CPlaneMachiningSourceComponent, double, SourceY, 0.)
  PLANE_FIELD(CPlaneMachiningSourceComponent, double, SourceAngleDeg, 0.)
  PLANE_FIELD(CPlaneMachiningSourceComponent, bool, SourceMirror, false)
  PLANE_FIELD(CPlaneMachiningSourceComponent, bool, ReferenceOnly, false)
};
class CPlaneResultComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneResultComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneResultComponent)
  PLANE_FIELD(CPlaneResultComponent, Text, Name, Text())
  PLANE_FIELD(CPlaneResultComponent, Text, Note, Text())
  PLANE_FIELD(CPlaneResultComponent, UInt, InputRevision, 0ull)
  PLANE_FIELD(CPlaneResultComponent, ID, TaskID, ID())
  PLANE_FIELD(CPlaneResultComponent, Text, Status, Text())
  PLANE_FIELD(CPlaneResultComponent, UInt, RequiredCount, 0ull)
  PLANE_FIELD(CPlaneResultComponent, UInt, PlacedCount, 0ull)
  PLANE_FIELD(CPlaneResultComponent, UInt, UnplacedCount, 0ull)
  PLANE_FIELD(CPlaneResultComponent, UInt, UsedStockCount, 0ull)
  PLANE_FIELD(CPlaneResultComponent, double, StockNetArea, 0.)
  PLANE_FIELD(CPlaneResultComponent, double, PlacedMaterialArea, 0.)
  PLANE_FIELD(CPlaneResultComponent, double, Utilization, 0.)
  PLANE_FIELD(CPlaneResultComponent, double, ElapsedMs, 0.)
  PLANE_FIELD(CPlaneResultComponent, bool, Valid, false)
  PLANE_FIELD(CPlaneResultComponent, bool, DemandConserved, false)
  PLANE_FIELD(CPlaneResultComponent, bool, StockConserved, false)
  PLANE_FIELD(CPlaneResultComponent, bool, OrientationsValid, false)
  PLANE_FIELD(CPlaneResultComponent, Text, ValidationScope,
              Text("PolygonMaterial"))
  PLANE_FIELD(CPlaneResultComponent, Text, ProviderVersion, Text())
  PLANE_FIELD(CPlaneResultComponent, Text, StopReason, Text())
  PLANE_FIELD(CPlaneResultComponent, Array, Diagnostics, Array())
  PLANE_FIELD(CPlaneResultComponent, double, PartGap, 0.)
  PLANE_FIELD(CPlaneResultComponent, double, StockMargin, 0.)
  PLANE_FIELD(CPlaneResultComponent, double, RotationStepDeg, 90.)
  PLANE_FIELD(CPlaneResultComponent, double, TimeLimitSeconds, 1.)
  PLANE_FIELD(CPlaneResultComponent, bool, AllowMirror, false)
  PLANE_FIELD(CPlaneResultComponent, double, CalculationPartGap, 0.)
  PLANE_FIELD(CPlaneResultComponent, double, CalculationStockMargin, 0.)
};
class CPlaneResultBoardComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneResultBoardComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneResultBoardComponent)
  PLANE_FIELD(CPlaneResultBoardComponent, ID, ResultID, ID())
  PLANE_FIELD(CPlaneResultBoardComponent, ID, StockID, ID())
  PLANE_FIELD(CPlaneResultBoardComponent, Text, PhysicalStockID, Text())
  PLANE_FIELD(CPlaneResultBoardComponent, UInt, PhysicalIndex, 0ull)
  PLANE_FIELD(CPlaneResultBoardComponent, UInt, Sequence, 0ull)
  PLANE_FIELD(CPlaneResultBoardComponent, UInt, PlacedCount, 0ull)
  PLANE_FIELD(CPlaneResultBoardComponent, double, StockNetArea, 0.)
  PLANE_FIELD(CPlaneResultBoardComponent, double, PlacedMaterialArea, 0.)
  PLANE_FIELD(CPlaneResultBoardComponent, double, Utilization, 0.)
};
class CPlanePlacementComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlanePlacementComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlanePlacementComponent)
  PLANE_FIELD(CPlanePlacementComponent, ID, ResultID, ID())
  PLANE_FIELD(CPlanePlacementComponent, ID, ResultBoardID, ID())
  PLANE_FIELD(CPlanePlacementComponent, ID, PartID, ID())
  PLANE_FIELD(CPlanePlacementComponent, UInt, InstanceIndex, 0ull)
  PLANE_FIELD(CPlanePlacementComponent, double, X, 0.)
  PLANE_FIELD(CPlanePlacementComponent, double, Y, 0.)
  PLANE_FIELD(CPlanePlacementComponent, double, AngleDeg, 0.)
  PLANE_FIELD(CPlanePlacementComponent, bool, Mirror, false)
};
class CPlaneUnplacedComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneUnplacedComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneUnplacedComponent)
  PLANE_FIELD(CPlaneUnplacedComponent, ID, ResultID, ID())
  PLANE_FIELD(CPlaneUnplacedComponent, ID, PartID, ID())
  PLANE_FIELD(CPlaneUnplacedComponent, UInt, InstanceIndex, 0ull)
  PLANE_FIELD(CPlaneUnplacedComponent, Text, Reason, Text())
};
class CPlaneTaskComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneTaskComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneTaskComponent)
  PLANE_FIELD(CPlaneTaskComponent, UInt, InputRevision, 0ull)
  PLANE_FIELD(CPlaneTaskComponent, Array, InputObjectIDs, Array())
  PLANE_FIELD(CPlaneTaskComponent, Text, Status, Text("Running"))
  PLANE_FIELD(CPlaneTaskComponent, Text, Error, Text())
  PLANE_FIELD(CPlaneTaskComponent, ID, ResultID, ID())
  PLANE_FIELD(CPlaneTaskComponent, double, PartGap, 0.)
  PLANE_FIELD(CPlaneTaskComponent, double, StockMargin, 0.)
  PLANE_FIELD(CPlaneTaskComponent, double, RotationStepDeg, 90.)
  PLANE_FIELD(CPlaneTaskComponent, double, TimeLimitSeconds, 1.)
  PLANE_FIELD(CPlaneTaskComponent, bool, AllowMirror, false)
  PLANE_FIELD(CPlaneTaskComponent, double, CalculationPartGap, 0.)
  PLANE_FIELD(CPlaneTaskComponent, double, CalculationStockMargin, 0.)
};
class CPlaneLayerComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneLayerComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneLayerComponent)
  PLANE_FIELD(CPlaneLayerComponent, Text, Name, Text("默认图层"))
  PLANE_FIELD(CPlaneLayerComponent, Text, Color, Text("#22C55E"))
  PLANE_FIELD(CPlaneLayerComponent, UInt, Order, 0ull)
  PLANE_FIELD(CPlaneLayerComponent, Map, MaterialRef, Map())
  PLANE_FIELD(CPlaneLayerComponent, bool, Visible, true)
  PLANE_FIELD(CPlaneLayerComponent, bool, Locked, false)
};
class CPlaneSketchComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneSketchComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneSketchComponent)
  PLANE_FIELD(CPlaneSketchComponent, ID, OwnerID, ID())
  PLANE_FIELD(CPlaneSketchComponent, Text, Status, Text("Ready"))
  PLANE_FIELD(CPlaneSketchComponent, Map, BaseGeometryRef, Map())
};
class CPlaneSketchCurveComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneSketchCurveComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneSketchCurveComponent)
  PLANE_FIELD(CPlaneSketchCurveComponent, ID, SketchID, ID())
  PLANE_FIELD(CPlaneSketchCurveComponent, bool, Deleted, false)
};
class CPlaneSketchBackupComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneSketchBackupComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneSketchBackupComponent)
  PLANE_FIELD(CPlaneSketchBackupComponent, ID, SketchID, ID())
  PLANE_FIELD(CPlaneSketchBackupComponent, ID, CurveID, ID())
  PLANE_FIELD(CPlaneSketchBackupComponent, Text, Name, Text())
  PLANE_FIELD(CPlaneSketchBackupComponent, Text, Note, Text())
  PLANE_FIELD(CPlaneSketchBackupComponent, ID, LayerID, ID())
  PLANE_FIELD(CPlaneSketchBackupComponent, Map, GeometryRef, Map())
  PLANE_FIELD(CPlaneSketchBackupComponent, Map, PreviewRef, Map())
  PLANE_FIELD(CPlaneSketchBackupComponent, UInt, GeometryVersion, 1ull)
  PLANE_FIELD(CPlaneSketchBackupComponent, double, Width, 0.)
  PLANE_FIELD(CPlaneSketchBackupComponent, double, Height, 0.)
  PLANE_FIELD(CPlaneSketchBackupComponent, double, ApproximationBoundMm, 0.)
};
class CPlaneEditTaskComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneEditTaskComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneEditTaskComponent)
  PLANE_FIELD(CPlaneEditTaskComponent, Text, Kind, Text())
  PLANE_FIELD(CPlaneEditTaskComponent, ID, TargetID, ID())
  PLANE_FIELD(CPlaneEditTaskComponent, ID, OwnerID, ID())
  PLANE_FIELD(CPlaneEditTaskComponent, ID, SketchID, ID())
  PLANE_FIELD(CPlaneEditTaskComponent, Map, OwnerGeometryRef, Map())
  PLANE_FIELD(CPlaneEditTaskComponent, Text, Status, Text("Running"))
  PLANE_FIELD(CPlaneEditTaskComponent, Text, Error, Text())
  PLANE_FIELD(CPlaneEditTaskComponent, ID, ResultID, ID())
  PLANE_FIELD(CPlaneEditTaskComponent, Array, ResultObjectIDs, Array())
};
class CPlaneTaskInputComponent final : public iCAX::Database::CComponentBase {
  DECLARE_ICAX_COMPONENT(CPlaneTaskInputComponent, CComponentBase)
  DECLARE_ICAX_COMPONENT_CREATOR(CPlaneTaskInputComponent)
  PLANE_FIELD(CPlaneTaskInputComponent, ID, TaskID, ID())
  PLANE_FIELD(CPlaneTaskInputComponent, ID, ObjectID, ID())
  PLANE_FIELD(CPlaneTaskInputComponent, Text, Kind, Text())
  PLANE_FIELD(CPlaneTaskInputComponent, UInt, Quantity, 1ull)
  PLANE_FIELD(CPlaneTaskInputComponent, UInt, GeometryVersion, 1ull)
  PLANE_FIELD(CPlaneTaskInputComponent, double, ApproximationBoundMm, 0.)
  PLANE_FIELD(CPlaneTaskInputComponent, Map, GeometryRef, Map())
};
#define PLANE_ROLE(C)                                                          \
  class C final : public iCAX::Database::CComponentBase {                      \
    DECLARE_ICAX_COMPONENT(C, CComponentBase)                                  \
    DECLARE_ICAX_COMPONENT_CREATOR(C)                                          \
  };
PLANE_ROLE(CPlaneDrawingComponent)
PLANE_ROLE(CPlanePartComponent)
PLANE_ROLE(CPlaneStockComponent)
#undef PLANE_ROLE
#undef PLANE_FIELD
} // namespace iCAX::PlaneCAM
