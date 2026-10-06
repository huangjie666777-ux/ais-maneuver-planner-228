#include "track.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;

double wrap_delta_deg(double d) {
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return d;
}
}  // namespace

TrackStore::TrackStore(double origin_lat_deg, double origin_lon_deg,
                       int valid_seconds, std::size_t max_ships)
    : lat0_rad_(origin_lat_deg * kDeg2Rad),
      lon0_rad_(origin_lon_deg * kDeg2Rad),
      cos_lat0_(std::cos(lat0_rad_)),
      valid_seconds_(valid_seconds),
      max_ships_(max_ships) {}

void TrackStore::project(Target& t) const {
    double lat_rad = t.lat * kDeg2Rad;
    double dlon = wrap_delta_deg(t.lon - lon0_rad_ / kDeg2Rad) * kDeg2Rad;
    t.east = kEarthMeters * dlon * cos_lat0_;
    t.north = kEarthMeters * (lat_rad - lat0_rad_);
    t.in_zone = std::hypot(t.east, t.north) <= kZoneMeters;
}

void TrackStore::apply(const ais_maneuver228::DecodedMessage& msg,
                       ais_maneuver228::Clock::time_point now) {
    std::lock_guard lock(mutex_);
    auto [it, inserted] = ships_.try_emplace(msg.mmsi);
    Target& t = it->second;
    if (inserted && ships_.size() > max_ships_) {
        ships_.erase(it);
        return;
    }
    t.mmsi = msg.mmsi;
    t.received = now;

    if (msg.type == 5) {
        if (!msg.name.empty()) t.name = msg.name;
        return;
    }

    t.has_sog = msg.has_sog;
    t.has_cog = msg.has_cog;
    t.has_pos = msg.has_lat && msg.has_lon;
    if (msg.has_sog) t.sog_knots = msg.sog;
    if (msg.has_cog) t.cog_deg = msg.cog;
    if (t.has_pos) {
        t.lat = msg.lat;
        t.lon = msg.lon;
        project(t);
    } else {
        t.in_zone = false;
        t.east = 0.0;
        t.north = 0.0;
    }
}

TargetSnapshot snapshot_of(const Target& t, ais_maneuver228::Clock::time_point now,
                           int valid_seconds) {
    TargetSnapshot s;
    s.target = t;
    double age = std::chrono::duration<double>(now - t.received).count();
    s.age_seconds = age;
    s.fresh = age >= 0.0 && age <= static_cast<double>(valid_seconds);
    return s;
}

std::vector<TargetSnapshot> TrackStore::snapshot(
    ais_maneuver228::Clock::time_point now) const {
    std::lock_guard lock(mutex_);
    std::vector<TargetSnapshot> out;
    out.reserve(ships_.size());
    for (const auto& [mmsi, t] : ships_)
        out.push_back(snapshot_of(t, now, valid_seconds_));
    std::sort(out.begin(), out.end(),
              [](const TargetSnapshot& a, const TargetSnapshot& b) {
                  return a.target.mmsi < b.target.mmsi;
              });
    return out;
}

std::optional<TargetSnapshot> TrackStore::get(
    std::uint32_t mmsi, ais_maneuver228::Clock::time_point now) const {
    std::lock_guard lock(mutex_);
    auto it = ships_.find(mmsi);
    if (it == ships_.end()) return std::nullopt;
    return snapshot_of(it->second, now, valid_seconds_);
}

void TrackStore::prune(ais_maneuver228::Clock::time_point now) {
    std::lock_guard lock(mutex_);
    for (auto it = ships_.begin(); it != ships_.end();) {
        double age =
            std::chrono::duration<double>(now - it->second.received).count();
        if (age > static_cast<double>(valid_seconds_) * 2.0)
            it = ships_.erase(it);
        else
            ++it;
    }
}

std::size_t TrackStore::size() const {
    std::lock_guard lock(mutex_);
    return ships_.size();
}
