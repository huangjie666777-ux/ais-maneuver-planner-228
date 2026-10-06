#include "ais.h"
#include "predict.h"
#include "track.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include <stdexcept>

namespace {

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, \
                         __FILE__, __LINE__);                           \
            return 1;                                                   \
        }                                                               \
    } while (0)

struct BitWriter {
    std::vector<int> bits;
    void put(unsigned long value, int len) {
        for (int i = len - 1; i >= 0; --i)
            bits.push_back((value >> i) & 1);
    }
    void put_text(const std::string& text, int chars) {
        for (int k = 0; k < chars; ++k) {
            unsigned v = 0;
            if (k < static_cast<int>(text.size())) {
                unsigned char c = text[k];
                if (c >= 'A' && c <= 'Z')
                    v = c - 'A' + 1;
                else if (c >= '0' && c <= '9')
                    v = c - '0' + 27;
                else if (c == ' ')
                    v = 32;
            }
            put(v, 6);
        }
    }
};

std::string armor(const std::vector<int>& bits, int& fill) {
    fill = (6 - static_cast<int>(bits.size()) % 6) % 6;
    std::vector<int> b = bits;
    b.resize(b.size() + fill, 0);
    std::string out;
    for (std::size_t i = 0; i < b.size(); i += 6) {
        int v = 0;
        for (int j = 0; j < 6; ++j) v = (v << 1) | b[i + j];
        int c = v + 48;
        if (c > 87) c += 8;  // skip ASCII 88..95 (X.._)
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string sentence(const std::string& payload, int fill,
                     int count = 1, int number = 1, int seq = -1,
                     char channel = 'A') {
    char body[256];
    std::snprintf(body, sizeof(body), "AIVDM,%d,%d,%s,%c,%s,%d", count,
                  number, seq < 0 ? "" : std::to_string(seq).c_str(), channel,
                  payload.c_str(), fill);
    unsigned x = 0;
    for (char c : std::string(body)) x ^= static_cast<unsigned char>(c);
    char out[512];
    std::snprintf(out, sizeof(out), "!%s*%02X", body, x);
    return out;
}

ais_maneuver228::DecodedMessage round_trip_dynamic(
    std::uint32_t mmsi, int type, double lat, double lon, double sog,
    double cog, int sog_raw_override = -1, int cog_raw_override = -1) {
    BitWriter w;
    w.put(type, 6);
    w.put(0, 2);       // repeat
    w.put(mmsi, 30);
    w.put(0, 4);       // nav status
    w.put(0, 8);       // rot
    int sog_raw = sog_raw_override >= 0
                      ? sog_raw_override
                      : static_cast<int>(std::lround(sog * 10));
    w.put(sog_raw, 10);
    w.put(0, 1);       // position accuracy
    long lon_raw = static_cast<long>(std::lround(lon * 600000));
    long lat_raw = static_cast<long>(std::lround(lat * 600000));
    auto put_signed = [&](long value, int len) {
        unsigned long u = static_cast<unsigned long>(value) &
                          ((1UL << len) - 1);
        w.put(u, len);
    };
    put_signed(lon_raw, 28);
    put_signed(lat_raw, 27);
    int cog_raw = cog_raw_override >= 0
                      ? cog_raw_override
                      : static_cast<int>(std::lround(cog * 10));
    w.put(cog_raw, 12);
    w.put(0, 9);       // heading
    w.put(0, 6);       // timestamp
    w.put(0, 3);       // maneuver
    w.put(0, 3);       // spare + maneuver
    w.put(0, 1);       // RAIM
    w.put(0, 19);      // communication state
    int fill = 0;
    std::string payload = armor(w.bits, fill);
    std::string line = sentence(payload, fill);

    ais_maneuver228::SentenceFields f;
    std::string err;
    if (!ais_maneuver228::parse_sentence(line, f, err)) throw std::runtime_error(err);
    std::vector<int> bits;
    if (!ais_maneuver228::payload_to_bits(f.payload, f.fill_bits, bits, err))
        throw std::runtime_error(err);
    ais_maneuver228::DecodedMessage m;
    bool ignored = true;
    if (!ais_maneuver228::decode_bits(bits, m, ignored, err)) throw std::runtime_error(err);
    if (ignored) throw std::runtime_error("unexpectedly ignored");
    return m;
}

}  // namespace

int main() {
    // Checksum and fields
    {
        auto m = round_trip_dynamic(477000001, 1, 22.123456, 114.012345,
                                    12.3, 45.0);
        CHECK(m.mmsi == 477000001);
        CHECK(std::fabs(m.lat - 22.123456) < 0.0002);
        CHECK(std::fabs(m.lon - 114.012345) < 0.0002);
        CHECK(std::fabs(m.sog - 12.3) < 0.05);
        CHECK(std::fabs(m.cog - 45.0) < 0.05);
        CHECK(m.has_lat && m.has_lon && m.has_sog && m.has_cog);
    }
    {
        // SOG 1023 and COG 3600 are unavailable; lat/lon not-applied markers
        // are represented by 91/181 degree sentinels.
        auto m = round_trip_dynamic(123, 3, 91.0, 181.0, 0, 0, 1023, 3600);
        CHECK(!m.has_sog && !m.has_cog && !m.has_lat && !m.has_lon);
    }

    // Type 5 name decode
    {
        BitWriter w;
        w.put(5, 6);
        w.put(0, 2);
        w.put(555, 30);  // MMSI
        w.put(0, 2);    // AIS version
        w.put(0, 30);   // IMO number
        w.put_text("", 7);  // call sign (42 bits)
        w.put_text("HARBOR STAR", 20);
        w.put(0, 8);    // ship type
        w.put(0, 9);    // dimension A
        w.put(0, 9);    // dimension B
        w.put(0, 6);    // dimension C
        w.put(0, 6);    // dimension D
        w.put(0, 4);    // EPFD
        w.put(0, 20);   // ETA
        w.put(0, 8);    // draught
        w.put_text("", 20);
        w.put(0, 1);    // DTE
        w.put(0, 1);    // spare
        int fill = 0;
        auto payload = armor(w.bits, fill);
        auto line = sentence(payload, fill);
        ais_maneuver228::SentenceFields f;
        std::string err;
        if (!ais_maneuver228::parse_sentence(line, f, err)) throw std::runtime_error(err);
        std::vector<int> bits;
        if (!ais_maneuver228::payload_to_bits(f.payload, f.fill_bits, bits, err))
            throw std::runtime_error(err);
        ais_maneuver228::DecodedMessage m;
        bool ignored = false;
        if (!ais_maneuver228::decode_bits(bits, m, ignored, err))
            throw std::runtime_error(err);
        CHECK(m.type == 5 && m.name == "HARBOR STAR");
    }

    // Reassembly: out of order, duplicate idempotent, conflict drops group
    {
        auto now = ais_maneuver228::Clock::now();
        ais_maneuver228::Reassembler r({64, 16});
        ais_maneuver228::SentenceFields a{};
        a.fragment_count = 2;
        a.fragment_number = 2;
        a.sequence_id = 3;
        a.channel = 'A';
        a.payload = "10000";
        a.fill_bits = 2;
        ais_maneuver228::SentenceFields b = a;
        b.fragment_number = 1;
        b.payload = "5";
        b.fill_bits = 0;

        bool rejected = true;
        CHECK(!r.feed("h:1", a, now, rejected));
        CHECK(!rejected);
        CHECK(!r.feed("h:1", a, now, rejected));  // duplicate
        CHECK(!rejected);
        auto full = r.feed("h:1", b, now, rejected);
        CHECK(full.has_value() && !rejected);

        // Conflicting total wipes the group instead of merging.
        ais_maneuver228::SentenceFields c = b;
        c.fragment_count = 3;
        c.sequence_id = 4;
        ais_maneuver228::SentenceFields d = a;
        d.fragment_count = 2;
        d.sequence_id = 4;
        CHECK(!r.feed("h:2", c, now, rejected));
        CHECK(!r.feed("h:2", d, now, rejected));
        CHECK(rejected);
    }

    // Endpoint/channel/sequence isolation
    {
        auto now = ais_maneuver228::Clock::now();
        ais_maneuver228::Reassembler r({64, 16});
        ais_maneuver228::SentenceFields a{};
        a.fragment_count = 2;
        a.fragment_number = 1;
        a.sequence_id = 1;
        a.channel = 'A';
        a.payload = "5";
        {bool rej=true; CHECK(!r.feed("h:1", a, now, rej)); CHECK(!rej);}
    }

    // Geometry and DCPA: closing head-on at 10 m/s relative, 1000 m apart
    {
        TrackStore store(22.0, 114.0, 60, 100);
        auto now = ais_maneuver228::Clock::now();
        ais_maneuver228::DecodedMessage own{};
        own.type = 1;
        own.mmsi = 1;
        own.has_lat = own.has_lon = own.has_sog = own.has_cog = true;
        own.lat = 22.0; own.lon = 114.0; own.sog = 19.4384; own.cog = 180.0;
        ais_maneuver228::DecodedMessage tgt = own;
        tgt.mmsi = 2;
        tgt.sog = 19.4384;
        tgt.cog = 0.0;
        // ~1000 m south: 0.00899 deg
        tgt.lat = 22.0 - (1000.0 / 6371000.0) * (180.0 / 3.14159265358979323846);
        store.apply(own, now);
        store.apply(tgt, now);
        auto o = store.get(1, now);
        auto t = store.get(2, now);
        CHECK(o && t && o->target.in_zone && t->target.in_zone);
        auto e = evaluate_encounter(*o, *t, now, 600, 500.0);
        CHECK(e.valid);
        CHECK(std::fabs(e.now_distance - 1000.0) < 2.0);
        CHECK(std::fabs(e.dcpa) < 2.0);
        CHECK(std::fabs(e.tcpa - 50.0) < 0.5);
        CHECK(e.alarm);
    }

    std::printf("all tests passed\n");
    return 0;
}
