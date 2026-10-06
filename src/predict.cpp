#include "predict.h"

#include <cmath>

namespace {
constexpr double kKnotToMps = 1852.0 / 3600.0;
constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;

struct State {
    double x, y, vx, vy;
};

// Propagates the last report at constant velocity from its receive instant
// to the common query instant. Age uses monotonic receive time, never the
// AIS time-of-day field.
bool state_of(const TargetSnapshot& s, double query_age, State& out,
              std::string& reason) {
    const Target& t = s.target;
    if (!s.fresh) {
        reason = "target report expired";
        return false;
    }
    if (!t.has_pos) {
        reason = "position missing or unavailable";
        return false;
    }
    if (!t.in_zone) {
        reason = "position outside 50 km port zone";
        return false;
    }
    if (!t.has_sog || !t.has_cog) {
        reason = "SOG/COG unavailable";
        return false;
    }
    double speed = t.sog_knots * kKnotToMps;
    double cog = t.cog_deg * kDeg2Rad;
    out.vx = speed * std::sin(cog);  // east
    out.vy = speed * std::cos(cog);  // north
    out.x = t.east + out.vx * query_age;
    out.y = t.north + out.vy * query_age;
    return true;
}

}  // namespace

Encounter evaluate_encounter(const TargetSnapshot& own,
                             const TargetSnapshot& target,
                             ais_maneuver228::Clock::time_point query_time,
                             int predict_seconds, double guard_meters) {
    Encounter e;
    e.mmsi = target.target.mmsi;
    e.name = target.target.name;
    e.age_seconds = target.age_seconds;

    if (!own.fresh) {
        e.reason = "own ship report expired";
        return e;
    }
    if (!own.target.has_pos) {
        e.reason = "own ship position unavailable";
        return e;
    }
    if (!own.target.in_zone) {
        e.reason = "own ship outside 50 km port zone";
        return e;
    }
    if (!own.target.has_sog || !own.target.has_cog) {
        e.reason = "own ship SOG/COG unavailable";
        return e;
    }

    double own_age =
        std::chrono::duration<double>(query_time - own.target.received).count();
    double target_age =
        std::chrono::duration<double>(query_time - target.target.received)
            .count();

    State a, b;
    if (!state_of(own, own_age, a, e.reason)) return e;
    if (!state_of(target, target_age, b, e.reason)) return e;

    double rx = b.x - a.x;
    double ry = b.y - a.y;
    double rvx = b.vx - a.vx;
    double rvy = b.vy - a.vy;

    double horizon = static_cast<double>(predict_seconds);
    double rv2 = rvx * rvx + rvy * rvy;
    double tcpa;
    if (rv2 == 0.0) {
        // No relative motion: closest approach is immediate; TCPA is zero.
        tcpa = 0.0;
    } else {
        // |r + v*t|^2 is minimized at t = -r.v / v.v.
        tcpa = -(rx * rvx + ry * rvy) / rv2;
        if (tcpa < 0.0) tcpa = 0.0;
        if (tcpa > horizon) tcpa = horizon;
    }
    double cx = rx + rvx * tcpa;
    double cy = ry + rvy * tcpa;
    e.valid = true;
    e.now_distance = std::hypot(rx, ry);
    e.tcpa = tcpa;
    e.dcpa = std::hypot(cx, cy);
    e.alarm = e.dcpa <= guard_meters;
    return e;
}
