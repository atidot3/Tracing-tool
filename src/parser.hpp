#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include "model.hpp"

// Parse JSON trace into events.
// - path: JSON file
// - out:  parsed event
// - durMinUs: filter (keep all event <here dur >= durMinUs). 0 = no filter.
// - outGlobalStats: map name -> EventStats if bloc "stats" exists.
// - outError: readable error optionnal.
//
// True in success
bool parse_trace_file(const std::string& path, std::vector<Event>& out, std::unordered_map<std::string, EventStats>& outGlobalStats, uint64_t durMinUs = 0, std::string* outError = nullptr);
