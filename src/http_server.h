#pragma once

#include "config.h"
#include "track.h"
#include "udp_listener.h"

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

#include <atomic>
#include <memory>
#include <string>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

class HttpServer : public std::enable_shared_from_this<HttpServer> {
public:
    HttpServer(asio::io_context& ioc, const Config& cfg,
               const TrackStore& tracks, const UdpListener& udp);

    void start();
    void stop();
    unsigned short local_port() const { return acceptor_.local_endpoint().port(); }

private:
    struct Connection;
    void accept_loop();

    asio::io_context& io_;
    const Config& cfg_;
    const TrackStore& tracks_;
    const UdpListener& udp_;
    tcp::acceptor acceptor_;
    std::atomic<bool> stopping_{false};
};
