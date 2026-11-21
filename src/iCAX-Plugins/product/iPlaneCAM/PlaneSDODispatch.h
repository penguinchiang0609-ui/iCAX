#pragma once
#include "PlaneDomainInternal.h"
#include "Data/VariantSerializer.h"
#include "ProductContext/IProductContext.h"
#include "SDO/SDO.h"
#include "SDO/SDORegistrationCatalog.h"

namespace iCAX::PlaneCAM::detail {
// Transport decoding is shared. Every registered method binds directly to
// the business object's member; no cross-business string dispatcher is used.
template<class Logic, Map(Logic::*Method)(const Map&)>
iCAX::Interaction::CInvocationResult Dispatch(
    const iCAX::Interaction::CInvocation& request,
    const iCAX::Application::IApplicationContext&,
    iCAX::Product::IProductContext* product,
    iCAX::Project::IProjectContext*, Scene* scene) {
  if (!scene || !product || product->GetProductID() != "icax.iplane-cam")
    throw std::invalid_argument("PLANE_SCENE_CHANNEL_REQUIRED");
  Map payload;
  if (!request.Payload.empty()) payload = iCAX::Data::VariantSerializer::Deserialize(
    Text(request.Payload.begin(), request.Payload.end())).To<Map>();
  Logic logic(*scene);
  const auto bytes = iCAX::Data::VariantSerializer::Serialize((logic.*Method)(payload));
  iCAX::Interaction::CInvocationResult result;
  result.nCallID = request.nCallID; result.Method = request.Method;
  result.Payload.assign(bytes.begin(), bytes.end());
  return result;
}
} // namespace iCAX::PlaneCAM::detail
