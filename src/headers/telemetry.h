// telemetry.h
#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <string>
#include <vector>
#include <tangle.h>
#include <peers2.h>
#include <jsoncpp/json/json.h>

// Forward declarations — replace with #include "tangle.h" if preferred

struct MetricSample
{
	std::string ts;
	double cpu_percent = 0.0;
	uint64_t ram_total_mb = 0;
	uint64_t ram_used_mb = 0;
	uint64_t net_bytes_sent = 0;
	uint64_t net_bytes_recv = 0;
};

// control the background collector
void startTelemetryCollector(int interval_ms = 1000);
void stopTelemetryCollector();

// snapshot+clear collected metrics (thread-safe)
std::vector<MetricSample> snapshotAndClearMetrics();

// send telemetry: uses Tangle & Peers types from your project
// returns true on success
bool sendTelemetry(const std::string &endpoint,
				   const std::string &nodeId,
				   Tangle &tangle,
				   Peers &peers,
				   std::vector<MetricSample> &metrics);

#endif // TELEMETRY_H
