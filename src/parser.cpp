#include "parser.hpp"
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

bool read_file(const std::string& path, std::string& out)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    std::ostringstream oss; oss << ifs.rdbuf();
    out = std::move(oss).str();
    return true;
}

static void parse_event_object(const json& o, std::vector<Event>& out, uint64_t durMinUs)
{
    Event e;
    e.name = o.value("name", std::string());
    e.category = o.value("cat", std::string());
    e.data = o.value("data", std::string());
    e.ts = o.value("ts", 0ull);
    e.dur = o.value("dur", 0ull);
    e.color = o.value("color", std::string());

    if (durMinUs == 0 || e.dur >= durMinUs)
        out.push_back(std::move(e));
}

static void parse_stat_object(const json& o, std::unordered_map<std::string, EventStats>& out)
{
    // accept { "type":"stat", "name":"X", ... } or { "name":"X", ... } (without "type")
    const json* s = &o;
    if (o.contains("stats")) s = &o["stats"];
    if (!s->is_object())
        return;

    std::string name = s->value("name", std::string());
    if (name.empty())
        return;

    EventStats st;
    st.count = s->value("count", 0ull);
    st.avg_us = s->value("avg_us", 0.0);
    st.min_us = s->value("min_us", 0ull);
    st.max_us = s->value("max_us", 0ull);
    out[name] = st;
}

static void parse_metric_object(const json& o, std::vector<Metric>& out)
{
    Metric m;
    m.cpu = o.value("cpu", 0.0);
    m.cpu_total = o.value("cpu_total", 0.0);
    m.ram_used = o.value("ram_used", 0ull);
    m.ram_total = o.value("ram_total", 0ull);
    m.ts = o.value("ts", 0ull);
    out.push_back(m);
}

// simple (event/stat/metric)
static void parse_one_object(const json& obj, std::vector<Event>& outEvents, std::unordered_map<std::string, EventStats>& outStats, std::vector<Metric>& outMetrics, uint64_t durMinUs)
{
    const std::string t = obj.value("type", std::string());

    if (t == "event")
        return parse_event_object(obj, outEvents, durMinUs);
    if (t == "stat")
        return parse_stat_object(obj, outStats);
    if (t == "metric")
        return parse_metric_object(obj, outMetrics);

    // heuristiques

    // may be event
    if (obj.contains("ts") && obj.contains("dur"))
        return parse_event_object(obj, outEvents, durMinUs);
    // stat unique
    if (obj.contains("stats"))
        return parse_stat_object(obj, outStats);
    if (obj.contains("cpu") || obj.contains("ram_used") || obj.contains("ram_total") || obj.contains("ram_total_gb"))
        return parse_metric_object(obj, outMetrics);

    // noop
}

// ---------- API ----------
bool parse_trace_payload(const std::string& jsonText, std::vector<Event>& outEvents, std::unordered_map<std::string, EventStats>& outStats, std::vector<Metric>& outMetrics, uint64_t durMinUs, std::string* outError)
{
    outEvents.clear();
    outStats.clear();
    outMetrics.clear();

    json root;
    try
    {
        root = json::parse(jsonText);
    }
    catch (const std::exception& e)
    {
        if (outError)
            *outError = e.what();
        return false;
    }

    // 1) {"traceEvents":[...], "stats":[...], "metrics":[...]}
    if (root.is_object() &&
        (root.contains("traceEvents") || root.contains("stats") || root.contains("metrics")))
    {
        if (root.contains("traceEvents") && root["traceEvents"].is_array())
        {
            for (const auto& it : root["traceEvents"])
                parse_one_object(it, outEvents, outStats, outMetrics, durMinUs);
        }
        if (root.contains("stats") && root["stats"].is_array() && !root["stats"].empty())
        {
            for (const auto& it : root["stats"])
                parse_stat_object(it, outStats);
        }
        if (root.contains("metrics") && root["metrics"].is_array() && !root["metrics"].empty())
        {
            for (const auto& it : root["metrics"])
                parse_metric_object(it, outMetrics);
        }
        return true;
    }

    // 2) Mixted array
    if (root.is_array())
    {
        for (const auto& it : root)
            parse_one_object(it, outEvents, outStats, outMetrics, durMinUs);
        return true;
    }

    // 3) Unique
    if (root.is_object())
    {
        parse_one_object(root, outEvents, outStats, outMetrics, durMinUs);
        return true;
    }

    if (outError) *outError = "Unsupported JSON root";
    return false;
}
