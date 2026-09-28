// Opt-in integration test against the real activity stack and mpv instance.
// Uses a public test video and an isolated WILIWILI_CONFIG_DIR. No OS input automation.
#include <borealis.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
#include <borealis/platforms/glfw/glfw_video.hpp>
#include "utils/activity_helper.hpp"
#include "activity/main_activity.hpp"
#include "activity/player_activity.hpp"
#include "activity/search_activity.hpp"
#include "view/video_view.hpp"
#include "view/mpv_core.hpp"
#include "view/recycling_grid.hpp"
namespace {
int phase = 0, result = 1;
int coveredPageDraws = 0;
int freedViews = 0, stressRound = 0, refreshes = 0;
RecyclingGrid* stressGrid = nullptr;
brls::View* lockedView = nullptr;
struct CountedView : brls::Box {
    bool spawn = false;
    ~CountedView() override {
        ++freedViews;
        if (spawn) {
            (new CountedView())->freeView();
            if (lockedView) lockedView->freeView();
        }
    }
};
void lifetimeRegression() {
    auto* focus = new brls::Box();
    focus->setFocusable(true);
    focus->setParentActivity(brls::Application::getActivitiesStack().back());
    brls::Application::giveFocus(focus);
    brls::Application::pushActivity(new brls::Activity(new brls::Box()), brls::TransitionAnimation::NONE);
    delete focus;
    brls::Application::popActivity(brls::TransitionAnimation::NONE);
    lockedView = new CountedView();
    lockedView->ptrLock();
    lockedView->freeView();
    std::vector<CountedView*> views{new CountedView(), new CountedView(), new CountedView()};
    std::sort(views.begin(), views.end(), std::greater<CountedView*>());
    views.front()->spawn = true;
    for (auto* v : views) v->freeView();
    views.back()->freeView(); // duplicate in an unsorted deletion queue
}
GLFWwindow* testWindow() {
    return dynamic_cast<brls::GLFWVideoContext*>(brls::Application::getPlatform()->getVideoContext())->getGLFWWindow();
}
void touchSample(int action, float logicalY) {
    auto* window = testWindow();
    auto frame = stressGrid->getFrame();
    double scale = brls::Application::windowScale /
        brls::Application::getPlatform()->getVideoContext()->getScaleFactor();
    double x = (frame.getMinX()+frame.getWidth()/2)*scale;
    double y = (frame.getMinY()+logicalY)*scale;
    if ((stressRound / 2) % 2 == 0) {
        auto callback = glfwSetTouchCallback(window, nullptr);
        glfwSetTouchCallback(window, callback);
        callback(window, 73, action, x, y);
    } else {
        auto cursor = glfwSetCursorPosCallback(window, nullptr);
        glfwSetCursorPosCallback(window, cursor);
        cursor(window, x, y);
        if (action != GLFW_MOVE) {
            auto button = glfwSetMouseButtonCallback(window, nullptr);
            glfwSetMouseButtonCallback(window, button);
            button(window, GLFW_MOUSE_BUTTON_LEFT, action, 0);
        }
    }
}

struct CoveredPage : brls::Box {
    void draw(NVGcontext*, float, float, float, float, brls::Style, brls::FrameContext*) override {
        ++coveredPageDraws;
    }
};
void insertCoveredPage() {
    coveredPageDraws = 0;
    brls::Application::pushActivity(new brls::Activity(new CoveredPage()), brls::TransitionAnimation::NONE);
}
bool clearStateRegression() {
#ifdef _WIN32
    glEnable(GL_SCISSOR_TEST);
    glScissor(0,0,1,1);
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    glDepthMask(GL_FALSE);
    glStencilMask(0);
    brls::Application::getPlatform()->getVideoContext()->clear(nvgRGB(73,119,181));
    glReadBuffer(GL_BACK);
    unsigned char pixel[4]{};
    glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    return std::abs((int)pixel[0]-73)<=1 && std::abs((int)pixel[1]-119)<=1 && std::abs((int)pixel[2]-181)<=1;
#else
    return true;
#endif
}
int64_t pausedAt = 0, playingAt = 0;
brls::Activity* retained = nullptr;
auto phaseStart = std::chrono::steady_clock::now();
void advance() {
    ++phase;
    phaseStart = std::chrono::steady_clock::now();
    brls::Logger::info("GO3 REGRESSION phase {}", phase);
}
bool check(bool ok, const char* message) {
    if (ok) return true;
    brls::Logger::error("GO3 REGRESSION FAIL phase {}: {}", phase, message);
    phase = -1;
    brls::Application::quit();
    return false;
}
}
int go3PlayerRegressionResult() { return result; }
void exitRegressionTick(const std::string& scenario) {
    if (phase < 0) return;
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()-phaseStart).count();
    if (!check(elapsed < 65,"exit scenario timed out")) return;
    if (elapsed < 2) return;
    auto pages = brls::Application::getActivitiesStack();
    if (pages.empty()) return;
    auto* top = pages.back();
    if (phase == 0) {
        if (!check(std::getenv("WILIWILI_CONFIG_DIR") != nullptr,"isolated profile required")) return;
        if (scenario != "home" && scenario != "home-key") {
            Intent::openBV("BV1JMec6xEPQ", 0, 0); advance(); return;
        }
        phase = 3;
    }
    if (phase == 1) {
        auto& mpv = MPVCore::instance();
        if (!mpv.isPlaying() || mpv.video_progress < 3) return;
        if (scenario == "background") Intent::backgroundPlayer();
        if (scenario == "fullscreen") dynamic_cast<VideoView*>(top->getView("video"))->setFullScreen(true);
        if (scenario == "paused-key") mpv.pause();
        advance(); return;
    }
    if (phase == 2) {
        if (scenario == "background" && !check(Intent::hasBackgroundPlayer(),"retained player missing on exit")) return;
        if (scenario == "paused-key" && !check(MPVCore::instance().isPaused(),"pause before exit failed")) return;
        phase = 3;
    }
    auto* target = scenario.find("key") != std::string::npos ? top->getContentView() :
        top->getView(dynamic_cast<MainActivity*>(top) ? "main/exit" : "video/app-exit");
    if (!check(target != nullptr,"exit button missing")) return;
    const bool keyboard = scenario.find("key") != std::string::npos;
    int key = brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_Q,
        brls::BRLS_KBD_MODIFIER_CTRL | brls::BRLS_KBD_MODIFIER_SHIFT);
    for (const auto& action : target->getActions()) {
        if (action->getType() != (keyboard ? brls::ACTION_KEYBOARD : brls::ACTION_GAMEPAD)) continue;
        if (action->getButton() != (keyboard ? key : int(brls::BUTTON_A))) continue;
        if (!check(action->getActionListener()(target),"exit action not consumed")) return;
        result = 0; phase = -1;
        brls::Logger::info("GO3 EXIT PASS: {}", scenario);
        return;
    }
    check(false,"exit action binding missing");
}
void pressKey(brls::BrlsKeyboardScancode key) {
    brls::Application::onKeyboardPressed(brls::BrlsKeyCombination(key), false);
}
// Opt-in scenario that drives the native Windows text input dialog.
std::string imeResult;
void imeRegressionTick() {
    if (phase < 0) return;
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()-phaseStart).count();
    if (!check(elapsed < 30,"ime phase timed out")) return;
    if (elapsed < 1) return;
    auto* window = testWindow();
    switch (phase) {
        case 0:
            if (!check(std::getenv("WILIWILI_CONFIG_DIR") != nullptr,"isolated profile required")) return;
            // 主窗口必须已经在首帧呈现后显示出来
            if (!check(glfwGetWindowAttrib(window, GLFW_VISIBLE) == GLFW_TRUE,"main window not shown after the first frame")) return;
            brls::Application::getImeManager()->openForText(
                [](const std::string& text) { imeResult = text; }, "回归测试", "", 32, "", 0);
            advance(); break;
        case 1: {
            HWND dialog = FindWindowW(L"WiliwiliTextInput", nullptr);
            if (!check(dialog != nullptr,"native input dialog missing")) return;
            if (!check(IsWindowVisible(dialog) == TRUE,"native input dialog not visible")) return;
            BYTE alpha = 0;
            if (!check(GetLayeredWindowAttributes(dialog, nullptr, &alpha, nullptr) &&
                       alpha == 255,"native input dialog was not revealed after painting")) return;
            HWND edit = FindWindowExW(dialog, nullptr, L"Edit", nullptr);
            if (!check(edit != nullptr,"native input edit control missing")) return;
            if (!check(GetFocus() == edit,"native input edit control not focused")) return;
            SetWindowTextW(edit, L"键盘测试");
            PostMessageW(dialog, WM_COMMAND, IDOK, 0);
            advance(); break;
        }
        case 2:
            if (!check(FindWindowW(L"WiliwiliTextInput", nullptr) == nullptr,"native input dialog was not closed")) return;
            if (!check(imeResult == "键盘测试","native input dialog returned the wrong text")) return;
            if (!check(IsWindowEnabled(glfwGetWin32Window(window)) == TRUE,"main window stayed disabled after the dialog")) return;
            result = 0; phase = -1;
            brls::Logger::info("GO3 IME PASS: dialog opened, painted, focused, submitted and released the main window");
            brls::Application::quit();
            break;
    }
}
// Opt-in scenario that drives the web-like player keys through the real view tree.
int64_t keyAt = 0;
brls::Activity* keyboardPlayerPage = nullptr;
void keyboardRegressionTick() {
    if (phase < 0) return;
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()-phaseStart).count();
    if (!check(elapsed < 65,"keyboard phase timed out")) return;
    if (elapsed < 0.3) return;
    auto pages = brls::Application::getActivitiesStack();
    if (pages.empty()) return;
    auto* top = pages.back();
    auto& mpv = MPVCore::instance();
    switch (phase) {
        case 0:
            if (!check(std::getenv("WILIWILI_CONFIG_DIR") != nullptr,"isolated profile required")) return;
            Intent::openBV("BV1JMec6xEPQ",0,0);
            advance(); break;
        case 1: {
            if (!mpv.isPlaying() || mpv.video_progress < 3) return;
            auto* video = dynamic_cast<VideoView*>(top->getView("video"));
            if (!check(video != nullptr,"player view missing")) return;
            keyboardPlayerPage = top;
            brls::Application::giveFocus(video);
            mpv.pause();
            keyAt = mpv.video_progress;
            pressKey(brls::BRLS_KBD_KEY_RIGHT);
            advance(); break;
        }
        case 2:
            // 右方向键：等待 400ms 延迟跳转生效，进度前进 5 秒
            if (mpv.video_progress < keyAt + 4) return;
            if (!check(mpv.video_progress <= keyAt + 7,"right arrow seek out of range")) return;
            pressKey(brls::BRLS_KBD_KEY_LEFT);
            advance(); break;
        case 3:
            // 左方向键：退回原位
            if (mpv.video_progress > keyAt + 1) return;
            if (!check(mpv.video_progress >= keyAt - 1,"left arrow seek out of range")) return;
            pressKey(brls::BRLS_KBD_KEY_M);
            advance(); break;
        case 4:
            if (mpv.volume != 0) return;
            if (!check(MPVCore::VIDEO_VOLUME > 0,"mute must not overwrite the saved volume")) return;
            pressKey(brls::BRLS_KBD_KEY_M);
            advance(); break;
        case 5: {
            if (mpv.volume == 0) return;
            if (!check(MPVCore::VIDEO_VOLUME == mpv.volume,"unmute did not restore the volume")) return;
            pressKey(brls::BRLS_KBD_KEY_5);
            advance(); break;
        }
        case 6: {
            double target = mpv.duration / 2;
            if (mpv.video_progress < target - 3) return;
            if (!check(std::abs(mpv.video_progress - target) <= 4,"digit key did not seek to 50%")) return;
            pressKey(brls::BRLS_KBD_KEY_F);
            advance(); break;
        }
        case 7: {
            if (pages.back() == keyboardPlayerPage) return;
            auto* fullscreen = dynamic_cast<VideoView*>(pages.back()->getView("video"));
            if (!check(fullscreen != nullptr,"fullscreen page missing")) return;
            if (!fullscreen->isFullscreen()) return;
            pressKey(brls::BRLS_KBD_KEY_F);
            advance(); break;
        }
        case 8:
            // F：再按一次退出全屏（退出是异步的，等到页面弹出为止）
            if (pages.back() != keyboardPlayerPage) return;
            result = 0; phase = -1;
            brls::Logger::info("GO3 KEYBOARD PASS: arrows seek, mute toggle, digit seek, fullscreen toggle");
            brls::Application::quit();
            break;
    }
}
void go3PlayerRegressionTick() {
    if (std::getenv("GO3_IME_SCENARIO")) { imeRegressionTick(); return; }
    if (std::getenv("GO3_KEYBOARD_SCENARIO")) { keyboardRegressionTick(); return; }
    if (const char* scenario = std::getenv("GO3_EXIT_SCENARIO")) { exitRegressionTick(scenario); return; }
    if (phase < 0) return;
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()-phaseStart).count();
    if (!check(elapsed < 65, "phase timed out")) return;
    if (elapsed < (phase >= 100 ? 0.18 : 2)) return;
    auto pages = brls::Application::getActivitiesStack();
    if (pages.empty()) return;
    auto* top = pages.back();
    auto& mpv = MPVCore::instance();
    switch (phase) {
        case 0:
            if (!std::getenv("WILIWILI_CONFIG_DIR")) { check(false,"isolated config is required"); return; }
            if (!check(clearStateRegression(),"frame clear inherited stale clipping/write masks")) return;
            {
                int width = brls::Application::windowWidth, height = brls::Application::windowHeight;
                brls::Application::onWindowResized(width+64, height+36);
                bool coherent = brls::Application::windowWidth == width+64 && brls::Application::windowHeight == height+36;
                brls::Application::onWindowResized(width, height);
                if (!check(coherent,"resize projection delayed behind framebuffer")) return;
            }
            lifetimeRegression();
            Intent::openBV("BV1JMec6xEPQ",0,0);
            advance(); break;
        case 1:
            if (lockedView) {
                if (!check(freedViews == 4,"deletion queue duplicate/nested destruction")) return;
                lockedView->ptrUnlock();
                lockedView = nullptr;
            }
            if (!mpv.isPlaying() || mpv.video_progress < 3) return;
            retained = top; mpv.pause(); advance(); break;
        case 2:
            if (!check(freedViews == 5,"locked view must be released exactly once")) return;
            if (!mpv.isPaused()) return;
            pausedAt = mpv.video_progress;
            Intent::backgroundPlayer(); advance(); break;
        case 3:
            if (!check(Intent::hasBackgroundPlayer() && dynamic_cast<MainActivity*>(top), "background must show main")) return;
            if (!check(mpv.isPaused() && mpv.video_progress == pausedAt,"paused state changed while browsing")) return;
            Intent::openSearch("中文测试"); advance(); break;
        case 4:
            if (!check(dynamic_cast<SearchActivity*>(top),"search did not open")) return;
            insertCoveredPage();
            Intent::restoreBackgroundPlayer(); advance(); break;
        case 5:
            if (!check(top == retained && !Intent::hasBackgroundPlayer(),"did not restore original activity")) return;
            if (!check(coveredPageDraws == 0,"covered page was rendered behind restored player")) return;
            if (!check(!top->isTranslucent() && !top->isHidden() && top->getContentView()->getAlpha() == 1.0f,"restored player must be opaque and visible")) return;
            if (!check(mpv.isPaused() && mpv.video_progress == pausedAt,"restore changed pause or position")) return;
            mpv.resume(); advance(); break;
        case 6:
            if (mpv.video_progress < pausedAt+3) return;
            dynamic_cast<VideoView*>(top->getView("video"))->setFullScreen(true);
            advance(); break;
        case 7:
            if (!check(top != retained && top->getView("video"),"fullscreen view missing")) return;
            playingAt = mpv.video_progress;
            brls::Application::getPlatform()->minimizeWindow();
            phase = 69; advance(); break;
        case 70:
            if (!check(brls::Application::getPlatform()->isWindowMinimized() && mpv.video_progress > playingAt,"fullscreen minimized playback stopped")) return;
            brls::Application::getPlatform()->restoreWindow(); advance(); break;
        case 71:
            playingAt = mpv.video_progress;
            Intent::backgroundPlayer(); phase = 7; advance(); break;
        case 8: {
            if (!check(Intent::hasBackgroundPlayer() && dynamic_cast<MainActivity*>(top),"fullscreen background failed")) return;
            if (!check(mpv.isPlaying() && mpv.video_progress > playingAt,"playback stopped while browsing")) return;
            auto* grid = dynamic_cast<RecyclingGrid*>(top->getView("home/recommends/recyclingGrid"));
            if (!check(grid != nullptr,"recommendations grid missing")) return;
            grid->refresh(); Intent::openSearch("中文测试"); advance(); break;
        }
        case 9:
            if (!check(dynamic_cast<SearchActivity*>(top) && mpv.isPlaying(),"search interrupted playback")) return;
            insertCoveredPage();
            Intent::restoreBackgroundPlayer(); advance(); break;
        case 10:
            if (!check(top == retained && mpv.isPlaying(),"playing restore failed")) return;
            if (!check(coveredPageDraws == 0,"covered page was rendered after fullscreen restore")) return;
            if (!check(!top->isTranslucent() && top->getContentView()->getAlpha() == 1.0f,"fullscreen background restore leaked transparency")) return;
            playingAt = mpv.video_progress;
            brls::Application::getPlatform()->minimizeWindow(); advance(); break;
        case 11:
            if (!check(brls::Application::getPlatform()->isWindowMinimized() && mpv.video_progress > playingAt,"minimized playback stopped")) return;
            brls::Application::getPlatform()->restoreWindow(); advance(); break;
        case 12:
            Intent::backgroundPlayer(); advance(); break;
        case 13:
            if (!check(Intent::hasBackgroundPlayer(),"second background failed")) return;
            stressGrid = dynamic_cast<RecyclingGrid*>(top->getView("home/recommends/recyclingGrid"));
            if (!check(stressGrid != nullptr,"stress grid missing")) return;
            // Real homepage layout/cards and recycler, with deterministic refresh counting.
            // The earlier phase tests the actual network refresh callback.
            stressGrid->setRefreshAction([] { ++refreshes; stressGrid->reloadData(); });
            phase = 99; advance(); break;
        case 100:
            if (!check(dynamic_cast<MainActivity*>(top),"stress main missing")) return;
            glfwFocusWindow(testWindow());
            stressGrid->setContentOffsetY(0, false);
            advance(); break;
        case 101:
            touchSample(GLFW_PRESS, 35); advance(); break;
        case 102:
            touchSample(GLFW_MOVE, 215); advance(); break;
        case 103:
            // Alternate completing a refresh and cancelling it by reversing the drag.
            if (stressRound % 2) touchSample(GLFW_MOVE, 45);
            advance(); break;
        case 104:
            touchSample(GLFW_RELEASE, stressRound % 2 ? 45 : 215); advance(); break;
        case 105:
            if (!check(refreshes == stressRound/2+1,"touch/mouse pull/reversal refresh count mismatch")) return;
            touchSample(GLFW_PRESS, 35); advance(); break;
        case 106:
            // Minimize with an active contact and no release event.
            brls::Application::getPlatform()->minimizeWindow(); advance(); break;
        case 107:
            brls::Application::getPlatform()->restoreWindow(); advance(); break;
        case 108:
            Intent::restoreBackgroundPlayer(); advance(); break;
        case 109:
            if (!check(top == retained && !top->isTranslucent(),"stress player restore failed")) return;
            brls::Application::getPlatform()->minimizeWindow(); advance(); break;
        case 110:
            if (!check(brls::Application::getPlatform()->isWindowMinimized(),"stress minimize failed")) return;
            brls::Application::getPlatform()->restoreWindow(); advance(); break;
        case 111:
            if (stressRound % 3 == 0) dynamic_cast<VideoView*>(top->getView("video"))->setFullScreen(true);
            Intent::backgroundPlayer(); Intent::backgroundPlayer(); advance(); break;
        case 112:
            if (!check(Intent::hasBackgroundPlayer() && dynamic_cast<MainActivity*>(top),"stress background failed")) return;
            if (!check(refreshes == stressRound/2+1,"aborted contact caused an extra refresh")) return;
            brls::Logger::info("GO3 STRESS round {} refreshes {}", stressRound+1, refreshes);
            if (++stressRound < 30) { phase = 99; advance(); break; }
            if (!check(mpv.isPlaying(),"stress background playback stopped")) return;
            phase = 19; advance(); break;
        case 20:
            Intent::openBV("BV1a44y62Ew1",0,0); phase = 13; advance(); break;
        case 14:
            if (!mpv.isPlaying() || mpv.video_progress < 2) return;
            if (!check(!Intent::hasBackgroundPlayer() && dynamic_cast<BasePlayerActivity*>(top),"new video did not replace retained player")) return;
            Intent::backgroundPlayer(); advance(); break;
        case 15:
            if (!check(Intent::hasBackgroundPlayer(),"cleanup player missing")) return;
            Intent::discardBackgroundPlayer(); advance(); break;
        case 16:
            if (!check(!Intent::hasBackgroundPlayer() && !mpv.isPlaying(),"stop retained player failed")) return;
            {
                auto* translucent = new brls::Activity(new brls::Box());
                brls::Application::pushActivity(translucent, brls::TransitionAnimation::NONE);
                translucent->setInFadeAnimation(true);
                brls::Application::popActivity(brls::TransitionAnimation::NONE, [] {}, false);
                bool preserved = translucent->isTranslucent();
                brls::Application::pushActivity(translucent, brls::TransitionAnimation::NONE);
                preserved = preserved && translucent->isTranslucent();
                brls::Application::popActivity(brls::TransitionAnimation::NONE);
                if (!check(preserved,"intentional modal translucency must be preserved")) return;
            }
            result = 0; phase = -1;
            brls::Logger::info("GO3 REGRESSION PASS: 30 touch/minimize/background stress rounds, focus lifetime, deletion queue, opaque restore, covered-page rendering, intentional translucency, GL clear, pause, browse, search, refresh, fullscreen, minimize, replace, stop");
            brls::Application::quit(); break;
    }
}
