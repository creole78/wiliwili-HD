/*
    Copyright 2020-2021 natinusala

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/core/thread.hpp>

#include "activity/main_activity.hpp"
#include "utils/activity_helper.hpp"
#include "utils/dialog_helper.hpp"
#include "utils/edge_back_gesture.hpp"
#include "view/custom_button.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/svg_image.hpp"

using namespace brls::literals;

MainActivity::~MainActivity() { brls::Logger::debug("del MainActivity"); }

void MainActivity::onResume() {
    auto* button = this->getView("main/resume");
    if (button) button->setVisibility(Intent::hasBackgroundPlayer() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

void MainActivity::onContentAvailable() {
    auto* resume = this->getView("main/resume");
    resume->registerClickAction([](brls::View*) {
        brls::sync([] { Intent::restoreBackgroundPlayer(); });
        return true;
    });
    resume->addGestureRecognizer(new brls::TapGestureRecognizer(resume));
    onResume();

    // Swap an icon button to its highlighted glyph while focused.
    auto bindIconFocus = [](brls::View* button, const std::string& normal, const std::string& focused) {
        auto* custom = dynamic_cast<CustomButton*>(button);
        if (!custom) return;
        custom->getFocusEvent()->subscribe([custom, normal, focused](bool value) {
            if (custom->getChildren().empty()) return;
            auto* image = dynamic_cast<SVGImage*>(custom->getChildren()[0]);
            if (image) image->setImageFromSVGRes(value ? focused : normal);
        });
    };
    bindIconFocus(resume, "svg/ico-back.svg", "svg/ico-back-activate.svg");

#ifdef _WIN32
    auto* exitButton = this->getView("main/exit");
    exitButton->registerClickAction([](brls::View*) { Intent::quitApplication(); return true; });
    exitButton->addGestureRecognizer(new brls::TapGestureRecognizer(exitButton));
    bindIconFocus(exitButton, "svg/ico-close.svg", "svg/ico-close-activate.svg");

    // Android style edge swipe: swiping in from a screen edge asks before quitting.
    this->getContentView()->addGestureRecognizer(new EdgeBackGestureRecognizer([] {
        brls::sync([] {
            DialogHelper::showCancelableDialog("wiliwili/home/common/quit_confirm"_i18n,
                                               [] { Intent::quitApplication(); });
        });
    }));

    this->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_Q,
        brls::BRLS_KBD_MODIFIER_CTRL | brls::BRLS_KBD_MODIFIER_SHIFT),
        [](brls::View*) { Intent::quitApplication(); return true; });
    homeBtn->registerClickAction([](brls::View*) {
        brls::Application::getPlatform()->minimizeWindow();
        return true;
    });
    homeBtn->addGestureRecognizer(new brls::TapGestureRecognizer(homeBtn));
    bindIconFocus(homeBtn, "svg/ico-minimize.svg", "svg/ico-minimize-activate.svg");
    homeBtn->setCustomNavigation([this](brls::FocusDirection direction) -> brls::View* {
        if (direction == brls::FocusDirection::RIGHT) return tabFrame->getActiveTab();
        if (direction == brls::FocusDirection::UP) return tabFrame->getSidebar();
        return nullptr;
    });
#else
    homeBtn->setVisibility(brls::Visibility::GONE);
    this->getView("main/exit")->setVisibility(brls::Visibility::GONE);
#endif
    this->registerAction(
        "Settings", brls::ControllerButton::BUTTON_BACK,
        [](brls::View* view) -> bool {
            Intent::openSetting();
            return true;
        },
        true);

    this->registerAction(
        "Settings", brls::ControllerButton::BUTTON_START,
        [](brls::View* view) -> bool {
            Intent::openSetting();
            return true;
        },
        true);

    this->settingBtn->registerClickAction([](brls::View* view) -> bool {
        Intent::openSetting();
        return true;
    });

    this->settingBtn->getFocusEvent()->subscribe([this](bool value) {
        SVGImage* image = dynamic_cast<SVGImage*>(this->settingBtn->getChildren()[0]);
        if (!image) return;
        if (value) {
            image->setImageFromSVGRes("svg/ico-setting-activate.svg");
        } else {
            image->setImageFromSVGRes("svg/ico-setting.svg");
        }
    });

    this->inboxBtn->setCustomNavigation([this](brls::FocusDirection direction) {
        if (tabFrame->getSideBarPosition() == AutoTabBarPosition::LEFT) {
            if (direction == brls::FocusDirection::RIGHT) {
                return (brls::View*)this->tabFrame->getActiveTab();
            } else if (direction == brls::FocusDirection::UP) {
                #ifdef _WIN32
                return (brls::View*)this->homeBtn;
#else
                return (brls::View*)this->tabFrame->getSidebar();
#endif
            }
        } else if (tabFrame->getSideBarPosition() == AutoTabBarPosition::TOP) {
            if (direction == brls::FocusDirection::DOWN) {
                return (brls::View*)this->tabFrame->getActiveTab();
            } else if (direction == brls::FocusDirection::LEFT) {
                return (brls::View*)this->tabFrame->getSidebar();
            }
        }
        return (brls::View*)nullptr;
    });
    this->settingBtn->setCustomNavigation([this](brls::FocusDirection direction) {
        if (tabFrame->getSideBarPosition() == AutoTabBarPosition::LEFT) {
            if (direction == brls::FocusDirection::RIGHT) {
                return (brls::View*)this->tabFrame->getActiveTab();
            } else if (direction == brls::FocusDirection::UP) {
                return (brls::View*)this->inboxBtn;
            }
        } else if (tabFrame->getSideBarPosition() == AutoTabBarPosition::TOP) {
            if (direction == brls::FocusDirection::DOWN) {
                return (brls::View*)this->tabFrame->getActiveTab();
            } else if (direction == brls::FocusDirection::LEFT) {
                return (brls::View*)this->inboxBtn;
            }
        }
        return (brls::View*)nullptr;
    });
    this->settingBtn->addGestureRecognizer(new brls::TapGestureRecognizer(this->settingBtn));

    this->inboxBtn->registerClickAction([](brls::View* view) -> bool {
        if (DialogHelper::checkLogin()) Intent::openInbox();
        return true;
    });

    this->inboxBtn->getFocusEvent()->subscribe([this](bool value) {
        SVGImage* image = dynamic_cast<SVGImage*>(this->inboxBtn->getChildren()[0]);
        if (!image) return;
        if (value) {
            image->setImageFromSVGRes("svg/ico-inbox-activate.svg");
        } else {
            image->setImageFromSVGRes("svg/ico-inbox.svg");
        }
    });
    this->inboxBtn->addGestureRecognizer(new brls::TapGestureRecognizer(this->inboxBtn));
}
