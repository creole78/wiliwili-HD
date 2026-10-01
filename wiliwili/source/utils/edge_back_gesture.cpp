//
// Android style edge swipe back for the Windows tablet build.
//

#include <borealis/core/application.hpp>
#include <borealis/core/activity.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/core/view.hpp>

#include <cmath>

#include "utils/edge_back_gesture.hpp"

namespace {
// The gesture belongs to the screen, not to the view it is attached to: the
// video area of the detail page is not as wide as the window.
float screenWidth(brls::View* view) {
    auto stack = brls::Application::getActivitiesStack();
    if (!stack.empty()) {
        brls::View* content = stack.back()->getContentView();
        if (content && content->getWidth() > 0) return content->getWidth();
    }
    return view ? view->getWidth() : 0.0f;
}
}  // namespace

brls::GestureState EdgeBackGestureRecognizer::recognitionLoop(brls::TouchState touch, brls::MouseState mouse,
                                                              brls::View* view, brls::Sound* soundToPlay) {
    if (!enabled) return brls::GestureState::FAILED;

    brls::TouchPhase phase = touch.phase;
    brls::Point position   = touch.position;
    int finger             = touch.fingerId;

    if (phase == brls::TouchPhase::NONE) {
        position = mouse.position;
        phase    = mouse.leftButton;
        finger   = 0;
    }

    // Once the gesture was interrupted or rejected it stays that way until the
    // finger is lifted, otherwise a failed swipe could fire during scrolling.
    if (phase != brls::TouchPhase::START &&
        (this->state == brls::GestureState::INTERRUPTED || this->state == brls::GestureState::FAILED)) {
        return this->state;
    }

    switch (phase) {
        case brls::TouchPhase::START:
            this->fingerId = finger;
            this->edge.begin(position.x, screenWidth(view), edgeZone);
            this->state = brls::GestureState::UNSURE;
            break;
        case brls::TouchPhase::STAY:
        case brls::TouchPhase::END:
            if (finger != this->fingerId) {
                this->state = brls::GestureState::FAILED;
                break;
            }
            if (this->state == brls::GestureState::UNSURE) {
                const float moved  = std::abs(position.x - this->edge.originX);
                const float inward = this->edge.fromLeft ? position.x - this->edge.originX
                                                        : this->edge.originX - position.x;
                if (moved > deadZone) {
                    this->state = (this->edge.active && inward > 0) ? brls::GestureState::START
                                                                    : brls::GestureState::FAILED;
                }
            } else if (this->state == brls::GestureState::START || this->state == brls::GestureState::STAY) {
                this->state = phase == brls::TouchPhase::END ? brls::GestureState::END : brls::GestureState::STAY;
            }

            if (this->state == brls::GestureState::START || this->state == brls::GestureState::STAY ||
                this->state == brls::GestureState::END) {
                this->edge.move(position.x);
            }

            if (this->state == brls::GestureState::END && this->edge.release() && this->action) {
                // Deferred: going back pops activities and must not run in the
                // middle of the touch dispatch loop.
                auto action = this->action;
                brls::sync([action] { action(); });
            }
            break;
        case brls::TouchPhase::NONE:
            this->state = brls::GestureState::FAILED;
            this->edge.cancel();
            break;
    }

    return this->state;
}
