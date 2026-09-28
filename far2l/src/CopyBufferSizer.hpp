#pragma once
#include <algorithm>
#include <cstdint>

// Probe larger requests to distinguish fixed I/O latency from slow media.
// A failed probe backs off; accepted probes allow a bounded latency increase.
class CopyBufferSizer
{
	unsigned int _samples = 0, _cooldown = 0;
	uint64_t _elapsed = 0, _probe_elapsed = 0, _latency_budget = 1000;
	uint32_t _probe_from = 0;

public:
	uint32_t Observe(uint32_t size, uint32_t capacity, uint64_t elapsed)
	{
		constexpr uint32_t minimum = 0x10000;
		elapsed = std::max<uint64_t>(elapsed, 1);
		const uint32_t larger = size > capacity / 2 ? capacity : size * 2;
		if (_probe_from) {
			const uint32_t previous = _probe_from;
			_probe_from = 0;
			const bool faster = double(size) / elapsed > 1.125 * double(previous) / _probe_elapsed;
			const uint64_t budget = std::max<uint64_t>(1000, _probe_elapsed + _probe_elapsed / 2);
			if (!faster || elapsed > budget) {
				_cooldown = 16;
				return previous;
			}
			_latency_budget = budget;
			return size;
		}
		if (elapsed < 100) {
			_samples = 0;
			_elapsed = 0;
			_cooldown = 0;
			_latency_budget = 1000;
			return larger;
		}
		if (elapsed > _latency_budget && size > minimum) {
			_samples = 0;
			_elapsed = 0;
			return std::max(size / 2, minimum);
		}
		if (_cooldown) {
			--_cooldown;
			return size;
		}
		_elapsed+= elapsed;
		if (++_samples == 4) {
			if (larger > size) {
				_probe_from = size;
				_probe_elapsed = std::max<uint64_t>(_elapsed / _samples, 1);
			}
			_samples = 0;
			_elapsed = 0;
			return larger;
		}
		return size;
	}
};
