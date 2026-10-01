#pragma once

#include <borealis/core/gesture.hpp>

#include <functional>

#include "utils/edge_back.hpp"

// Swipe inwards from the left or right screen edge to go back, like Android.
// Attach it to a view that covers the page: touches that start away from the
// edge are ignored, so the video seek gesture and list scrolling keep working.
class EdgeBackGestureRecognizer : public brls::GestureRecognizer {
public:
    // Width of the screen border where a back gesture may start.
    static constexpr float edgeZone = 24.0f;
    // Movement needed before the gesture is recognised as horizontal.
    static constexpr float deadZone = 12.0f;

    explicit EdgeBackGestureRecognizer(std::function<void()> action) : action(std::move(action)) {}

    brls::GestureState recognitionLoop(brls::TouchState touch, brls::MouseState mouse, brls::View* view,
                                       brls::Sound* soundToPlay) override;

private:
    std::function<void()> action;
    EdgeBackState edge;
    int fingerId = -1;
};
