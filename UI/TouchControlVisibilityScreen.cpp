// Copyright (c) 2013- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Common/System/Display.h"
#include "Common/System/Request.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"
#include "Common/UI/TabHolder.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/ScreenManager.h"

#include "Core/Config.h"
#include "Core/HW/BackgroundPlayer.h"
#include "Common/File/AndroidContentURI.h"
#include "Common/File/AndroidStorage.h"
#include "UI/TouchControlVisibilityScreen.h"
#include "UI/CustomButtonMappingScreen.h"
#include "UI/GamepadEmu.h"


static const int leftColumnWidth = 140;

class PSPButtonModePopup : public UI::PopupScreen {
public:
        PSPButtonModePopup(std::string_view title, bool *toggle, bool *repeat)
                : UI::PopupScreen(std::string(title), "OK", ""), toggle_(toggle), repeat_(repeat) {}

		const char *tag() const override { return "PSPButtonModePopup"; }

        void CreatePopupContents(UI::ViewGroup *parent) override {
                auto co = GetI18NCategory(I18NCat::CONTROLS);
                parent->Add(new UI::CheckBox(toggle_, co->T("Toggle Mode")));
                parent->Add(new UI::CheckBox(repeat_, co->T("Repeat Mode")));
        }

        void OnCompleted(DialogResult result) override {
				g_Config.Save("PSPButtonModePopup");
		}

private:
        bool *toggle_;
        bool *repeat_;
};

// Background video picker: uses the same SAF flow as ISO browsing.
// When user clicks "Animation Background", it opens the system folder picker
// to select the PSP/BACKGROUND/ folder. The selected path is then passed
// to BackgroundPlayer via LoadFromBackgroundFolder.
class BackgroundVideoSelectPopup : public UI::PopupScreen {
public:
    BackgroundVideoSelectPopup()
        : UI::PopupScreen("Background Videos", "OK", "") {}

    const char *tag() const override { return "BackgroundVideoSelectPopup"; }

    void CreatePopupContents(UI::ViewGroup *parent) override {
        using namespace UI;
        std::string folder = g_Config.sBackgroundVideoPath;
        if (folder.empty()) {
            folder = g_Config.memStickDirectory.ToString();
        }

        parent->Add(new ItemHeader("Folder"));
        std::string visualFolder = folder;
        if (startsWith(visualFolder, "content://")) {
            AndroidContentURI uri(visualFolder);
            visualFolder = uri.ToVisualString();
        }
        Choice *folderDisplay = parent->Add(new Choice(visualFolder.empty() ? "(Not set, using default memstick)" : visualFolder, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
        folderDisplay->SetEnabled(false);

        Choice *browseBtn = parent->Add(new Choice("Browse..."));
        browseBtn->OnClick.Add([=](UI::EventParams &e) {
            System_BrowseForFolder(
                GetRequesterToken(),
                "Select Background Videos folder (usually PSP/BACKGROUND)",
                Path(folder),
                [=](std::string_view returnValue, int) {
                    std::string selectedFolder = std::string(returnValue);
                    if (selectedFolder != g_Config.sBackgroundVideoPath) {
                        g_Config.sBackgroundVideoPath = selectedFolder;
                        g_Config.Save("BackgroundVideoSelectPopup");
                        RecreateViews();
                    }
                }
            );
        });

        parent->Add(new ItemHeader("Playlist"));

        ScrollView *scroll = parent->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
        LinearLayout *list = scroll->Add(new LinearLayout(ORIENT_VERTICAL));

        // Get file list to show playlist.
        std::vector<File::FileInfo> fileList;
        std::string dirUri = folder;
#ifdef __ANDROID__
        if (startsWith(folder, "content://")) {
            AndroidContentURI uri(folder);
            if (uri.IsTreeURI() && !endsWithNoCase(uri.RootPath(), "BACKGROUND") && !endsWithNoCase(uri.RootPath(), "BACKGROUND/")) {
                dirUri = uri.WithRootFilePath("PSP/BACKGROUND").ToString();
            }
            bool exists = false;
            fileList = Android_ListContentUri(dirUri, "", &exists);
        } else {
#endif
            if (!endsWithNoCase(folder, "BACKGROUND") && !endsWithNoCase(folder, "BACKGROUND/")) {
                Path p(folder);
                p /= "PSP/BACKGROUND";
                dirUri = p.ToString();
            }
            File::GetFilesInDir(Path(dirUri), &fileList);
#ifdef __ANDROID__
        }
#endif

        std::vector<std::string> whitelist;
        SplitString(g_Config.sBackgroundVideoWhitelist, ',', whitelist);
        std::set<std::string> whitelistSet;
        for (const auto &s : whitelist) {
            if (!s.empty()) whitelistSet.insert(s);
        }

        const char *exts[] = { ".mp4", ".mkv", ".webm", ".mov", ".avi" };
        std::vector<std::string> allVideoNames;
        for (auto &f : fileList) {
            if (f.isDirectory) continue;
            std::string lower = f.name;
            for (auto &c : lower) c = tolower(c);
            bool isVideo = false;
            for (auto e : exts) {
                if (endsWith(lower, e)) { isVideo = true; break; }
            }
            if (isVideo) {
                allVideoNames.push_back(f.name);
            }
        }

        for (const auto &fileName : allVideoNames) {
            bool *enabled = new bool(whitelistSet.empty() || whitelistSet.count(fileName) > 0);

            CheckBox *cb = list->Add(new CheckBox(enabled, fileName, ""));
            cb->OnClick.Add([fileName, enabled, allVideoNames](EventParams &e) {
                std::vector<std::string> whitelist;
                SplitString(g_Config.sBackgroundVideoWhitelist, ',', whitelist);
                std::set<std::string> whitelistSet;
                for (const auto &s : whitelist) {
                    if (!s.empty()) whitelistSet.insert(s);
                }

                // If currently empty, it means "all". If we toggle one, we must populate.
                if (whitelistSet.empty()) {
                    for (const auto &name : allVideoNames) {
                        whitelistSet.insert(name);
                    }
                }

                if (*enabled) {
                    whitelistSet.insert(fileName);
                } else {
                    whitelistSet.erase(fileName);
                }

                // If we now have all of them, or none of them, what to do?
                // Let's keep it explicit for now.
                std::string newWhitelist;
                for (auto it = whitelistSet.begin(); it != whitelistSet.end(); ++it) {
                    if (it != whitelistSet.begin()) newWhitelist += ",";
                    newWhitelist += *it;
                }
                g_Config.sBackgroundVideoWhitelist = newWhitelist;
                g_Config.Save("BackgroundVideoPlaylist");

                if (g_BackgroundPlayer) {
                    g_BackgroundPlayer->LoadFromBackgroundFolder(g_Config.sBackgroundVideoPath, g_Config.sBackgroundVideoWhitelist);
                }
            });
        }

        if (allVideoNames.empty()) {
            list->Add(new TextView("No videos found in this folder."));
        }
    }

    void OnCompleted(DialogResult result) override {
        // nothing extra needed here - the browse callback handles everything
    }
};

class CheckBoxChoice : public UI::Choice {
public:
    CheckBoxChoice(std::string_view text, UI::CheckBox *checkbox, UI::LayoutParams *lp)
        : Choice(text, lp), checkbox_(checkbox) {
        OnClick.Handle(this, &CheckBoxChoice::HandleClick);
    }
    CheckBoxChoice(ImageID imgID, UI::CheckBox *checkbox, UI::LayoutParams *lp)
        : Choice(imgID, lp), checkbox_(checkbox) {
        OnClick.Handle(this, &CheckBoxChoice::HandleClick);
    }

private:
    void HandleClick(UI::EventParams &e);

    UI::CheckBox *checkbox_;
};

void CheckBoxChoice::HandleClick(UI::EventParams &e) {
    checkbox_->Toggle();
}

std::string_view TouchControlVisibilityScreen::GetTitle() const {
    auto co = GetI18NCategory(I18NCat::CONTROLS);
    return co->T("Touch Control Visibility");
}

void TouchControlVisibilityScreen::CreateContextMenu(UI::ViewGroup *parent) {
    using namespace UI;

    auto di = GetI18NCategory(I18NCat::DIALOG);

    Choice *toggleAll = parent->Add(new Choice(di->T("Toggle All")));
    toggleAll->OnClick.Add([this](UI::EventParams &e) {
        // TODO: Is this a meaningful operation to support?
        for (auto toggle : toggles_) {
            *toggle.show = nextToggleAll_;
        }
        nextToggleAll_ = !nextToggleAll_;
    });
}

void TouchControlVisibilityScreen::CreateDialogViews(UI::ViewGroup *parent) {
    using namespace UI;
    using namespace CustomKeyData;

    auto di = GetI18NCategory(I18NCat::DIALOG);
    auto co = GetI18NCategory(I18NCat::CONTROLS);

    // Temporarily hide the drum to prevent it from blocking the UI
    TouchControlConfig &touch = g_Config.GetTouchControlsConfig(GetDeviceOrientation());

    const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

    const int cellSize = portrait ? std::min((g_display.dp_xres / 2 - 10), 290) : 380;
    UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
    gridsettings.fillCells = true;

    toggles_.clear();
    toggles_.push_back({ "Circle", &touch.bShowTouchCircle, ImageID("I_CIRCLE"), nullptr, &touch.bToggleTouchCircle, &touch.bRepeatTouchCircle });
    toggles_.push_back({ "Cross", &touch.bShowTouchCross, ImageID("I_CROSS"), nullptr, &touch.bToggleTouchCross, &touch.bRepeatTouchCross });
    toggles_.push_back({ "Square", &touch.bShowTouchSquare, ImageID("I_SQUARE"), nullptr, &touch.bToggleTouchSquare, &touch.bRepeatTouchSquare });
    toggles_.push_back({ "Triangle", &touch.bShowTouchTriangle, ImageID("I_TRIANGLE"), nullptr, &touch.bToggleTouchTriangle, &touch.bRepeatTouchTriangle });
    toggles_.push_back({ "L", &touch.touchLKey.show, ImageID("I_L"), nullptr });
    toggles_.push_back({ "R", &touch.touchRKey.show, ImageID("I_R"), nullptr });
    toggles_.push_back({ "Start", &touch.touchStartKey.show, ImageID("I_START"), nullptr });
    toggles_.push_back({ "Select", &touch.touchSelectKey.show, ImageID("I_SELECT"), nullptr });

    toggles_.push_back({ "Dpad", &touch.touchDpad.show, ImageID::invalid(), nullptr });
    toggles_.push_back({ "Analog Stick", &touch.touchAnalogStick.show, ImageID::invalid(), nullptr });
    toggles_.push_back({ "Right Analog Stick", &touch.touchRightAnalogStick.show, ImageID::invalid(), [=](EventParams &e) {
        screenManager()->push(new RightAnalogMappingScreen(gamePath_));
    }});
    toggles_.push_back({ "Fast-forward", &touch.touchFastForwardKey.show, ImageID::invalid(), nullptr, &touch.bToggleTouchFastForward, &touch.bRepeatTouchFastForward});
    toggles_.push_back({ "Pause", &touch.touchPauseKey.show, ImageID("I_HAMBURGER"), nullptr});
    toggles_.push_back({ "Taiko Drum", &touch.touchTatacon.show, ImageID::invalid(), nullptr});
    toggles_.push_back({"Animation Background", &g_Config.bAnimationBackground, ImageID::invalid(),
                        [=](EventParams &e) {
                            screenManager()->push(new BackgroundVideoSelectPopup());
                        }
                       });

    for (int i = 0; i < TouchControlConfig::CUSTOM_BUTTON_COUNT; i++) {
        char temp[256];
        snprintf(temp, sizeof(temp), "Custom %d", i + 1);
        toggles_.push_back({ temp, &touch.touchCustom[i].show, ImageID::invalid(), [=](EventParams &e) {
            screenManager()->push(new CustomButtonMappingScreen(GetDeviceOrientation(), gamePath_, i));
        } });
    }

    auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);

    UI::GridLayout *grid = parent->Add(new UI::GridLayout(gridsettings, new UI::LayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT)));

    for (auto toggle : toggles_) {
        LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
        row->SetSpacing(0);

        CheckBox *checkbox = new CheckBox(toggle.show, "", "", new LinearLayoutParams(50, WRAP_CONTENT));
        row->Add(checkbox);

        Choice *choice = nullptr;
        if (toggle.handle) {
            // Handle custom button strings differently, and hackily. But will extend to arbitrary button counts.
            char translated[256];
            int i = 0;
            if (sscanf(toggle.key.c_str(), "Custom %d", &i) == 1) {
                snprintf(translated, sizeof(translated), mc->T_cstr("Custom %d"), i);
            } else {
                truncate_cpy(translated, sizeof(translated), mc->T(toggle.key));
            }
            choice = new Choice(std::string(translated) + " (" + std::string(mc->T("tap to customize")) + ")", "", new LinearLayoutParams(1.0f));
            choice->OnClick.Add(toggle.handle);
        } else if (toggle.img.isValid()) {
            choice = new Choice(toggle.img, new LinearLayoutParams(1.0f));
            if (toggle.toggle != nullptr) {
                choice->OnClick.Add([this, toggle](UI::EventParams &e) {
                    auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);
                    screenManager()->push(new PSPButtonModePopup(mc->T(toggle.key), toggle.toggle, toggle.repeat));
                });
            }
        } else {
            choice = new Choice(mc->T(toggle.key), new LinearLayoutParams(1.0f));
        }
        if (choice)
            row->Add(choice);
        grid->Add(row);
    }

    LinearLayout *buttons = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
    buttons->SetSpacing(0);

    Choice *back = new Choice("Back", new LinearLayoutParams(1.0f));
    back->OnClick.Add([=](UI::EventParams &e) {
        TriggerFinish(DR_OK);
    });
    buttons->Add(back);

    parent->Add(buttons);
}

void TouchControlVisibilityScreen::onFinish(DialogResult result) {
	g_Config.Save("TouchControlVisibilityScreen::onFinish");
}

std::string_view RightAnalogMappingScreen::GetTitle() const {
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	return co->T("Right Analog Stick");
}

void RightAnalogMappingScreen::CreateDialogViews(UI::ViewGroup *parent) {
	using namespace UI;
	auto co = GetI18NCategory(I18NCat::CONTROLS);

	parent->Add(new CheckBox(&g_Config.bRightAnalogCustom, co->T("Custom Mapping")));
	parent->Add(new CheckBox(&g_Config.bRightAnalogDisableDiagonal, co->T("Disable Diagonal")));

	static const char *rightAnalogStickMappings[] = {
		"None",
		"L", "R", "Square", "Triangle", "Circle", "Cross", "Up", "Down", "Left", "Right", "Start", "Select",
		"Right Stick Down", "Right Stick Up", "Right Stick Left", "Right Stick Right",
		"Left Stick Down", "Left Stick Up", "Left Stick Left", "Left Stick Right"
	};

	auto mappingGroup = parent->Add(new LinearLayout(ORIENT_VERTICAL));
	mappingGroup->SetEnabledPtr(&g_Config.bRightAnalogCustom);

	mappingGroup->Add(new PopupMultiChoice(&g_Config.iRightAnalogUp, co->T("Up"), rightAnalogStickMappings, 0, ARRAY_SIZE(rightAnalogStickMappings), I18NCat::MAPPABLECONTROLS, screenManager()));
	mappingGroup->Add(new PopupMultiChoice(&g_Config.iRightAnalogDown, co->T("Down"), rightAnalogStickMappings, 0, ARRAY_SIZE(rightAnalogStickMappings), I18NCat::MAPPABLECONTROLS, screenManager()));
	mappingGroup->Add(new PopupMultiChoice(&g_Config.iRightAnalogLeft, co->T("Left"), rightAnalogStickMappings, 0, ARRAY_SIZE(rightAnalogStickMappings), I18NCat::MAPPABLECONTROLS, screenManager()));
	mappingGroup->Add(new PopupMultiChoice(&g_Config.iRightAnalogRight, co->T("Right"), rightAnalogStickMappings, 0, ARRAY_SIZE(rightAnalogStickMappings), I18NCat::MAPPABLECONTROLS, screenManager()));
	mappingGroup->Add(new PopupMultiChoice(&g_Config.iRightAnalogPress, co->T("Press"), rightAnalogStickMappings, 0, ARRAY_SIZE(rightAnalogStickMappings), I18NCat::MAPPABLECONTROLS, screenManager()));

	Choice *back = new Choice(GetI18NCategory(I18NCat::DIALOG)->T("Back"), new LinearLayoutParams(1.0f));
	back->OnClick.Add([=](UI::EventParams &e) {
		TriggerFinish(DR_OK);
	});
	parent->Add(back);
}

