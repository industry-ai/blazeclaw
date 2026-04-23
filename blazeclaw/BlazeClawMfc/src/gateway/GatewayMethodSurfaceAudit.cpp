#include "pch.h"
#include "GatewayMethodSurfaceAudit.h"
#include "GatewayMethodDispatcher.h"
#include "generated/GatewayHandlerCatalog.Generated.h"

#include <unordered_set>

namespace blazeclaw::gateway {

	namespace {

		constexpr std::array<std::string_view, 35> kChannelHandlerMethods = {
			"gateway.channels.adapters.list",
			"gateway.channels.accounts.reset",
			"gateway.channels.accounts.restore",
			"gateway.channels.route.reset",
			"gateway.channels.accounts.count",
			"gateway.channels.accounts.clear",
			"gateway.channels.routes.reset",
			"gateway.channels.routes.count",
			"gateway.channels.route.restore",
			"gateway.channels.route.patch",
			"gateway.channels.route.get",
			"gateway.channels.routes.restore",
			"gateway.channels.routes.clear",
			"gateway.channels.accounts.delete",
			"gateway.channels.accounts.create",
			"gateway.channels.accounts.get",
			"gateway.channels.accounts.update",
			"gateway.channels.accounts.exists",
			"gateway.channels.accounts.deactivate",
			"gateway.channels.accounts.activate",
			"gateway.channels.route.exists",
			"gateway.channels.route.delete",
			"gateway.channels.route.set",
			"gateway.channels.logout",
			"channels.logout",
			"web.login.start",
			"web.login.wait",
			"gateway.channels.accounts",
			"gateway.channels.status.get",
			"gateway.channels.status.exists",
			"gateway.channels.status.count",
			"gateway.channels.route.resolve",
			"gateway.channels.routes",
			"gateway.channels.status",
			"channels.status",
		};

	} // namespace

	const std::array<std::string_view, 35>& GatewayChannelHandlerSurfaceMethodNames() noexcept {
		return kChannelHandlerMethods;
	}

	bool GatewayChannelHandlerSurfaceMethodsAreRegistered(
		const GatewayMethodDispatcher& dispatcher) {
		const auto names = dispatcher.RegisteredMethods();
		const std::unordered_set<std::string> have(names.begin(), names.end());
		for (const std::string_view method : kChannelHandlerMethods) {
			if (have.find(std::string(method)) == have.end()) {
				return false;
			}
		}
		return true;
	}

	bool GatewayPluginRpcSurfaceMethodsAreRegistered(
		const GatewayMethodDispatcher& dispatcher,
		const std::vector<std::string>& pluginRpcMethodUnion) {
		if (pluginRpcMethodUnion.empty()) {
			return true;
		}

		const auto names = dispatcher.RegisteredMethods();
		const std::unordered_set<std::string> have(names.begin(), names.end());
		for (const std::string& method : pluginRpcMethodUnion) {
			if (method.empty()) {
				continue;
			}
			if (have.find(method) == have.end()) {
				return false;
			}
		}

		return true;
	}

	bool GatewayRuntimeMethodSurfaceInvariantsHold(
		const GatewayMethodDispatcher& dispatcher,
		const std::vector<std::string>& pluginRpcMethodUnion,
		std::string& violationOut) {
		if (!GatewayGeneratedHandlerCatalogMethodsAreRegistered(dispatcher)) {
			violationOut = "generated_handler_catalog_subset_not_registered";
			return false;
		}
		if (!GatewayChannelHandlerSurfaceMethodsAreRegistered(dispatcher)) {
			violationOut = "channel_handler_surface_not_registered";
			return false;
		}
		if (!GatewayPluginRpcSurfaceMethodsAreRegistered(dispatcher, pluginRpcMethodUnion)) {
			violationOut = "plugin_rpc_surface_not_registered";
			return false;
		}
		violationOut.clear();
		return true;
	}

} // namespace blazeclaw::gateway
