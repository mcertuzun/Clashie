#include "miniclash/replay.h"
#include "pebble/platform/platform.h"
#include <cstdio>
#include <cstring>

namespace miniclash {

// ─────────────────────────────────────────────────────────
// ReplayRecorder
// ─────────────────────────────────────────────────────────

ReplayRecorder::ReplayRecorder() {
    std::memset(&m_header, 0, sizeof(m_header));
    m_header.magic   = REPLAY_MAGIC;
    m_header.version = REPLAY_VERSION;
}

void ReplayRecorder::set_seed(uint32_t seed) {
    m_header.seed = seed;
}

void ReplayRecorder::set_engine_version(uint16_t version) {
    m_header.engine_version = version;
}

void ReplayRecorder::set_base_hash(uint32_t hash) {
    m_header.base_layout_hash = hash;
}

void ReplayRecorder::record_tick(const TickInput& input) {
    if (input.count == 0) return;
    m_inputs.push_back(input);
}

void ReplayRecorder::finalize(uint32_t final_hash, uint32_t tick_count) {
    m_header.final_state_hash = final_hash;
    m_header.tick_count       = tick_count;
    m_header.input_count      = static_cast<uint32_t>(m_inputs.size());
}

bool ReplayRecorder::save_to_file(const char* path) const {
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        pebble::platform::log_error("ReplayRecorder: failed to open '%s' for writing", path);
        return false;
    }

    // Write header
    if (std::fwrite(&m_header, sizeof(ReplayHeader), 1, f) != 1) {
        pebble::platform::log_error("ReplayRecorder: failed to write header");
        std::fclose(f);
        return false;
    }

    // Write each TickInput entry:
    //   uint32_t tick
    //   uint8_t  count
    //   InputEvent events[count]   (only the used entries)
    for (const auto& ti : m_inputs) {
        uint32_t tick = ti.tick;
        uint8_t  count = ti.count;

        if (std::fwrite(&tick, sizeof(tick), 1, f) != 1)   goto write_error;
        if (std::fwrite(&count, sizeof(count), 1, f) != 1) goto write_error;

        for (uint8_t i = 0; i < count; ++i) {
            const InputEvent& ev = ti.events[i];
            // Write each field individually to avoid padding issues
            if (std::fwrite(&ev.type,      sizeof(ev.type),      1, f) != 1) goto write_error;
            if (std::fwrite(&ev.troop_type, sizeof(ev.troop_type), 1, f) != 1) goto write_error;
            if (std::fwrite(&ev.world_x,   sizeof(ev.world_x),   1, f) != 1) goto write_error;
            if (std::fwrite(&ev.world_y,   sizeof(ev.world_y),   1, f) != 1) goto write_error;
        }
    }

    std::fclose(f);
    pebble::platform::log_info("Replay saved: %u ticks, %u input records -> '%s'",
                               m_header.tick_count, m_header.input_count, path);
    return true;

write_error:
    pebble::platform::log_error("ReplayRecorder: write error");
    std::fclose(f);
    return false;
}

// ─────────────────────────────────────────────────────────
// ReplayPlayer
// ─────────────────────────────────────────────────────────

ReplayPlayer::ReplayPlayer()
    : m_speed(1.0f) {
    std::memset(&m_header, 0, sizeof(m_header));
}

bool ReplayPlayer::load_from_file(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        pebble::platform::log_error("ReplayPlayer: failed to open '%s'", path);
        return false;
    }

    // Read header
    if (std::fread(&m_header, sizeof(ReplayHeader), 1, f) != 1) {
        pebble::platform::log_error("ReplayPlayer: failed to read header");
        std::fclose(f);
        return false;
    }

    // Validate magic
    if (m_header.magic != REPLAY_MAGIC) {
        pebble::platform::log_error("ReplayPlayer: bad magic 0x%08X (expected 0x%08X)",
                                    m_header.magic, REPLAY_MAGIC);
        std::fclose(f);
        return false;
    }

    // Validate version
    if (m_header.version > REPLAY_VERSION) {
        pebble::platform::log_error("ReplayPlayer: unsupported version %u (max %u)",
                                    m_header.version, REPLAY_VERSION);
        std::fclose(f);
        return false;
    }

    // Read input records
    m_inputs.clear();
    m_tick_map.clear();
    m_inputs.reserve(m_header.input_count);

    for (uint32_t r = 0; r < m_header.input_count; ++r) {
        TickInput ti;
        std::memset(&ti, 0, sizeof(ti));

        uint32_t tick  = 0;
        uint8_t  count = 0;

        if (std::fread(&tick, sizeof(tick), 1, f) != 1)   goto read_error;
        if (std::fread(&count, sizeof(count), 1, f) != 1) goto read_error;

        ti.tick  = tick;
        ti.count = count;

        for (uint8_t i = 0; i < count; ++i) {
            InputEvent& ev = ti.events[i];
            if (std::fread(&ev.type,       sizeof(ev.type),       1, f) != 1) goto read_error;
            if (std::fread(&ev.troop_type, sizeof(ev.troop_type), 1, f) != 1) goto read_error;
            if (std::fread(&ev.world_x,    sizeof(ev.world_x),    1, f) != 1) goto read_error;
            if (std::fread(&ev.world_y,    sizeof(ev.world_y),    1, f) != 1) goto read_error;
        }

        uint32_t idx = static_cast<uint32_t>(m_inputs.size());
        m_tick_map[tick] = idx;
        m_inputs.push_back(ti);
    }

    std::fclose(f);
    pebble::platform::log_info("Replay loaded: %u ticks, %u input records from '%s'",
                               m_header.tick_count, m_header.input_count, path);
    return true;

read_error:
    pebble::platform::log_error("ReplayPlayer: read error at record %u",
                                static_cast<uint32_t>(m_inputs.size()));
    m_inputs.clear();
    m_tick_map.clear();
    std::fclose(f);
    return false;
}

TickInput ReplayPlayer::get_tick_input(uint32_t tick) const {
    auto it = m_tick_map.find(tick);
    if (it != m_tick_map.end()) {
        return m_inputs[it->second];
    }

    // No inputs for this tick — return empty
    TickInput empty;
    empty.tick  = tick;
    empty.count = 0;
    return empty;
}

bool ReplayPlayer::verify(uint32_t computed_hash) const {
    bool match = (computed_hash == m_header.final_state_hash);
    if (!match) {
        pebble::platform::log_error("Replay verification FAILED: computed 0x%08X != stored 0x%08X",
                                    computed_hash, m_header.final_state_hash);
    } else {
        pebble::platform::log_info("Replay verification OK (hash 0x%08X)", computed_hash);
    }
    return match;
}

void ReplayPlayer::set_speed(float multiplier) {
    // Clamp to reasonable range
    if (multiplier < 0.25f) multiplier = 0.25f;
    if (multiplier > 8.0f)  multiplier = 8.0f;
    m_speed = multiplier;
}

} // namespace miniclash
