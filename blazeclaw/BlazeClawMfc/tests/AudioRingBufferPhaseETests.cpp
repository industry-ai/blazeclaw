#include "pch.h"

#include "../src/app/AudioRingBuffer.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("AudioRingBuffer keeps latest window after overwrite", "[speech][ring][phase_e]")
{
	AudioRingBuffer ring(8);

	const std::vector<float> first = { 1, 2, 3, 4, 5 };
	const std::vector<float> second = { 6, 7, 8, 9, 10 };
	ring.Push(first.data(), first.size());
	ring.Push(second.data(), second.size());

	std::vector<float> latest;
	REQUIRE(ring.ReadLatest(latest, 8));
	REQUIRE(latest == std::vector<float>({ 3, 4, 5, 6, 7, 8, 9, 10 }));
	REQUIRE(ring.GetDroppedSamples() == 2);
}

TEST_CASE("AudioRingBuffer reads deterministic sequence windows under wrap", "[speech][ring][phase_e]")
{
	AudioRingBuffer ring(6);
	const std::vector<float> values = { 10, 11, 12, 13, 14, 15, 16 };
	ring.Push(values.data(), values.size());

	std::vector<float> window;
	const uint64_t oldest = ring.GetOldestAvailableSequence();
	REQUIRE(ring.ReadWindowBySequence(window, oldest + 1, 4));
	REQUIRE(window == std::vector<float>({ 12, 13, 14, 15 }));
}

TEST_CASE("AudioRingBuffer peek latest fast-fails when insufficient data", "[speech][ring][phase_e]")
{
	AudioRingBuffer ring(16);
	const std::vector<float> values = { 0.1f, 0.2f, 0.3f };
	ring.Push(values.data(), values.size());

	std::vector<float> latest;
	REQUIRE_FALSE(ring.PeekLatest(latest, 8));
}
