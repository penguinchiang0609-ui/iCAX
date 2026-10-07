#pragma once
#include "LicenseRuntime.h"
#include "ProductContext/IProductContext.h"
#include "ProjectContext/ISceneContext.h"
#include "ProjectContext/IProjectContext.h"
#include "Resources/ResourceLibrary.h"
#include "SDO/SDO.h"

namespace tube::license {
template<std::uint32_t Site, Feature... Alternatives>
iCAX::Interaction::CSDO::MethodFunc ProtectAnyMethod(iCAX::Interaction::CSDO::MethodFunc handler) {
    return [handler = std::move(handler)](const auto& request, const auto& application,
        auto* product, auto* project, auto* scene) {
        EnforceAny<Site, Alternatives...>();
        return handler(request, application, product, project, scene);
    };
}
template<std::uint32_t Site, Feature Required>
iCAX::Interaction::CSDO::MethodFunc ProtectMethod(iCAX::Interaction::CSDO::MethodFunc handler) {
    return [handler = std::move(handler)](const auto& request, const auto& application,
        auto* product, auto* project, auto* scene) {
        Enforce<Site, Required>();
        return handler(request, application, product, project, scene);
    };
}

// Shared CAM SDOs serve multiple products. Authorization ownership comes from
// the host product and its bound scene resource scope, never request payloads.
template<std::uint32_t Site, Feature Required>
iCAX::Interaction::CSDO::MethodFunc ProtectProductMethod(iCAX::Interaction::CSDO::MethodFunc handler) {
    return [handler = std::move(handler)](const auto& request, const auto& application,
        auto* product, auto* project, auto* scene) {
        std::string productId = product ? product->GetProductID() : std::string{};
        if (product) Require(product->GetDefinition().ProductID == productId,
            "CAM product declaration authorization scope mismatch");
        if (scene && scene->Resources().HasScope()) {
            const auto& scope = scene->Resources().GetScope();
            Require(scope.Scope == iCAX::Resource::EResourceScope::Scene
                && scope.SceneID == scene->GetSceneID(), "CAM scene authorization scope mismatch");
            Require(productId.empty() || productId == scope.ProductID, "CAM product authorization scope mismatch");
            productId = scope.ProductID;
            Require(!project || scope.ProjectID == project->GetProjectID(), "CAM project authorization scope mismatch");
        }
        Require(!productId.empty(), "CAM authorization requires a host product context");
        if (productId == Product) Enforce<Site, Required>();
        return handler(request, application, product, project, scene);
    };
}
}
