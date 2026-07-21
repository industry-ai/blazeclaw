#include "pch.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <catch2/catch_all.hpp>

#include "core/ThreadPoolRuntimeService.h"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using blazeclaw::core::ThreadPoolRuntimeService;

TEST_CASE(
	"Multi-active thread-pool stress reports saturation under bounded capacity",
	"[gateway][multi-active][thread-pool][stress][saturation]")
{
	ThreadPoolRuntimeService service;
	ThreadPoolRuntimeService::Config config;
	config.minThreads = 1;
	config.maxThreads = 1;
	config.queueCapacity = 1;
	config.dequeueTimeoutMs = 2;
	service.Configure(config);

	std::atomic<bool> releaseFirst = false;
	std::atomic<std::size_t> started = 0;

	auto blockingTask = [&releaseFirst, &started]() {
		return [&releaseFirst, &started]() {
			started.fetch_add(1);
			while (!releaseFirst.load()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
			blazeclaw::gateway::GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "ok";
			return result;
		};
	};

	auto firstFuture = std::async(
		std::launch::async,
		[&service, &blockingTask]() {
			ThreadPoolRuntimeService::ExecuteRequest request;
			request.runId = "run-a";
			request.sessionKey = "main";
			request.responderRunId = "run-a.r0";
			request.timeoutMs = 4000;
			request.task = blockingTask();
			return service.EnqueueAndExecute(request);
		});

	while (started.load() == 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	auto secondFuture = std::async(
		std::launch::async,
		[&service, &blockingTask]() {
			ThreadPoolRuntimeService::ExecuteRequest request;
			request.runId = "run-b";
			request.sessionKey = "main";
			request.responderRunId = "run-b.r0";
			request.timeoutMs = 4000;
			request.task = blockingTask();
			return service.EnqueueAndExecute(request);
		});

	std::this_thread::sleep_for(std::chrono::milliseconds(25));

	ThreadPoolRuntimeService::ExecuteRequest saturatedRequest;
	saturatedRequest.runId = "run-c";
	saturatedRequest.sessionKey = "main";
	saturatedRequest.responderRunId = "run-c.r0";
	saturatedRequest.timeoutMs = 50;
	saturatedRequest.task = []() {
		blazeclaw::gateway::GatewayHost::ChatRuntimeResult result;
		result.ok = true;
		result.assistantText = "saturated";
		return result;
	};

	const auto saturated = service.EnqueueAndExecute(saturatedRequest);
	REQUIRE_FALSE(saturated.accepted);
	REQUIRE(saturated.saturated);
	REQUIRE(saturated.diagnosticCode == "thread_pool_queue_saturated");

	releaseFirst = true;

	const auto first = firstFuture.get();
	const auto second = secondFuture.get();

	REQUIRE(first.accepted);
	REQUIRE(first.completed);
	REQUIRE(first.runtimeResult.has_value());
	REQUIRE(first.runtimeResult->ok);

	REQUIRE(second.accepted);
	REQUIRE(second.completed);
	REQUIRE(second.runtimeResult.has_value());
	REQUIRE(second.runtimeResult->ok);
}

TEST_CASE(
	"Multi-active thread-pool cancellation race completes with cancel-compatible terminal state",
	"[gateway][multi-active][thread-pool][stress][cancel-race]")
{
	ThreadPoolRuntimeService service;
	ThreadPoolRuntimeService::Config config;
	config.minThreads = 1;
	config.maxThreads = 1;
	config.queueCapacity = 4;
	config.dequeueTimeoutMs = 2;
	service.Configure(config);

	std::atomic<bool> holdGate = true;
	std::atomic<bool> queuedTaskExecuted = false;

	auto firstFuture = std::async(
		std::launch::async,
		[&service, &holdGate]() {
			ThreadPoolRuntimeService::ExecuteRequest request;
			request.runId = "cancel-race-a";
			request.sessionKey = "main";
			request.responderRunId = "cancel-race-a.r0";
			request.timeoutMs = 5000;
			request.task = [&holdGate]() {
				while (holdGate.load()) {
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
				blazeclaw::gateway::GatewayHost::ChatRuntimeResult result;
				result.ok = true;
				result.assistantText = "first";
				return result;
			};
			return service.EnqueueAndExecute(request);
		});

	while (service.ActiveTaskCount() == 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	auto secondFuture = std::async(
		std::launch::async,
		[&service, &queuedTaskExecuted]() {
			ThreadPoolRuntimeService::ExecuteRequest request;
			request.runId = "cancel-race-b";
			request.sessionKey = "main";
			request.responderRunId = "cancel-race-b.r0";
			request.timeoutMs = 30;
			request.task = [&queuedTaskExecuted]() {
				queuedTaskExecuted = true;
				blazeclaw::gateway::GatewayHost::ChatRuntimeResult result;
				result.ok = true;
				result.assistantText = "second";
				return result;
			};
			return service.EnqueueAndExecute(request);
		});

	while (service.QueueDepth() == 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(80));
	const auto secondResult = secondFuture.get();
	REQUIRE(secondResult.accepted);
	REQUIRE(secondResult.timedOut);
	REQUIRE(secondResult.cancelled);
	REQUIRE(secondResult.diagnosticCode == "thread_pool_task_timeout");
	REQUIRE_FALSE(secondResult.metadata.taskId.empty());

	holdGate = false;

	const auto firstResult = firstFuture.get();
	REQUIRE(service.WaitForTaskDrain(secondResult.metadata.taskId, 2000));
	REQUIRE_FALSE(queuedTaskExecuted.load());

	REQUIRE(firstResult.accepted);
	REQUIRE(firstResult.completed);
	REQUIRE(firstResult.runtimeResult.has_value());
	REQUIRE(firstResult.runtimeResult->ok);
}
