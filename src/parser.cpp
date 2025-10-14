#include "parser.hpp"
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static bool read_file(const std::string& path, std::string& out)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    std::ostringstream oss; oss << ifs.rdbuf();
    out = std::move(oss).str();
    return true;
}

static void fill_event_from_json(const json& it, Event& e)
{
    // Compat: "cat" ou "category", "color" optionnel, etc.
    e.name = it.value("name", std::string());
    e.category = it.value("cat", std::string());
    e.data = it.value("data", std::string());
    e.ts = it.value("ts", 0ull);
    e.dur = it.value("dur", 0ull);
    e.color = it.value("color", std::string());
}

static void read_global_stats_object(const json& jstats, std::unordered_map<std::string, EventStats>& out)
{
    // stats = [ { "name":"X", "count":..., "avg_us":..., ... }, ... ]

    if (jstats.is_array())
    {
        for (const auto& s : jstats) {
            if (!s.is_object()) continue;
            const std::string name = s.value("name", std::string());
            if (name.empty()) continue;
            EventStats es;
            es.count = s.value("count", 0ull);
            es.avg_us = s.value("avg_us", 0.0);
            es.min_us = s.value("min_us", 0ull);
            es.max_us = s.value("max_us", 0ull);
            out[name] = es;
        }
    }
}

bool parse_trace_file(const std::string& path, std::vector<Event>& out, std::unordered_map<std::string, EventStats>& outGlobalStats, uint64_t durMinUs, std::string* outError)
{
    std::string txt;
    if (!read_file(path, txt)) {
        if (outError) *outError = "Cannot read file";
        return false;
    }

    json root;
    try {
        root = json::parse(txt);
    }
    catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }

    // mapping global stats
    std::unordered_map<std::string, EventStats> globalStats;

    // Structure:
    // - root object => { traceEvents:[...], stats: ... }
    json eventsJson;
    if (root.is_object())
    {
        if (root.contains("traceEvents"))
        {
            eventsJson = root["traceEvents"];
        }
        else
        {
            // fallback err
            if (outError) *outError = "Missing 'traceEvents' array.";
            return false;
        }

        // stats globales
        if (root.contains("stats"))
        {
            read_global_stats_object(root["stats"], globalStats);
        }
    }
    else {
        if (outError) *outError = "Root must be array or object";
        return false;
    }

    if (!eventsJson.is_array())
    {
        if (outError) *outError = "'traceEvents' must be an array";
        return false;
    }

    out.clear();
    out.reserve(eventsJson.size());

    for (const auto& it : eventsJson)
    {
        if (!it.is_object()) continue;
        Event e;
        fill_event_from_json(it, e);

        // Filtre same duration
        if (durMinUs > 0 && e.dur < durMinUs) continue;

        // If local stats missing, try apply global stats by "name"
        if (e.stats.count == 0 && !e.name.empty())
        {
            auto gs = globalStats.find(e.name);
            if (gs != globalStats.end())
            {
                e.stats = gs->second;
            }
        }

        out.push_back(std::move(e));
    }
    outGlobalStats = std::move(globalStats);

    return true;
}
