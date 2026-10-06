#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ais_maneuver228 {

using Clock = std::chrono::steady_clock;

struct DecodedMessage {
    int type = 0;
    std::uint32_t mmsi = 0;

    bool has_lat = false;
    bool has_lon = false;
    bool has_sog = false;
    bool has_cog = false;
    double lat = 0.0;
    double lon = 0.0;
    double sog = 0.0;  // knots
    double cog = 0.0;  // degrees from north, clockwise

    std::string name;
};

// Returns true when the "*HH" XOR checksum matches the sentence body.
bool verify_checksum(const std::string& sentence);

struct SentenceFields {
    int fragment_count = 0;
    int fragment_number = 0;
    int sequence_id = -1;  // -1 means empty
    char channel = 0;
    std::string payload;
    int fill_bits = 0;
};

// Parses a single !AIVDM sentence, validating fields and checksum.
bool parse_sentence(const std::string& line, SentenceFields& out, std::string& error);

// Validates the six-bit payload alphabet and fill bits (0..5).
bool payload_to_bits(const std::string& payload, int fill_bits,
                     std::vector<int>& bits, std::string& error);

// Decodes a complete six-bit bit stream. Only types 1/2/3 and 5 are accepted;
// other supported-grammar messages return false with ignored=true.
bool decode_bits(const std::vector<int>& bits, DecodedMessage& out,
                 bool& ignored, std::string& error);

struct ReassemblerLimits {
    std::size_t max_fragments = 4096;
    std::size_t max_groups = 2048;
};

// Reassembles multi-sentence messages. State is isolated by sender endpoint,
// sequence id and VHF channel.
class Reassembler {
public:
    explicit Reassembler(ReassemblerLimits limits = {}) : limits_(limits) {}

    // Feeds one sentence. Returns a complete payload bit stream when a group
    // completes; std::nullopt otherwise. "rejected" is set when limits or a
    // conflict force the group to be dropped.
    std::optional<std::vector<int>> feed(const std::string& sender_key,
                                         const SentenceFields& f,
                                         Clock::time_point now,
                                         bool& rejected);

    void expire(Clock::time_point now);
    std::size_t group_count() const { return groups_.size(); }
    std::size_t fragment_count() const;

private:
    struct Fragment {
        std::string payload;
        int fill_bits = 0;
    };
    struct Group {
        int count = 0;
        std::unordered_map<int, Fragment> fragments;
        Clock::time_point created;
    };

    ReassemblerLimits limits_;
    std::unordered_map<std::string, Group> groups_;
};

}  // namespace ais_maneuver228
