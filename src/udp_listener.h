#pragma once

#include "ais.h"
#include "track.h"

#include <boost/asio.hpp>

#include <array>
#include <atomic>
#include <string>

namespace asio = boost::asio;
using boost::asio::ip::udp;

class UdpListener : public std::enable_shared_from_this<UdpListener> {
public:
    UdpListener(asio::io_context& ioc, const std::string& host,
                unsigned short port, TrackStore& tracks,
                ais_maneuver228::ReassemblerLimits reassembler_limits);

    void start();
    void stop();
    unsigned short local_port() const { return socket_.local_endpoint().port(); }

    std::uint64_t accepted() const { return accepted_.load(); }
    std::uint64_t rejected() const { return rejected_.load(); }
    std::uint64_t ignored() const { return ignored_.load(); }

private:
    void receive_loop();
    void maintain(boost::system::error_code ec);
    std::string sender_key(const udp::endpoint& ep) const;

    asio::io_context& io_;
    udp::socket socket_;
    udp::endpoint sender_ep_;
    std::array<char, 2048> buffer_{};
    asio::steady_timer maintenance_timer_;

    TrackStore& tracks_;
    ais_maneuver228::Reassembler reassembler_;
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint64_t> accepted_{0};
    std::atomic<std::uint64_t> rejected_{0};
    std::atomic<std::uint64_t> ignored_{0};
};
