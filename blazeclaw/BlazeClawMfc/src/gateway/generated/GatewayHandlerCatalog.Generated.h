#pragma once

namespace blazeclaw::gateway {

    inline constexpr int kGatewayHandlerCatalogVersion = 1;

    class GatewayMethodDispatcher;
    [[nodiscard]] bool GatewayGeneratedHandlerCatalogMethodsAreRegistered(
        const GatewayMethodDispatcher& dispatcher);

} // namespace blazeclaw::gateway
