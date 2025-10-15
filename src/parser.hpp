#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include "model.hpp"

// Parse JSON trace into events.
// - path: JSON file
// - out:  parsed event
// - outGlobalStats: stats
// - outMetrics: cpu/ram metrics
// - durMinUs: filter (keep all event <here dur >= durMinUs). 0 = no filter.
// - outGlobalStats: map name -> EventStats if bloc "stats" exists.
// - outError: readable error optionnal.
//
// True in success
bool parse_trace_payload(const std::string& path, std::vector<Event>& out, std::unordered_map<std::string, EventStats>& outGlobalStats, std::vector<Metric>& outMetrics, uint64_t durMinUs = 0, std::string* outError = nullptr);
bool read_file(const std::string& path, std::string& out);