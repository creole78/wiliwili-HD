#pragma once
#include <algorithm>

// A refresh can only be armed by a new drag starting at the top of the list.
class PullRefreshState {
public:
    void begin(bool eligible) { active = eligible; distance = 0; }
    void move(float delta) { distance = active ? std::min(112.0f, std::max(0.0f, delta * 0.5f)) : 0; }
    bool armed() const { return active && distance >= 72; }
    bool release() { bool refresh = armed(); cancel(); return refresh; }
    void cancel() { active = false; distance = 0; }
    bool active = false;
    float distance = 0;
    float originY = 0;
};
