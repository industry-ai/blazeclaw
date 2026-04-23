#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace blazeclaw::gateway {

class GatewayMethodDispatcher;

/// S4.2: static channel-handler RPC names registered by `GatewayHost.Handlers.Channels.cpp`.
[[nodiscard]] const std::array<std::string_view, 35>& GatewayChannelHandlerSurfaceMethodNames() noexcept;

[[nodiscard]] bool GatewayChannelHandlerSurfaceMethodsAreRegistered(
	const GatewayMethodDispatcher& dispatcher);

/// S4.2: deduped union of optional per-extension `gatewayRpcMethods` (from manifests) must be registered.
[[nodiscard]] bool GatewayPluginRpcSurfaceMethodsAreRegistered(
	const GatewayMethodDispatcher& dispatcher,
	const std::vector<std::string>& pluginRpcMethodUnion);

/// S4.2: generated manifest catalog + channel surface + plugin RPC union must be present on the dispatcher.
[[nodiscard]] bool GatewayRuntimeMethodSurfaceInvariantsHold(
	const GatewayMethodDispatcher& dispatcher,
	const std::vector<std::string>& pluginRpcMethodUnion,
	std::string& violationOut);

} // namespace blazeclaw::gateway
