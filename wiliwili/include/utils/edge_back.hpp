#pragma once

// Android style edge back: a drag may only trigger "back" when it starts inside
// the screen edge zone and then moves towards the inside of the screen.
// Kept free of framework types so the rules can be unit tested.
class EdgeBackState {
public:
    // A back gesture is only armed by a touch that starts near a screen edge.
    void begin(float positionX, float screenWidth, float zone) {
        active   = positionX <= zone || positionX >= screenWidth - zone;
        fromLeft = positionX <= zone;
        originX  = positionX;
        distance = 0;
    }

    // Distance travelled towards the inside of the screen, negative when the
    // finger moves back towards the edge (which cancels the gesture).
    void move(float positionX) {
        distance = active ? (fromLeft ? positionX - originX : originX - positionX) : 0;
    }

    bool armed() const { return active && distance >= threshold; }

    bool release() {
        bool fired = armed();
        cancel();
        return fired;
    }

    void cancel() {
        active   = false;
        distance = 0;
    }

    // Logical pixels the finger has to travel inwards before "back" fires.
    static constexpr float threshold = 80.0f;

    bool active    = false;
    bool fromLeft  = false;
    float originX  = 0;
    float distance = 0;
};
