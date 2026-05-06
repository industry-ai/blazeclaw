#include "pch.h"

#include "../src/app/CMgrMessage.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace {
	struct DummyPayload
	{
		int value = 0;
	};

	class FakeEngine final : public CMgrMessage::IEngine
	{
	public:
		bool windowOk = true;
		bool postOk = true;

		bool IsWindow(HWND) const override
		{
			return windowOk;
		}

		bool PostMessage(HWND, UINT, WPARAM, LPARAM) const override
		{
			return postOk;
		}
	};

	bool g_payloadDeleted = false;

	void DeleteDummyPayload(void* raw)
	{
		delete static_cast<DummyPayload*>(raw);
		g_payloadDeleted = true;
	}
}

TEST_CASE("CMgrMessage reports failure for invalid HWND post", "[cmgrmessage][negative]")
{
	CMgrMessage& mgr = CMgrMessage::Instance();
	mgr.Shutdown();
	mgr.ResetDiagnostics();

	const bool posted = mgr.PostToHwnd(
		reinterpret_cast<HWND>(0x1),
		WM_APP + 0x7A,
		0,
		0);
	REQUIRE_FALSE(posted);

	const auto diag = mgr.GetDiagnostics();
	REQUIRE(diag.postAttempts >= 1);
	REQUIRE(diag.postFailures >= 1);
}

TEST_CASE("CMgrMessage owned tool-status line rejects null payload", "[cmgrmessage][negative]")
{
	CMgrMessage& mgr = CMgrMessage::Instance();
	mgr.Shutdown();
	mgr.ResetDiagnostics();

	const bool posted = mgr.PostOwnedToolStatusLine(
		WM_USER + 0x155,
		nullptr);
	REQUIRE_FALSE(posted);

	const auto diag = mgr.GetDiagnostics();
	REQUIRE(diag.postAttempts >= 1);
	REQUIRE(diag.postFailures >= 1);
}

TEST_CASE("CMgrMessage owned payload post rejects invalid target and calls deleter", "[cmgrmessage][negative]")
{
	CMgrMessage& mgr = CMgrMessage::Instance();
	mgr.Shutdown();
	mgr.ResetDiagnostics();

	g_payloadDeleted = false;
	auto* payload = new DummyPayload{ 7 };
	const bool posted = mgr.PostOwnedPayloadToHwnd(
		reinterpret_cast<HWND>(0x2),
		WM_APP + 0x7B,
		payload,
		true,
		&DeleteDummyPayload);
	REQUIRE_FALSE(posted);
	REQUIRE(g_payloadDeleted);

	const auto diag = mgr.GetDiagnostics();
	REQUIRE(diag.postAttempts >= 1);
	REQUIRE(diag.postFailures >= 1);
	REQUIRE(diag.payloadCleanup >= 1);
}

TEST_CASE("CMgrMessage supports injectable engine and channel routing diagnostics", "[cmgrmessage][routing]")
{
	CMgrMessage& mgr = CMgrMessage::Instance();
	mgr.Shutdown();
	mgr.ResetDiagnostics();
	mgr.ClearWebChannelHandlers();

	auto engine = std::make_shared<FakeEngine>();
	engine->windowOk = true;
	engine->postOk = true;
	mgr.SetEngineForTesting(engine);
	mgr.Initialize(reinterpret_cast<HWND>(0x101));

	const bool posted = mgr.PostToMainFrame(WM_APP + 0x7C, 0, 0);
	REQUIRE(posted);

	bool channelExecuted = false;
	mgr.RegisterWebChannelHandler(
		"unit.test.channel",
		[&channelExecuted](const std::string& raw)
		{
			channelExecuted = (raw == "{\"ok\":true}");
			return true;
		});
	REQUIRE(mgr.DispatchWebChannelMessage("unit.test.channel", "{\"ok\":true}"));
	REQUIRE(channelExecuted);
	REQUIRE_FALSE(mgr.DispatchWebChannelMessage("unit.test.missing", "{}"));

	const auto diag = mgr.GetDiagnostics();
	REQUIRE(diag.posted >= 1);
	REQUIRE(diag.handled >= 1);
	REQUIRE(diag.dropped >= 1);

	mgr.ClearWebChannelHandlers();
	mgr.ResetEngineForTesting();
	mgr.Shutdown();
}
