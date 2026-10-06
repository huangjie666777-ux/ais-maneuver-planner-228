#include "config.h"

#include <charconv>
#include <cstdlib>
#include <string_view>

namespace {

template <typename T>
bool num(std::string_view s, T& out) {
    T v{};
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size()) return false;
    out = v;
    return true;
}

bool split_host_port(const std::string& s, std::string& host, unsigned short& port) {
    auto c = s.rfind(':');
    if (c == std::string::npos) return false;
    unsigned p = 0;
    if (!num<unsigned>(s.substr(c + 1), p) || p == 0 || p > 65535) return false;
    host = s.substr(0, c);
    port = static_cast<unsigned short>(p);
    return true;
}

}  // namespace

bool parse_args(int argc, char** argv, Config& cfg, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string& value) -> bool {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--udp" && next(v)) {
            if (!split_host_port(v, cfg.udp_listen, cfg.udp_port)) {
                error = "bad --udp host:port";
                return false;
            }
        } else if (a == "--http" && next(v)) {
            if (!split_host_port(v, cfg.http_listen, cfg.http_port)) {
                error = "bad --http host:port";
                return false;
            }
        } else if (a == "--own-mmsi" && next(v)) {
            if (!num(v, cfg.own_mmsi) || cfg.own_mmsi == 0) {
                error = "bad --own-mmsi";
                return false;
            }
        } else if (a == "--origin" && next(v)) {
            auto c = v.find(',');
            if (c == std::string::npos || !num(v.substr(0, c), cfg.origin_lat) ||
                !num(v.substr(c + 1), cfg.origin_lon)) {
                error = "bad --origin lat,lon";
                return false;
            }
        } else if (a == "--valid-seconds" && next(v)) {
            if (!num(v, cfg.valid_seconds) || cfg.valid_seconds <= 0) {
                error = "bad --valid-seconds";
                return false;
            }
        } else if (a == "--predict-seconds" && next(v)) {
            if (!num(v, cfg.predict_seconds) || cfg.predict_seconds < 0) {
                error = "bad --predict-seconds";
                return false;
            }
        } else if (a == "--guard-meters" && next(v)) {
            if (!num(v, cfg.guard_meters) || cfg.guard_meters <= 0) {
                error = "bad --guard-meters";
                return false;
            }
        } else if (a == "--max-ships" && next(v)) {
            if (!num(v, cfg.max_ships) || cfg.max_ships == 0) {
                error = "bad --max-ships";
                return false;
            }
        } else {
            error = "unknown or incomplete argument: " + a;
            return false;
        }
    }
    if (cfg.own_mmsi == 0) {
        error = "--own-mmsi is required";
        return false;
    }
    if (std::abs(cfg.origin_lat) >= 70.0) {
        error = "origin latitude must have absolute value < 70 degrees";
        return false;
    }
    return true;
}
