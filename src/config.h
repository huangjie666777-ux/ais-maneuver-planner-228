#pragma once

#include <cstdint>
#include <string>

struct Config {
    std::string udp_listen = "127.0.0.1";
    unsigned short udp_port = 10110;
    std::string http_listen = "127.0.0.1";
    unsigned short http_port = 8080;

    std::uint32_t own_mmsi = 0;
    double origin_lat = 0.0;  // degrees
    double origin_lon = 0.0;

    int valid_seconds = 30;
    int predict_seconds = 300;
    double guard_meters = 1000.0;

    std::size_t max_ships = 10000;
    std::size_t max_fragments = 4096;
    std::size_t max_groups = 2048;
};

bool parse_args(int argc, char** argv, Config& cfg, std::string& error);
