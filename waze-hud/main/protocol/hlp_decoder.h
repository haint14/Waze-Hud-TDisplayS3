#pragma once

#include "cJSON.h"
#include "state/hud_state.h"

namespace waze_hud {

class HlpDecoder {
public:
    bool handleHi(const cJSON *root, HudState &state);
    bool decodeState(const cJSON *root, HudState &state);
    void resetSession();

private:
    uint32_t session_{0};
    uint32_t lastTimestamp_{0};
    bool haveTimestamp_{false};
    std::array<LaneState, kMaxLanes> cachedLanes_{};
    uint8_t cachedLaneCount_{0};
    int64_t laneHoldAccumulatedMovingMs_{0};
    int64_t laneLastCheckMs_{0};
};

}  // namespace waze_hud
