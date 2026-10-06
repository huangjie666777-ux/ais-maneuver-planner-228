#include "config.h"
#include "http_server.h"
#include "track.h"
#include "udp_listener.h"

#include <boost/asio.hpp>

#include <csignal>
#include <cstdio>
#include <iostream>

int main(int argc, char** argv) {
    Config cfg;
    std::string error;
    if (!parse_args(argc, argv, cfg, error)) {
        std::fprintf(stderr,
                     "usage: ais_maneuver228 --own-mmsi M [--udp H:P] [--http H:P] "
                     "[--origin LAT,LON]\n"
                     "                     [--valid-seconds N] "
                     "[--predict-seconds N] [--guard-meters M]\n"
                     "error: %s\n",
                     error.c_str());
        return 2;
    }

    asio::io_context ioc;
    TrackStore tracks(cfg.origin_lat, cfg.origin_lon, cfg.valid_seconds,
                      cfg.max_ships);
    ais_maneuver228::ReassemblerLimits rl{cfg.max_fragments, cfg.max_groups};
    auto udp = std::make_shared<UdpListener>(ioc, cfg.udp_listen, cfg.udp_port,
                                             tracks, rl);
    auto http = std::make_shared<HttpServer>(ioc, cfg, tracks, *udp);
    udp->start();
    http->start();

    asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](auto, int) {
        udp->stop();
        http->stop();
        signals.clear();
    });

    std::cout << "ais_maneuver228 backend\n"
              << "  UDP  " << cfg.udp_listen << ':' << udp->local_port()
              << "\n"
              << "  HTTP " << cfg.http_listen << ':' << http->local_port()
              << "\n"
              << "  own MMSI " << cfg.own_mmsi << ", origin "
              << cfg.origin_lat << ',' << cfg.origin_lon << "\n"
              << "  valid " << cfg.valid_seconds << "s, predict "
              << cfg.predict_seconds << "s, guard " << cfg.guard_meters
              << " m\n"
              << std::flush;

    ioc.run();
    std::cout << "shut down, resources released\n";
    return 0;
}
