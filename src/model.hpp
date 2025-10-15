#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <functional>

// =============== Stats ===============
struct EventStats
{
    uint64_t count = 0;
    double   avg_us = 0.0;
    uint64_t min_us = 0;
    uint64_t max_us = 0;
};

// =============== SysStats ===============
struct Metric
{
    double cpu = 0.0;
    double cpu_total = 0.0;
    uint64_t ram_used = 0;
    uint64_t ram_total = 0;
    uint64_t ts = 0;
};

// =============== Keys (optionnal vue/aggregate by type) ===============
// format "stats" index by name
// Key (cat,name) if aggregate client side
struct EventKindKey
{
    std::string category;
    std::string name;

    bool operator==(const EventKindKey& o) const noexcept
    {
        return category == o.category && name == o.name;
    }
};

struct EventKindKeyHash
{
    size_t operator()(const EventKindKey& k) const noexcept
    {
        std::hash<std::string> H;
        return (H(k.category) * 1315423911u) ^ H(k.name);
    }
};

// =============== Event ===============
// producer: { name, cat, data, ph, ts, dur, pid, tid, id, color }
// + client derived filed (normStart/normEnd).
// + "stats" local stored
struct Event {
    // Producteur
    std::string name;       // "name"
    std::string category;   // "cat"
    std::string data;       // anything
    char        ph = 'X';   // "ph" (phase), par default 'X'
    uint64_t    ts = 0;     // "ts"  (µs absolute)
    uint64_t    dur = 0;    // "dur" (µs)
    uint32_t    pid = 1;    // "pid" (optionnal)
    uint32_t    tid = 0;    // "tid" (optionnal)
    uint64_t    id = 0;     // "id"  (optionnal)
    std::string color;      // "#RRGGBB" optionnal

// ---- internal ---- \\
    // Compat: stats holder
    EventStats  stats{};

    // Derived client (normilized timeline [0..1])
    double normStart = 0.0;
    double normEnd = 0.0;
};

// =============== Full Document ===============
// format:
// {
//   "traceEvents": [ Event, ... ],
//   "stats": [ { Stats, ... ],
//   "metrics": [ Metrics, ... ]
// }
