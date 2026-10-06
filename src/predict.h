#pragma once

#include "track.h"

#include <string>

struct Encounter {
    std::uint32_t mmsi = 0;
    std::string name;

    bool valid = false;
    std::string reason;  // populated when valid == false

    double now_distance = 0.0;
    double dcpa = 0.0;
    double tcpa = 0.0;  // seconds, clamped to [0, predict_seconds]
    bool alarm = false;

    double age_seconds = 0.0;
};

// Evaluates one target against own ship. Every invalid input is reported as
// invalid and must never be presented as "safe".
Encounter evaluate_encounter(const TargetSnapshot& own,
                             const TargetSnapshot& target,
                             ais_maneuver228::Clock::time_point query_time,
                             int predict_seconds, double guard_meters);
