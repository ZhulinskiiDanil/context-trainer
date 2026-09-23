#include "Store.hpp"
#include "TrainerUI.hpp"

#include <Geode/ui/Popup.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

using namespace geode::prelude;

namespace context {
namespace {

std::string elapsed(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return "0s";
    if (seconds < 60) return fmt::format("{:.0f}s", seconds);
    return fmt::format("{:.0f}m", seconds / 60);
}

class ProgressPopup final : public Popup {
    enum Action {
        Map = 1, Results, Previous, Next, Practice, Normal, Verify, Study,
        OtherRecords, BackToProgress, Guide, CurrentPlan, AllSaved, CombinedRecords, AllModes
    };

    int m_tab = Results;
    int m_windowId = 0;
    std::optional<bool> m_practice;
    Mode m_mode = Mode::Verify;
    bool m_allSaved = false;
    bool m_otherRecords = false;
    bool m_guide = false;
    CCNode* m_page = nullptr;
    CCMenu* m_menu = nullptr;

    CCMenuItemSpriteExtra* button(std::string const& caption, float x, float y, float width,
                                int action, bool selected = false, float height = 25.f) {
        return ui::button(m_menu, caption, x, y, width, height, this,
            menu_selector(ProgressPopup::onAction), action,
            selected ? ui::Tone::Selected : ui::Tone::Normal);
    }

    bool init() override {
        if (!Popup::init(460, 300, "GJ_square01_custom.png"_spr)) return false;
        setID("context-progress-popup"_spr);
        m_closeBtn->setScale(.6f);
        m_closeBtn->setPosition({444, 283});
        auto const& store = Store::get();
        m_windowId = store.activeId;
        m_allSaved = !store.plan.created;
        ui::text(m_mainLayer, "Statistics", 22, 275, 255, 16,
            ui::Text, true, "bigFont.fnt");
        ui::text(m_mainLayer, store.levelName, 22, 255, 260, 10, ui::Muted);
        render();
        return true;
    }

    void render() {
        if (m_page) m_page->removeFromParentAndCleanup(true);
        m_page = CCNode::create();
        m_mainLayer->addChild(m_page);
        m_menu = ui::menu(m_page);
        button("Runs", 326, 266, 62, Results, m_tab == Results, 23);
        button("Map", 398, 266, 62, Map, m_tab == Map, 23);
        ui::line(m_page, 22, 243, 416);
        if (m_tab == Map) drawMap();
        else if (m_otherRecords) drawRecordFilters();
        else if (m_guide) drawGuide();
        else drawResults();
        handleTouchPriority(this);
    }

    void drawMap() {
        auto const& survey = Store::get().plan.survey;
        std::array<int, 20> deaths{};
        std::array<bool, 20> observed{};
        for (std::size_t i = 0; i < survey.bins.size(); ++i) {
            auto const& bin = survey.bins[i];
            deaths[i / 5] += bin.deaths;
            observed[i / 5] = observed[i / 5] || bin.covered;
        }
        int const totalDeaths = std::accumulate(deaths.begin(), deaths.end(), 0);
        bool const hasData = survey.runs > 0 || survey.totalSeconds > 0 || totalDeaths > 0 ||
            std::any_of(observed.begin(), observed.end(), [](bool value) { return value; });

        ui::text(m_page, "Where you needed practice", 22, 224, 410, 15);
        ui::text(m_page, "A map of your first pass through the level", 22, 205, 410, 10, ui::Muted);
        if (!hasData) {
            ui::panel(m_page, 22, 64, 416, 119);
            ui::text(m_page, "Your level map starts with Stage 1", 230, 142, 365, 14, ui::Text, false);
            ui::text(m_page, "Create a plan and play through your selected starts.", 230, 115, 365, 10, ui::Muted, false);
            ui::text(m_page, "Each attempt adds to this map.", 230, 94, 365, 10, ui::Muted, false);
        } else {
            ui::text(m_page,
                fmt::format("{} attempts     {} played     {} deaths", survey.runs,
                    elapsed(survey.totalSeconds), totalDeaths),
                22, 178, 410, 11);

            constexpr float x = 48, y = 70, width = 368, height = 87;
            int const peak = *std::max_element(deaths.begin(), deaths.end());
            ui::line(m_page, x, y, width, ui::Muted);
            if (peak > 0) {
                ui::line(m_page, x, y + height, width);
                ui::text(m_page, fmt::format("{}", peak), 25, y + height, 20, 9, ui::Muted);
                ui::text(m_page, "0", 25, y, 20, 9, ui::Muted);
            } else {
                ui::text(m_page, "No deaths recorded in this first pass.", 232, 112,
                    345, 11, ui::Muted, false);
            }
            constexpr float step = width / 20;
            for (std::size_t i = 0; i < deaths.size(); ++i) {
                float const barX = x + static_cast<float>(i) * step + 2;
                if (peak > 0 && deaths[i] > 0)
                    ui::fill(m_page, barX, y + 1, step - 4,
                        height * static_cast<float>(deaths[i]) / peak, ui::Danger);
                ui::fill(m_page, barX, y - 5, step - 4, 2,
                    observed[i] ? ui::Accent : ui::Surface);
            }
            for (int tick = 0; tick <= 4; ++tick) {
                float const tickX = x + width * tick / 4;
                ui::text(m_page, fmt::format("{}%", tick * 25), tickX, y - 16,
                    42, 9, ui::Muted, false);
            }
            ui::fill(m_page, 25, 33, 9, 5, ui::Danger);
            ui::text(m_page, "Deaths per 5%", 40, 36, 130, 9, ui::Muted);
            ui::fill(m_page, 224, 33, 9, 5, ui::Accent);
            ui::text(m_page, "Played during Stage 1", 239, 36, 196, 9, ui::Muted);
        }
        ui::text(m_page, "For current reliability, open Runs.", 230, 17,
            414, 9, ui::Muted, false);
    }

    std::vector<Window const*> visibleWindows() const {
        auto const& store = Store::get();
        std::vector<Window const*> result;
        if (m_allSaved) {
            for (auto const& window : store.windows) result.push_back(&window);
        } else {
            for (int id : store.plan.windowIds) {
                auto found = std::find_if(store.windows.begin(), store.windows.end(),
                    [id](auto const& window) { return window.id == id; });
                if (found != store.windows.end()) result.push_back(&*found);
            }
        }
        return result;
    }

    Window const* viewedWindow(std::vector<Window const*> const& windows) {
        if (windows.empty()) return nullptr;
        auto found = std::find_if(windows.begin(), windows.end(),
            [&](auto const* window) { return window->id == m_windowId; });
        if (found == windows.end()) {
            m_windowId = windows.front()->id;
            return windows.front();
        }
        return *found;
    }

    std::string recordCaption() const {
        return fmt::format("{} / {}", !m_practice.has_value() ? "Normal + Practice" : *m_practice ? "Practice" : "Normal",
            m_mode == Mode::Verify ? "Checks" : "Learning");
    }

    void drawResults() {
        bool const filtered = m_practice.has_value() || m_mode != Mode::Verify ||
            (m_allSaved && Store::get().plan.created);
        button("Filters", 358, 21, 88, OtherRecords, filtered, 22);
        ui::iconButton(m_menu, "GJ_infoIcon_001.png", 424, 21, 22, this,
            menu_selector(ProgressPopup::onAction), Guide);
        auto const windows = visibleWindows();
        auto const* window = viewedWindow(windows);
        ui::text(m_page, window ? window->name : "Run progress", 22, 28, 278, 9, ui::Muted);
        ui::text(m_page, recordCaption(), 22, 15, 278, 8.5f, filtered ? ui::Accent : ui::Muted);
        if (!window) {
            ui::panel(m_page, 22, 66, 416, 160);
            ui::text(m_page, m_allSaved ? "No saved runs yet" : "No training runs in this plan yet",
                230, 169, 370, 15, ui::Text, false);
            ui::text(m_page, "Finish the first pass to create your training runs.",
                230, 140, 370, 10, ui::Muted, false);
            ui::text(m_page, !m_allSaved && !Store::get().windows.empty()
                    ? "Older runs are available under Filters."
                    : "Results appear as you practise each run.",
                230, 119, 370, 10, ui::Muted, false);
            return;
        }

        auto index = std::distance(windows.begin(), std::find(windows.begin(), windows.end(), window)) + 1;
        ui::iconButton(m_menu, "GJ_arrow_01_001.png", 35, 218, 26, this,
            menu_selector(ProgressPopup::onAction), Previous, false, windows.size() > 1);
        ui::iconButton(m_menu, "GJ_arrow_01_001.png", 425, 218, 26, this,
            menu_selector(ProgressPopup::onAction), Next, true, windows.size() > 1);
        ui::text(m_page, ui::range(window->start, window->end),
            230, 225, 344, 18, ui::Current, false);

        int attempts = 0;
        int passes = 0;
        for (auto const& attempt : window->history) {
            if (!matchesRecord(attempt, m_practice, m_mode)) continue;
            ++attempts;
            if (attempt.outcome == Outcome::Success) ++passes;
        }
        ui::text(m_page, fmt::format("Run {} of {}     {} passes / {} attempts", index, windows.size(), passes, attempts),
            230, 205, 344, 10, ui::Muted, false);
        ui::interval(m_page, 80, 193, 300, window->start, window->end);
        auto const threshold = Store::get().threshold();
        constexpr std::array contexts{RunContext::Fresh, RunContext::LeadIn, RunContext::FromZero};
        constexpr std::array titles{"From this start", "From an earlier start", "From level beginning"};
        for (std::size_t i = 0; i < contexts.size(); ++i)
            drawContext(*window, contexts[i], titles[i], 132.f - static_cast<float>(i) * 48, threshold);
    }

    void drawContext(Window const& window, RunContext runContext, char const* title,
                     float y, Threshold threshold) {
        auto const summary = summarize(window, runContext, m_practice, m_mode, threshold);
        ui::panel(m_page, 22, y, 416, 44);
        ui::text(m_page, title, 32, y + 31, 180, 11);

        std::string status;
        ccColor3B color = ui::Muted;
        if (!summary.attempts) status = "No results yet";
        else if (m_mode == Mode::Study) status = "Learning only";
        else if (summary.transferred) {
            if (runContext == RunContext::Fresh)
                status = summary.repeatable ? "Reliable after a return" : "Passed after a return";
            else
                status = summary.repeatable ? "Reliable with this entry" : "Passed with this entry";
            color = ui::Success;
        } else if (summary.repeatable) {
            status = "Reliable";
            color = ui::Success;
        } else {
            status = fmt::format("{} / {} recent passes", summary.recentSuccesses, summary.recentAttempts);
            color = ui::Current;
        }
        ui::text(m_page, status, 224, y + 31, 204, 9.5f, color);
        ui::text(m_page, summary.attempts
                ? fmt::format("{} passed / {} attempts", summary.successes, summary.attempts)
                : "No completed attempts",
            32, y + 15, 180, 9, ui::Muted);
        ui::outcomeStrip(m_page, window, runContext, m_practice, m_mode, 224, y + 8, 204, 13);
    }

    void drawRecordFilters() {
        ui::text(m_page, "Filter results", 22, 225, 414, 15);
        ui::text(m_page, "Training always uses checks from both game modes.", 22, 205, 410, 10, ui::Muted);

        ui::text(m_page, "Runs", 22, 183, 410, 10, ui::Muted);
        auto current = button("Current plan", 123, 164, 202, CurrentPlan, !m_allSaved, 23);
        current->setEnabled(Store::get().plan.created);
        if (!Store::get().plan.created) current->setOpacity(110);
        button("All saved runs", 337, 164, 202, AllSaved, m_allSaved, 23);

        ui::text(m_page, "Game mode", 22, 140, 410, 10, ui::Muted);
        button("All modes", 88, 121, 132, AllModes, !m_practice.has_value(), 23);
        button("Normal", 230, 121, 132, Normal, m_practice == false, 23);
        button("Practice", 372, 121, 132, Practice, m_practice == true, 23);

        ui::text(m_page, "Attempt type", 22, 97, 410, 10, ui::Muted);
        button("Checks", 123, 78, 202, Verify, m_mode == Mode::Verify, 23);
        button("Learning", 337, 78, 202, Study, m_mode == Mode::Study, 23);
        ui::text(m_page, "Learning attempts do not establish reliability. Filters only change this view.",
            22, 56, 416, 9, ui::Muted);
        ui::line(m_page, 22, 43, 416);
        button("Back to runs", 144, 23, 244, BackToProgress, false, 24);
        button("Reset", 358, 23, 160, CombinedRecords, false, 24);
    }

    void drawGuide() {
        auto const threshold = Store::get().threshold();
        ui::text(m_page, "What counts as progress", 22, 224, 414, 15);
        ui::panel(m_page, 22, 151, 416, 56);
        ui::text(m_page, "Pass", 32, 191, 54, 11, ui::Success);
        ui::text(m_page, "Reached the end; saved when the attempt ends.", 93, 191, 332, 10);
        ui::text(m_page, "Fail", 32, 173, 54, 11, ui::Danger);
        ui::text(m_page, "The attempt ended before the end of the run.", 93, 173, 332, 10);
        ui::text(m_page, "The six latest results read left to right, oldest to newest.",
            22, 137, 416, 10, ui::Muted);
        ui::text(m_page, fmt::format("Reliable: at least {} passes in the last {} checks.",
            threshold.required, threshold.total), 22, 116, 416, 11, ui::Success);
        ui::text(m_page, "Each starting point has its own history and reliability check.",
            22, 97, 416, 10, ui::Muted);
        ui::line(m_page, 22, 83, 416);
        ui::text(m_page, "Normal and Practice count together. Unfinished attempts are left out.",
            22, 69, 416, 10);
        ui::text(m_page, "A checkpoint inside a run cannot confirm its missing beginning.",
            22, 52, 416, 10, ui::Muted);
        button("Back to runs", 230, 22, 220, BackToProgress, false, 25);
    }

    void onAction(CCObject* sender) {
        sender->retain();
        sender->autorelease();
        int const action = static_cast<CCNode*>(sender)->getTag();
        switch (action) {
            case Map: case Results:
                m_tab = action;
                m_otherRecords = m_guide = false;
                break;
            case OtherRecords: m_otherRecords = true; m_guide = false; break;
            case Guide: m_guide = true; m_otherRecords = false; break;
            case BackToProgress: m_otherRecords = m_guide = false; break;
            case CurrentPlan:
                if (!Store::get().plan.created) return;
                m_allSaved = false;
                break;
            case AllSaved: m_allSaved = true; break;
            case CombinedRecords:
                m_practice.reset();
                m_mode = Mode::Verify;
                m_allSaved = !Store::get().plan.created;
                m_otherRecords = m_guide = false;
                break;
            case AllModes: m_practice.reset(); break;
            case Practice: m_practice = true; break;
            case Normal: m_practice = false; break;
            case Verify: m_mode = Mode::Verify; break;
            case Study: m_mode = Mode::Study; break;
            case Previous: case Next: {
                auto const windows = visibleWindows();
                auto const* window = viewedWindow(windows);
                if (!window) return;
                int const count = static_cast<int>(windows.size());
                int index = static_cast<int>(std::distance(windows.begin(), std::find(windows.begin(), windows.end(), window)));
                index = (index + (action == Next ? 1 : count - 1)) % count;
                m_windowId = windows[index]->id;
                break;
            }
            default: return;
        }
        render();
    }

public:
    static ProgressPopup* create() {
        auto popup = new ProgressPopup;
        if (popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

} // namespace

void showTrainingProgress() {
    if (auto popup = ProgressPopup::create()) popup->show();
}

} // namespace context
