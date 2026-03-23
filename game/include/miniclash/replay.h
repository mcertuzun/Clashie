#pragma once

#include "miniclash/simulation.h"
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace miniclash {

// Binary replay file format header
struct ReplayHeader {
    uint32_t magic;              // 'PREP' = 0x50455250
    uint16_t version;            // Replay format version
    uint16_t engine_version;     // Engine version that produced this replay
    uint32_t seed;               // RNG seed used for the simulation
    uint32_t tick_count;         // Total ticks in the replay
    uint32_t base_layout_hash;   // Hash of the base layout for verification
    uint32_t final_state_hash;   // Hash of final simulation state for verification
    uint32_t input_count;        // Number of TickInput entries stored
};

static constexpr uint32_t REPLAY_MAGIC   = 0x50455250; // 'PREP'
static constexpr uint16_t REPLAY_VERSION = 1;

// Full replay file: header + recorded inputs
struct ReplayFile {
    ReplayHeader header;
    std::vector<TickInput> inputs; // Only ticks that have events (count > 0)
};

// Records inputs during live gameplay for later saving
class ReplayRecorder {
public:
    ReplayRecorder();

    void set_seed(uint32_t seed);
    void set_engine_version(uint16_t version);
    void set_base_hash(uint32_t hash);

    // Record a tick's input. Only stores if input.count > 0.
    void record_tick(const TickInput& input);

    // Called when the attack ends to stamp the final hash and tick count.
    void finalize(uint32_t final_hash, uint32_t tick_count);

    // Write the replay to a binary file. Returns true on success.
    bool save_to_file(const char* path) const;

    // Access recorded data
    const std::vector<TickInput>& get_recorded_inputs() const { return m_inputs; }
    const ReplayHeader& header() const { return m_header; }

private:
    ReplayHeader m_header;
    std::vector<TickInput> m_inputs;
};

// Plays back a recorded replay
class ReplayPlayer {
public:
    ReplayPlayer();

    // Load replay from a binary file. Returns true on success.
    bool load_from_file(const char* path);

    // Get the TickInput for a given tick. Returns an empty TickInput if no
    // inputs were recorded for that tick.
    TickInput get_tick_input(uint32_t tick) const;

    // Total number of ticks in the replay.
    uint32_t tick_count() const { return m_header.tick_count; }

    // Seed used in the recorded simulation.
    uint32_t seed() const { return m_header.seed; }

    // Base layout hash stored in the replay.
    uint32_t base_layout_hash() const { return m_header.base_layout_hash; }

    // Verify the computed final state hash against the stored one.
    // Returns true if they match.
    bool verify(uint32_t computed_hash) const;

    // Set playback speed multiplier (0.5, 1.0, 2.0, 4.0, etc.).
    // Controls how many simulation ticks to advance per real-time frame.
    void set_speed(float multiplier);
    float speed() const { return m_speed; }

    // Access the full header
    const ReplayHeader& header() const { return m_header; }

private:
    ReplayHeader m_header;
    // Map from tick number -> index into m_inputs for O(1) lookup
    std::unordered_map<uint32_t, uint32_t> m_tick_map;
    std::vector<TickInput> m_inputs;
    float m_speed;
};

} // namespace miniclash
