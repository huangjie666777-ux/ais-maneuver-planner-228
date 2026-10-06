#pragma once

#include "ais.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct Target {
    std::uint32_t mmsi = 0;
    std::string name;

    // Last dynamic report state. Missing fields stay missing; stale positions
    // are never reused after an unavailable report.
    bool has_pos = false;
    bool has_sog = false;
    bool has_cog = false;
    double lat = 0.0;
    double lon = 0.0;
    double sog_knots = 0.0;
    double cog_deg = 0.0;

    ais_maneuver228::Clock::time_point received{};
    bool in_zone = false;
    double east = 0.0;   // meters from port origin
    double north = 0.0;
};

struct TargetSnapshot {
    Target target;
    double age_seconds = 0.0;
    bool fresh = false;
};

class TrackStore {
public:
    TrackStore(double origin_lat_deg, double origin_lon_deg, int valid_seconds,
               std::size_t max_ships);

    void apply(const ais_maneuver228::DecodedMessage& msg, ais_maneuver228::Clock::time_point now);
    std::vector<TargetSnapshot> snapshot(ais_maneuver228::Clock::time_point now) const;
    std::optional<TargetSnapshot> get(std::uint32_t mmsi,
                                      ais_maneuver228::Clock::time_point now) const;
    void prune(ais_maneuver228::Clock::time_point now);
    std::size_t size() const;

private:
    static constexpr double kEarthMeters = 6371000.0;
    static constexpr double kZoneMeters = 50000.0;

    void project(Target& t) const;

    mutable std::mutex mutex_;
    std::unordered_map<std::uint32_t, Target> ships_;
    double lat0_rad_;
    double lon0_rad_;
    double cos_lat0_;
    int valid_seconds_;
    std::size_t max_ships_;
};
