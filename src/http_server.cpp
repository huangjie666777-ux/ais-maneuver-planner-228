#include "http_server.h"

#include "predict.h"

#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

using json = nlohmann::json;

namespace {

json target_json(const TargetSnapshot& s) {
    const Target& t = s.target;
    json j;
    j["mmsi"] = t.mmsi;
    j["name"] = t.name;
    j["age_seconds"] = std::round(s.age_seconds * 1000.0) / 1000.0;
    j["fresh"] = s.fresh;
    j["in_zone"] = t.in_zone && t.has_pos;
    if (t.has_pos) {
        j["lat"] = t.lat;
        j["lon"] = t.lon;
        j["east_m"] = std::round(t.east * 10.0) / 10.0;
        j["north_m"] = std::round(t.north * 10.0) / 10.0;
    } else {
        j["lat"] = nullptr;
        j["lon"] = nullptr;
        j["position"] = "unavailable";
    }
    j["sog_knots"] = t.has_sog ? json(t.sog_knots) : nullptr;
    j["cog_deg"] = t.has_cog ? json(t.cog_deg) : nullptr;
    return j;
}

json encounter_json(const Encounter& e) {
    json j;
    j["mmsi"] = e.mmsi;
    j["name"] = e.name;
    j["age_seconds"] = std::round(e.age_seconds * 1000.0) / 1000.0;
    j["status"] = e.valid ? "evaluable" : "indeterminate";
    if (!e.valid) {
        j["reason"] = e.reason;
        j["alarm"] = false;
        j["safe"] = false;
        return j;
    }
    j["now_distance_m"] = std::round(e.now_distance * 10.0) / 10.0;
    j["dcpa_m"] = std::round(e.dcpa * 10.0) / 10.0;
    j["tcpa_seconds"] = std::round(e.tcpa * 10.0) / 10.0;
    j["alarm"] = e.alarm;
    j["safe"] = !e.alarm;
    return j;
}

http::response<http::string_body> make_response(
    const http::request<http::string_body>& req, http::status status,
    json body) {
    http::response<http::string_body> res(status, req.version());
    res.set(http::field::server, "ais-maneuver228/1.0");
    res.set(http::field::content_type, "application/json");
    res.keep_alive(req.keep_alive());
    res.body() = body.dump(2);
    res.prepare_payload();
    return res;
}

}  // namespace

struct HttpServer::Connection
    : std::enable_shared_from_this<Connection> {
    Connection(tcp::socket socket, const HttpServer& server)
        : stream_(std::move(socket)), server_(server) {}

    void run() {
        read_request();
    }

private:
    void read_request() {
        auto self = shared_from_this();
        http::async_read(
            stream_, buffer_, req_,
            [this, self](beast::error_code ec, std::size_t) {
                if (ec) return;
                handle();
            });
    }

    void send(http::response<http::string_body> res) {
        auto self = shared_from_this();
        auto keep = res.keep_alive();
        auto sp = std::make_shared<http::response<http::string_body>>(
            std::move(res));
        http::async_write(stream_, *sp,
                          [this, self, sp, keep](beast::error_code ec,
                                                 std::size_t) {
                              if (!ec && keep) read_request();
                          });
    }

    void handle() {
        const Config& cfg = server_.cfg_;
        auto now = ais_maneuver228::Clock::now();
        auto own = server_.tracks_.get(cfg.own_mmsi, now);

        if (req_.target() == "/health") {
            json j = {{"status", "ok"}};
            send(make_response(req_, http::status::ok, j));
            return;
        }

        if (req_.target() == "/v1/ships") {
            auto all = server_.tracks_.snapshot(now);
            json arr = json::array();
            for (const auto& s : all) arr.push_back(target_json(s));
            json j;
            j["query_time"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    now.time_since_epoch())
                                    .count();
            j["count"] = arr.size();
            j["ships"] = std::move(arr);
            send(make_response(req_, http::status::ok, j));
            return;
        }

        if (req_.target() == "/v1/own" || req_.target() == "/v1/encounters") {
            json j;
            j["own_mmsi"] = cfg.own_mmsi;
            j["guard_meters"] = cfg.guard_meters;
            j["predict_seconds"] = cfg.predict_seconds;
            j["valid_seconds"] = cfg.valid_seconds;
            j["udp"] = {{"accepted", server_.udp_.accepted()},
                         {"rejected", server_.udp_.rejected()},
                         {"ignored", server_.udp_.ignored()}};
            if (!own) {
                j["own_ship"] = nullptr;
                j["own_status"] = "never received";
            } else {
                j["own_ship"] = target_json(*own);
                if (own->fresh && own->target.has_pos && own->target.in_zone &&
                    own->target.has_sog && own->target.has_cog)
                    j["own_status"] = "ok";
                else if (!own->fresh)
                    j["own_status"] = "expired";
                else
                    j["own_status"] = "incomplete";
            }
            if (req_.target() == "/v1/encounters") {
                json arr = json::array();
                if (own) {
                    auto all = server_.tracks_.snapshot(now);
                    for (const auto& s : all) {
                        if (s.target.mmsi == cfg.own_mmsi) continue;
                        arr.push_back(encounter_json(
                            evaluate_encounter(*own, s, now,
                                               cfg.predict_seconds,
                                               cfg.guard_meters)));
                    }
                }
                j["targets"] = std::move(arr);
                j["alarm_count"] =
                    std::count_if(j["targets"].begin(), j["targets"].end(),
                                  [](const json& x) {
                                      return x.value("alarm", false);
                                  });
            }
            send(make_response(req_, http::status::ok, j));
            return;
        }

        send(make_response(req_, http::status::not_found,
                           json({{"error", "unknown route"}})));
    }

    beast::tcp_stream stream_;
    const HttpServer& server_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
};

HttpServer::HttpServer(asio::io_context& ioc, const Config& cfg,
                       const TrackStore& tracks, const UdpListener& udp)
    : io_(ioc),
      cfg_(cfg),
      tracks_(tracks),
      udp_(udp),
      acceptor_(ioc) {
    tcp::endpoint ep(asio::ip::make_address(cfg.http_listen), cfg.http_port);
    acceptor_.open(ep.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    acceptor_.bind(ep);
    acceptor_.listen();
}

void HttpServer::start() { accept_loop(); }

void HttpServer::stop() {
    stopping_ = true;
    boost::system::error_code ec;
    acceptor_.close(ec);
}

void HttpServer::accept_loop() {
    if (stopping_) return;
    auto self = shared_from_this();
    acceptor_.async_accept(
        io_, [this, self](beast::error_code ec, tcp::socket socket) {
            if (stopping_) return;
            if (!ec)
                std::make_shared<Connection>(std::move(socket), *this)->run();
            accept_loop();
        });
}
