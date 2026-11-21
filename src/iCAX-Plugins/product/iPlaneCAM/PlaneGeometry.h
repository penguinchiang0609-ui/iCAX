#pragma once
#include "GeometryServices/PlaneGeometryService.h"
#include "OpenCascadeResourceImport/CadDocumentImportService.h"
#include "PlaneComponents.h"
#include "ProjectContext/ISceneContext.h"
#include "Services/ServiceProvider.h"
namespace iCAX::PlaneCAM {
using iCAX::GeometryServices::PlaneGeometryData::Number;
using iCAX::GeometryServices::PlaneGeometryData::String;
using iCAX::GeometryServices::PlaneGeometryData::ToRing;
inline auto GeometryService(iCAX::Project::ISceneContext &scene) {
  return scene.Services().Resolve<iCAX::GeometryServices::IPlaneGeometryService>();
}
} // namespace iCAX::PlaneCAM
