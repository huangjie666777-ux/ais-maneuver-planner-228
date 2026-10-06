#include "udp_listener.h"

#include <sstream>

UdpListener::UdpListener(asio::io_context& ioc, const std::string& host,
                         unsigned short port, TrackStore& tracks,
                         ais_maneuver228::ReassemblerLimits reassembler_limits)
    : io_(ioc),
      socket_(ioc),
      maintenance_timer_(ioc),
      tracks_(tracks),
      reassembler_(reassembler_limits) {
    udp::endpoint ep(asio::ip::make_address(host), port);
    socket_.open(ep.protocol());
    socket_.set_option(udp::socket::reuse_address(true));
    socket_.bind(ep);
}

void UdpListener::start() {
    // Caller holds a shared_ptr (constructed via make_shared), so
    // shared_from_this() inside the loops is valid.
    receive_loop();
    maintain({});
}

void UdpListener::stop() {
    stopping_ = true;
    boost::system::error_code ec;
    socket_.cancel(ec);
    maintenance_timer_.cancel(ec);
    socket_.close(ec);
}

std::string UdpListener::sender_key(const udp::endpoint& ep) const {
    // Endpoint isolation: different senders never share fragment groups.
    std::ostringstream os;
    os << ep.address().to_string() << ':' << ep.port();
    return os.str();
}

void UdpListener::receive_loop() {
    if (stopping_) return;
    auto self = shared_from_this();
    socket_.async_receive_from(
        asio::buffer(buffer_), sender_ep_,
        [this, self](boost::system::error_code ec, std::size_t n) {
            if (stopping_) return;
            if (ec) {
                if (ec != asio::error::operation_aborted) receive_loop();
                return;
            }
            std::string line(buffer_.data(), n);
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();

            ais_maneuver228::SentenceFields fields;
            std::string err;
            if (ais_maneuver228::parse_sentence(line, fields, err)) {
                bool rejected = false;
                auto bits = reassembler_.feed(sender_key(sender_ep_), fields,
                                              ais_maneuver228::Clock::now(), rejected);
                if (rejected) {
                    rejected_.fetch_add(1);
                } else if (bits) {
                    ais_maneuver228::DecodedMessage msg;
                    bool ignored = false;
                    if (ais_maneuver228::decode_bits(*bits, msg, ignored, err)) {
                        tracks_.apply(msg, ais_maneuver228::Clock::now());
                        accepted_.fetch_add(1);
                    } else if (ignored) {
                        ignored_.fetch_add(1);
                    } else {
                        rejected_.fetch_add(1);
                    }
                }
            } else {
                rejected_.fetch_add(1);
            }
            receive_loop();
        });
}

void UdpListener::maintain(boost::system::error_code ec) {
    if (stopping_ || ec == asio::error::operation_aborted) return;
    auto now = ais_maneuver228::Clock::now();
    reassembler_.expire(now);
    tracks_.prune(now);
    auto self = shared_from_this();
    maintenance_timer_.expires_after(std::chrono::seconds(1));
    maintenance_timer_.async_wait(
        [this, self](boost::system::error_code e) { maintain(e); });
}
