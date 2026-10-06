#include "ais.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <sstream>

namespace ais_maneuver228 {
namespace {

using Bits = std::vector<int>;

std::optional<long long> to_int(std::string_view s) {
    if (s.empty()) return std::nullopt;
    long long v = 0;
    const char* b = s.data();
    const char* e = s.data() + s.size();
    auto [p, ec] = std::from_chars(b, e, v);
    if (ec != std::errc{} || p != e) return std::nullopt;
    return v;
}

unsigned get_uint(const Bits& b, std::size_t pos, int len) {
    unsigned v = 0;
    for (int i = 0; i < len; ++i)
        v = (v << 1) | static_cast<unsigned>(b[pos + i]);
    return v;
}

long get_int(const Bits& b, std::size_t pos, int len) {
    long v = static_cast<long>(get_uint(b, pos, len));
    if (v >= (1L << (len - 1))) v -= (1L << len);
    return v;
}

}  // namespace

bool verify_checksum(const std::string& sentence) {
    if (sentence.size() < 5 || sentence[0] != '!') return false;
    auto star = sentence.rfind('*');
    if (star == std::string::npos || star + 3 > sentence.size()) return false;
    auto hexval = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    int hi = hexval(sentence[star + 1]);
    int lo = hexval(sentence[star + 2]);
    if (hi < 0 || lo < 0) return false;
    unsigned expected = static_cast<unsigned>(hi * 16 + lo);
    unsigned x = 0;
    for (std::size_t i = 1; i < star; ++i)
        x ^= static_cast<unsigned char>(sentence[i]);
    return x == expected;
}

bool parse_sentence(const std::string& line, SentenceFields& out,
                    std::string& error) {
    std::string s = line;
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    if (s.size() < 7 || s.rfind("!AIVDM,", 0) != 0) {
        error = "not an !AIVDM sentence";
        return false;
    }
    if (!verify_checksum(s)) {
        error = "checksum mismatch";
        return false;
    }

    auto star = s.rfind('*');
    std::string body = s.substr(7, star - 7);
    std::vector<std::string> f;
    std::string cur;
    std::stringstream ss(body);
    while (std::getline(ss, cur, ',')) f.push_back(cur);
    if (f.size() != 6) {
        error = "expected 6 AIVDM fields";
        return false;
    }
    auto nfrag = to_int(f[0]);
    auto fnum = to_int(f[1]);
    if (!nfrag || *nfrag < 1 || *nfrag > 9) {
        error = "bad fragment count";
        return false;
    }
    if (!fnum || *fnum < 1 || *fnum > *nfrag) {
        error = "bad fragment number";
        return false;
    }
    out.fragment_count = static_cast<int>(*nfrag);
    out.fragment_number = static_cast<int>(*fnum);
    if (!f[2].empty()) {
        auto seq = to_int(f[2]);
        if (!seq || *seq < 0 || *seq > 9) {
            error = "bad sequence id";
            return false;
        }
        out.sequence_id = static_cast<int>(*seq);
    } else if (out.fragment_count > 1) {
        error = "multi-fragment message requires sequence id";
        return false;
    }
    if (f[3].size() != 1 ||
        (f[3][0] != 'A' && f[3][0] != 'B' && f[3][0] != '1' &&
         f[3][0] != '2')) {
        error = "bad VHF channel";
        return false;
    }
    out.channel = f[3][0];
    out.payload = f[4];
    if (out.payload.empty()) {
        error = "empty payload";
        return false;
    }
    if (!f[5].empty()) {
        auto fb = to_int(f[5]);
        if (!fb || *fb < 0 || *fb > 5) {
            error = "bad fill bits";
            return false;
        }
        out.fill_bits = static_cast<int>(*fb);
    }
    return true;
}

bool payload_to_bits(const std::string& payload, int fill_bits, Bits& bits,
                     std::string& error) {
    bits.clear();
    bits.reserve(payload.size() * 6);
    for (char c : payload) {
        int v = static_cast<unsigned char>(c) - 48;
        // Encoding skips ASCII 88..95: values 40..63 map onto 96..119.
        if (v > 39) v -= 8;
        if (v < 0 || v > 63) {
            error = "payload character outside six-bit alphabet";
            return false;
        }
        for (int i = 5; i >= 0; --i) bits.push_back((v >> i) & 1);
    }
    if (fill_bits > static_cast<int>(bits.size())) {
        error = "fill bits exceed payload";
        return false;
    }
    bits.resize(bits.size() - static_cast<std::size_t>(fill_bits));
    return true;
}

namespace {

char sixbit_char(int v) {
    if (v < 32) return static_cast<char>('@' + v);
    return static_cast<char>(v);
}

std::string decode_name(const Bits& b, std::size_t pos, int len) {
    std::string s;
    for (std::size_t i = 0; i + 6 <= pos + static_cast<std::size_t>(len);
         i += 6) {
        int v = static_cast<int>(get_uint(b, pos + i, 6));
        if (v == 0) break;
        char c = sixbit_char(v);
        if (c == '@') break;
        if (c >= ' ') s.push_back(c);
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

}  // namespace

bool decode_bits(const Bits& bits, DecodedMessage& out, bool& ignored,
                 std::string& error) {
    ignored = false;
    if (bits.size() < 38) {
        error = "payload too short";
        return false;
    }
    out.type = static_cast<int>(get_uint(bits, 0, 6));
    out.mmsi = get_uint(bits, 8, 30);
    if (out.mmsi == 0) {
        error = "missing MMSI";
        return false;
    }

    if (out.type == 1 || out.type == 2 || out.type == 3) {
        if (bits.size() < 168) {
            error = "position report too short";
            return false;
        }
        long sog_raw = static_cast<long>(get_uint(bits, 50, 10));
        long lon_raw = get_int(bits, 61, 28);
        long lat_raw = get_int(bits, 89, 27);
        long cog_raw = static_cast<long>(get_uint(bits, 116, 12));

        out.has_sog = sog_raw < 1023;
        out.has_cog = cog_raw < 3600;
        out.has_lon = lon_raw != 0x6791AC0;
        out.has_lat = lat_raw != 0x3412140;
        if (out.has_sog) out.sog = sog_raw / 10.0;
        if (out.has_cog) out.cog = cog_raw / 10.0;
        if (out.has_lon) out.lon = lon_raw / 600000.0;
        if (out.has_lat) out.lat = lat_raw / 600000.0;
        return true;
    }

    if (out.type == 5) {
        if (bits.size() < 424) {
            error = "type 5 report too short";
            return false;
        }
        out.name = decode_name(bits, 112, 120);
        return true;
    }

    ignored = true;
    return false;
}

std::optional<Bits> Reassembler::feed(const std::string& sender_key,
                                      const SentenceFields& f,
                                      Clock::time_point now,
                                      bool& rejected) {
    rejected = false;
    if (f.fragment_count > 1 && f.sequence_id < 0) {
        rejected = true;
        return std::nullopt;
    }
    std::string key = sender_key;
    key.push_back('|');
    key.push_back(f.channel);
    key.push_back('|');
    key.append(f.sequence_id < 0 ? "-" : std::to_string(f.sequence_id));

    if (f.fragment_count == 1) {
        Bits bits;
        std::string err;
        if (!payload_to_bits(f.payload, f.fill_bits, bits, err)) {
            rejected = true;
            return std::nullopt;
        }
        return bits;
    }

    auto it = groups_.find(key);
    if (it == groups_.end()) {
        if (groups_.size() >= limits_.max_groups) {
            rejected = true;
            return std::nullopt;
        }
        Group g;
        g.count = f.fragment_count;
        g.created = now;
        it = groups_.emplace(key, std::move(g)).first;
    }
    Group& g = it->second;
    if (g.count != f.fragment_count) {
        groups_.erase(it);
        rejected = true;
        return std::nullopt;
    }
    if (g.fragments.size() >= static_cast<std::size_t>(g.count) ||
        fragment_count() >= limits_.max_fragments) {
        groups_.erase(it);
        rejected = true;
        return std::nullopt;
    }
    auto [fit, inserted] = g.fragments.emplace(
        f.fragment_number, Fragment{f.payload, f.fill_bits});
    if (!inserted) {
        if (fit->second.payload != f.payload ||
            fit->second.fill_bits != f.fill_bits) {
            groups_.erase(it);
            rejected = true;
        }
        return std::nullopt;
    }
    if (static_cast<int>(g.fragments.size()) != g.count) return std::nullopt;

    std::string combined;
    int fill = 0;
    for (int i = 1; i <= g.count; ++i) {
        auto p = g.fragments.find(i);
        if (p == g.fragments.end()) break;
        combined += p->second.payload;
        fill = p->second.fill_bits;
    }
    groups_.erase(it);
    Bits bits;
    std::string err;
    if (!payload_to_bits(combined, fill, bits, err)) {
        rejected = true;
        return std::nullopt;
    }
    return bits;
}

void Reassembler::expire(Clock::time_point now) {
    for (auto it = groups_.begin(); it != groups_.end();) {
        if (now - it->second.created > std::chrono::seconds(5))
            it = groups_.erase(it);
        else
            ++it;
    }
}

std::size_t Reassembler::fragment_count() const {
    std::size_t n = 0;
    for (const auto& [k, g] : groups_) n += g.fragments.size();
    return n;
}

}  // namespace ais_maneuver228
