#include "Store.hpp"
#include "TrainerUI.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <array>
#include <numeric>

using namespace geode::prelude;

namespace context {
namespace {
class TrainerPopup final : public Popup {
    enum class Page { Home, Setup, Options, Runs, Pace, OriginalLevel };
    enum Action { Home = 1, Setup, Options, Runs, Stats, Tools, Help, Primary,
        Begin, Stop, SelectAll, SelectNone, Pace, Next, LessTime, MoreTime,
        ReminderOff, Reminder20, Reminder30, OriginalLevel, SaveAndExit, LinkOriginal, UnlinkOriginal,
        ResumeOriginal, OptionFirst = 100, StartFirst = 1000,
        RunFirst = 100000 };
    Page m_page = Page::Home;
    CCNode* m_content = nullptr;
    CCMenu* m_menu = nullptr;
    ScrollLayer* m_scroll = nullptr;
    WeakRef<PauseLayer> m_pause;
    std::vector<int> m_selected;
    AnalysisOptions m_options;
    unsigned m_seenRevision = 0;
    bool m_seenEnabled = false;
    std::string m_originalIdText;

    CCMenuItemSpriteExtra* button(std::string const& caption, float x, float y,
        float width, int action, ui::Tone tone = ui::Tone::Normal, float height = 28) {
        return ui::button(m_menu, caption, x, y, width, height, this,
            menu_selector(TrainerPopup::onAction), action, tone);
    }

    void heading(std::string const& title, std::string const& subtitle, float titleWidth = 320) {
        ui::text(m_content, title, 22, 277, titleWidth, 18, ui::Text, true, "bigFont.fnt");
        ui::text(m_content, subtitle, 22, 255, 387, 10.5f, ui::Muted);
        ui::line(m_content, 22, 242, 416);
    }

    void steps(int active) {
        constexpr std::array names{"1  Choose starts", "2  Learn the level", "3  Train"};
        for (int i = 0; i < 3; ++i) {
            float x = 22.f + 141.f * i;
            ui::text(m_content, names[i], x, 225, 130, 10.5f,
                i == active ? ui::Accent : i < active ? ui::Success : ui::Muted);
            ui::fill(m_content, x, 213, 132, 2, i == active ? ui::Accent : ui::Surface);
        }
    }

    static bool selected(std::vector<int> const& ids, int id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }

    static int runMinutes() {
        return static_cast<int>(std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("run-minutes"), 1, 10));
    }

    static int reminderMinutes() {
        return static_cast<int>(Mod::get()->getSettingValue<int64_t>("break-reminder-minutes"));
    }

    void setReminder(int minutes) {
        Mod::get()->setSettingValue<int64_t>("break-reminder-minutes", minutes);
        m_options.restReminders = minutes > 0;
        auto& store = Store::get();
        if (store.plan.created) store.plan.options.restReminders = minutes > 0;
        store.save();
        render();
    }

    bool ready(Window const& window) const {
        auto const& store = Store::get();
        auto fresh = summarize(window, window.start <= .01 ? RunContext::FromZero : RunContext::Fresh,
            std::nullopt, Mode::Verify, store.threshold());
        if (!fresh.repeatable) return false;
        if (!store.plan.options.longerEntries || window.start <= .01) return true;
        return fresh.transferred ||
            summarize(window, RunContext::LeadIn, std::nullopt, Mode::Verify, store.threshold()).transferred ||
            summarize(window, RunContext::FromZero, std::nullopt, Mode::Verify, store.threshold()).transferred;
    }

    std::vector<Window const*> planWindows() const {
        auto const& store = Store::get();
        std::vector<Window const*> windows;
        for (auto const& window : store.windows)
            if (selected(store.plan.windowIds, window.id)) windows.push_back(&window);
        std::sort(windows.begin(), windows.end(), [](auto a, auto b) { return a->start < b->start; });
        return windows;
    }

    void map(float y, bool setup = false) {
        auto const& store = Store::get();
        ui::fill(m_content, 22, y, 416, 13, ui::Surface);
        if (setup) {
            for (auto const& point : store.startPoints) {
                float x = 22 + 416.f * static_cast<float>(point.percent / 100.);
                ui::fill(m_content, x, y - 2, 2, 17, selected(m_selected, point.id) ? ui::Accent : ui::Muted);
            }
        } else if (store.plan.scanning) {
            for (size_t i = 0; i < store.plan.actualStarts.size(); ++i) {
                double start = store.plan.actualStarts[i];
                double end = i + 1 < store.plan.actualStarts.size() ? store.plan.actualStarts[i + 1] : 100.;
                auto color = store.plan.stagePassed[i] ? ui::Success :
                    static_cast<int>(i) == store.plan.currentStage ? ui::Current : ui::Surface;
                ui::fill(m_content, 22 + 416 * static_cast<float>(start / 100.), y,
                    std::max(.8f, 416 * static_cast<float>((end - start) / 100.) - 1), 13, color);
            }
        } else {
            for (auto window : planWindows()) {
                bool enabled = selected(store.plan.selectedIds, window->id);
                auto color = !enabled ? ui::Surface : ready(*window) ? ui::Success :
                    window->id == store.activeId ? ui::Current : ccColor3B{111, 98, 141};
                ui::fill(m_content, 22 + 416 * static_cast<float>(window->start / 100.), y,
                    std::max(.8f, 416 * static_cast<float>((window->end - window->start) / 100.) - 1), 13, color);
            }
        }
        for (int i = 0; i <= 4; ++i)
            ui::text(m_content, fmt::format("{}%", i * 25), 22.f + 104.f * i, y - 9,
                40, 8, ui::Muted, i == 0);
    }

    void renderHome() {
        auto& store = Store::get();
        if (store.originalSession) { renderOriginalHome(); return; }
        auto const& plan = store.plan;
        heading("Context Trainer", store.levelName, 220);
        button("Pace", 279, 276, 49, Pace, ui::Tone::Secondary, 23);
        button("Stats", 334, 276, 49, Stats, ui::Tone::Secondary, 23);
        button("Tools", 389, 276, 49, Tools, ui::Tone::Secondary, 23);
        if (store.platformer) {
            ui::panel(m_content, 22, 95, 416, 135);
            ui::text(m_content, "Open a classic level", 38, 198, 380, 21);
            ui::text(m_content, "Training uses percentages and StartPos.", 38, 168, 380, 12, ui::Muted);
            ui::text(m_content, "Platformer levels are not supported yet.", 38, 146, 380, 12, ui::Muted);
            return;
        }
        if (!plan.created) {
            steps(0);
            ui::panel(m_content, 22, 75, 416, 126);
            ui::text(m_content, "Choose where to begin.", 36, 179, 382, 21);
            ui::text(m_content, "Pass each selected run once.", 36, 150, 382, 12);
            ui::text(m_content, "Then train the parts that need another try.", 36, 130, 382, 11, ui::Muted);
            auto count = store.startPoints.size();
            ui::text(m_content, count > 1 ? fmt::format("{} starting points found", count) :
                count == 1 ? "Full level from 0%" : "Waiting for the level to load", 36, 101, 382, 12, ui::Accent);
            ui::text(m_content, count == 1 ? "Add StartPos in a copy for shorter runs." :
                count ? "Your starts and results are saved." : "Resume play, then reopen Trainer.",
                22, 58, 416, 10, ui::Muted);
            button("How it works", 104, 30, 164, Help, ui::Tone::Secondary, 30);
            auto create = button("Choose starts", 320, 30, 236, Setup, ui::Tone::Primary, 32);
            create->setEnabled(count > 0);
            if (!count) create->setOpacity(90);
            return;
        }
        if (plan.scanning) {
            int done = static_cast<int>(std::count(plan.stagePassed.begin(), plan.stagePassed.end(), true));
            int total = static_cast<int>(plan.stagePassed.size());
            bool complete = done == total;
            ui::panel(m_content, 22, 91, 266, 140);
            ui::text(m_content, complete ? "First pass complete" : "Learn the level", 34, 213, 238, 12, ui::Accent);
            if (complete) {
                ui::text(m_content, "Every run passed", 34, 182, 238, 22);
                ui::text(m_content, "Your training plan is ready to build.", 34, 152, 238, 11, ui::Muted);
            } else {
                auto i = static_cast<size_t>(std::clamp(plan.currentStage, 0, total - 1));
                ui::text(m_content, ui::range(plan.actualStarts[i], store.stageEnd()), 34, 183, 238, 25, ui::Current);
                ui::interval(m_content, 34, 153, 238, plan.actualStarts[i], store.stageEnd());
                ui::text(m_content, "Pass this run once.", 34, 127, 238, 12);
                ui::text(m_content, "Deaths retry the same start.", 34, 108, 238, 10, ui::Muted);
            }
            ui::panel(m_content, 298, 91, 140, 140);
            ui::text(m_content, "First pass", 310, 213, 116, 11, ui::Muted);
            ui::text(m_content, fmt::format("{} / {}", done, total), 310, 185, 116, 23, ui::Success);
            ui::text(m_content, "runs passed", 310, 165, 116, 10, ui::Muted);
            ui::meter(m_content, 310, 147, 116, 5, total ? double(done) / total : 0, ui::Success);
            button("All runs", 368, 113, 116, Runs, ui::Tone::Secondary, 26);
            ui::text(m_content, store.enabled ? "Each completed run moves you to the next start." : "Paused. Your completed runs are saved.",
                22, 69, 416, 10, ui::Muted);
            button("Change starts", 104, 30, 164, Setup, ui::Tone::Secondary, 30);
            button(complete ? "Start training" : store.enabled ? "Play this run" : "Continue first pass",
                320, 30, 236, Primary, ui::Tone::Primary, 32);
            return;
        }

        auto windows = planWindows();
        int completed = 0, enabledCount = 0;
        for (auto window : windows) if (selected(plan.selectedIds, window->id)) {
            ++enabledCount;
            if (ready(*window)) ++completed;
        }
        bool allReady = enabledCount > 0 && completed == enabledCount;
        Window const* window = store.active();
        if (!window || !selected(plan.selectedIds, window->id)) {
            window = nullptr;
            for (auto w : windows) if (selected(plan.selectedIds, w->id)) { window = w; break; }
        }

        ui::panel(m_content, 22, 86, 266, 145);
        ui::text(m_content, !enabledCount ? "No runs selected" : !store.enabled ? "Training paused" : "Your current run",
            34, 214, 238, 11, ui::Muted);
        if (window) {
            double start = window->start;
            int startId = store.autoStartId();
            for (size_t i = 0; i < plan.selectedStartIds.size(); ++i)
                if (plan.selectedStartIds[i] == startId) start = plan.actualStarts[i];
            if (startId == 0) start = 0;
            auto context = classify(start, *window);
            auto progress = summarize(*window, context, std::nullopt, Mode::Verify, store.threshold());
            ui::text(m_content, ui::range(start, window->end), 34, 185, 238, 26, ui::Current);
            ui::interval(m_content, 34, 158, 238, start, window->end);
            ui::text(m_content, progress.recentAttempts ? fmt::format("{} / {} recent passes", progress.recentSuccesses, progress.recentAttempts) : "No attempts yet",
                34, 133, 150, 11);
            ui::text(m_content, fmt::format("Goal: {} / {}", store.threshold().required, store.threshold().total),
                198, 133, 74, 10, ui::Muted);
            ui::outcomeStrip(m_content, *window, context, std::nullopt, Mode::Verify, 34, 99, 238, 19);
        } else {
            ui::text(m_content, "Pick a run to train", 34, 184, 238, 22);
            ui::text(m_content, "Open All runs to choose your parts.", 34, 151, 238, 11, ui::Muted);
        }

        ui::panel(m_content, 298, 86, 140, 145);
        ui::text(m_content, allReady ? "Ready for full runs" : "Your plan", 310, 214, 116, 11, allReady ? ui::Success : ui::Muted);
        ui::text(m_content, fmt::format("{} / {}", completed, enabledCount), 310, 185, 116, 24, ui::Success);
        ui::text(m_content, "runs ready", 310, 165, 116, 10, ui::Muted);
        ui::meter(m_content, 310, 149, 116, 5, enabledCount ? double(completed) / enabledCount : 0, ui::Success);
        button("All runs", 368, 127, 116, Runs, ui::Tone::Normal, 24);
        button("Original level", 368, 101, 116, OriginalLevel, ui::Tone::Secondary, 22);

        ui::text(m_content, "Pass the target and keep playing. Later runs count too.", 22, 70, 416, 10, ui::Muted);
        ui::text(m_content, fmt::format("Auto-switch: {} min or 5 passes in a row. Between attempts.", runMinutes()),
            22, 55, 416, 9, ui::Muted);
        auto next = button("Next run", 104, 29, 164, Next, ui::Tone::Secondary, 30);
        auto play = button(store.enabled && plan.training ? "Resume training" : allReady ? "Practice again" : "Train now",
            320, 29, 236, Primary, ui::Tone::Primary, 32);
        next->setEnabled(enabledCount > 0);
        play->setEnabled(enabledCount > 0);
        if (!enabledCount) { next->setOpacity(90); play->setOpacity(90); }
    }

    void renderPace() {
        heading("Training pace", "Changes apply to your current plan.");
        button("Back", 382, 275, 61, Home, ui::Tone::Secondary, 23);
        int minutes = runMinutes();
        ui::panel(m_content, 22, 128, 416, 104);
        ui::text(m_content, "Time on each run", 34, 212, 215, 16);
        ui::text(m_content, "Before the trainer can choose another start", 34, 192, 238, 10, ui::Muted);
        ui::text(m_content, "5 passes in a row skip the wait.", 34, 163, 238, 11, ui::Accent);
        ui::text(m_content, "A new start loads after the attempt ends.", 34, 146, 238, 10, ui::Muted);
        auto less = button("-", 299, 185, 28, LessTime, ui::Tone::Normal, 28);
        ui::text(m_content, fmt::format("{}", minutes), 355, 192, 62, 30, ui::Text, false);
        ui::text(m_content, minutes == 1 ? "minute" : "minutes", 355, 167, 68, 10, ui::Muted, false);
        auto more = button("+", 411, 185, 28, MoreTime, ui::Tone::Normal, 28);
        less->setEnabled(minutes > 1); more->setEnabled(minutes < 10);
        if (minutes == 1) less->setOpacity(90);
        if (minutes == 10) more->setOpacity(90);

        auto const& plan = Store::get().plan;
        int reminder = plan.created && !plan.options.restReminders ? 0 : reminderMinutes();
        ui::panel(m_content, 22, 46, 416, 72);
        ui::text(m_content, "Break reminder", 34, 97, 162, 16);
        ui::text(m_content, "Notification only. Never pauses play.", 34, 73, 390, 11, ui::Muted);
        ui::text(m_content, "Off during your first pass.", 34, 57, 390, 9, ui::Muted);
        button("Off", 253, 96, 54, ReminderOff, reminder == 0 ? ui::Tone::Selected : ui::Tone::Normal, 24);
        button("20 min", 319, 96, 66, Reminder20, reminder == 20 ? ui::Tone::Selected : ui::Tone::Normal, 24);
        button("30 min", 393, 96, 66, Reminder30, reminder == 30 ? ui::Tone::Selected : ui::Tone::Normal, 24);
        ui::text(m_content, "Use Next run on the training screen to move on sooner.", 22, 26, 416, 10, ui::Muted);
    }

    void renderOriginalLevel() {
        auto const& store = Store::get();
        heading("Original level", "Share your training progress with the original.");
        button("Back", 382, 275, 61, Home, ui::Tone::Secondary, 23);
        ui::panel(m_content, 22, 127, 416, 105);
        ui::text(m_content, "Original level ID", 34, 213, 230, 13);
        auto input = TextInput::create(220.f / .75f, "Level ID", "chatFont.fnt");
        input->setScale(.75f);
        input->setPosition({144, 182});
        input->setFilter("0123456789");
        input->setMaxCharCount(10);
        input->setString(m_originalIdText);
        input->setCallback([this](std::string const& value) { m_originalIdText = value; });
        m_content->addChild(input);
        button("Save link", 347, 182, 154, LinkOriginal, ui::Tone::Primary, 28);
        ui::text(m_content, store.originalLevelId > 0 ? fmt::format("Linked to #{}", store.originalLevelId) : "Use the original with the same gameplay layout.",
            34, 147, 300, 10, store.originalLevelId > 0 ? ui::Success : ui::Muted);
        if (store.originalLevelId > 0) button("Unlink", 395, 147, 58, UnlinkOriginal, ui::Tone::Secondary, 20);
        ui::text(m_content, "Open the original in Saved or Search.", 34, 103, 390, 13);
        ui::text(m_content, "Play Normal from 0%. Reached runs join this plan.", 34, 82, 390, 11, ui::Muted);
        ui::text(m_content, "You can also keep playing from 0% in this copy.", 34, 63, 390, 10, ui::Muted);
        button("Back to trainer", 104, 29, 164, Home, ui::Tone::Secondary, 30);
        auto exit = button("Save & exit", 320, 29, 236, SaveAndExit, ui::Tone::Primary, 32);
        auto pause = m_pause.lock();
        exit->setEnabled(pause && pause->getParent());
        if (!pause || !pause->getParent()) exit->setOpacity(90);
    }

    void renderOriginalHome() {
        auto const& store = Store::get();
        heading("Original level", store.levelName, 270);
        button("Stats", 389, 276, 49, Stats, ui::Tone::Secondary, 23);
        ui::panel(m_content, 22, 97, 416, 135);
        ui::text(m_content, "Play from the beginning", 36, 207, 384, 22);
        ui::text(m_content, "Shared progress is on", 36, 178, 384, 13, ui::Success);
        ui::text(m_content, store.profileName, 36, 155, 384, 12, ui::Muted);
        ui::interval(m_content, 36, 131, 384, 0, 100);
        ui::text(m_content, "Normal from 0%. No automatic start changes.", 22, 75, 416, 11, ui::Muted);
        ui::text(m_content, "Reached runs count toward the same training plan.", 22, 56, 416, 10, ui::Muted);
        button("View results", 104, 29, 164, Stats, ui::Tone::Normal, 30);
        button("Back to play", 320, 29, 236, ResumeOriginal, ui::Tone::Primary, 32);
    }

    bool saveOriginalLink() {
        int id = utils::numFromString<int>(m_originalIdText).unwrapOr(0);
        if (Store::get().linkOriginal(id)) return true;
        FLAlertLayer::create("Check the original ID",
            "Enter the original level's online ID. It must differ from this copy and must not already be linked to another training plan.", "OK")->show();
        return false;
    }

    CCMenuItemSpriteExtra* row(CCNode* parent, std::string const& title, std::string const& hint,
        float x, float y, float width, int action, bool checked, bool clickable = true,
        bool current = false) {
        CCNode* background;
        if (clickable) {
            background = ui::buttonFace(width, 38, current ? ui::Tone::Primary :
                checked ? ui::Tone::Selected : ui::Tone::Normal);
        } else {
            auto panel = CCScale9Sprite::create(current ? "range-current-bg.png"_spr :
                checked ? "range-completed-bg.png"_spr : "range-disabled-bg.png"_spr);
            panel->setContentSize({width, 38});
            background = panel;
        }
        auto mark = CCMenuItemToggler::createWithStandardSprites(nullptr, nullptr, .38f);
        mark->toggle(checked);
        mark->setPosition({15, 19});
        mark->setEnabled(false);
        background->addChild(mark);
        ui::text(background, title, 34, 25, width - 45, 13, ui::Text);
        ui::text(background, hint, 34, 11, width - 45, 9, clickable ? ui::Text : ui::Muted);
        auto item = CCMenuItemSpriteExtra::create(background, this, menu_selector(TrainerPopup::onAction));
        item->m_scaleMultiplier = 1.015f;
        item->setTag(action);
        item->setPosition({x, y});
        item->setEnabled(clickable);
        auto menu = ui::menu(parent);
        menu->addChild(item);
        return item;
    }

    void renderSetup() {
        auto const& store = Store::get();
        heading("Choose your starts", "Choose the parts you want to learn first.");
        button("Back", 382, 275, 61, Home, ui::Tone::Secondary, 23);
        ui::text(m_content, fmt::format("{} selected", m_selected.size()), 22, 225, 120, 13, ui::Accent);
        button("All", 283, 226, 43, SelectAll, ui::Tone::Secondary, 22);
        button("None", 331, 226, 48, SelectNone, ui::Tone::Secondary, 22);
        button("Options", 401, 226, 74, Options, ui::Tone::Normal, 23);
        map(201, true);
        m_scroll = ui::list(m_content, 22, 64, 416, 119);
        for (size_t i = 0; i < store.startPoints.size(); i += 2) {
            auto container = CCNode::create();
            container->setContentSize({416, 38});
            for (size_t j = i; j < std::min(i + 2, store.startPoints.size()); ++j) {
                auto const& p = store.startPoints[j];
                bool enabled = selected(m_selected, p.id);
                double next = 100.;
                for (auto const& point : store.startPoints)
                    if (point.percent > p.percent && selected(m_selected, point.id)) next = std::min(next, point.percent);
                row(container, p.id == 0 ? "Beginning" : ui::percent(p.percent),
                    enabled ? "to " + (next >= 100 ? std::string("Finish") : ui::percent(next)) : "Not included",
                    j % 2 ? 313 : 103, 19, 206, StartFirst + static_cast<int>(j), enabled);
            }
            m_scroll->m_contentLayer->addChild(container);
        }
        m_scroll->m_contentLayer->updateLayout();
        m_scroll->moveToTop();
        if (store.startPoints.size() == 1)
            ui::text(m_content, "Only 0% is available. Add StartPos in a local copy for shorter runs.", 22, 52, 415, 9.5f, ui::Muted);
        else ui::text(m_content, "Each run ends at the next selected start, or the finish.", 22, 52, 415, 9.5f, ui::Muted);
        ui::text(m_content, store.plan.created ? "Starting over repeats the first pass." : "One successful pass per selected run.",
            22, 26, 191, 10, store.plan.created ? ui::Current : ui::Muted);
        auto begin = button("Start first pass", 326, 27, 224, Begin, ui::Tone::Primary, 32);
        begin->setEnabled(!m_selected.empty());
        if (m_selected.empty()) begin->setOpacity(90);
    }

    void renderOptions() {
        heading("Training options", "Used when you start a new first pass.");
        button("Done", 383, 275, 60, Setup, ui::Tone::Primary, 23);
        constexpr std::array titles{"Trouble spots", "Click intensity", "Mode changes",
            "Earlier starts", "Break reminders", "Memory level"};
        constexpr std::array hints{"Prioritize repeated deaths", "Consider fast click sequences", "Include difficult transitions",
            "Practice with a longer entry", "Notifications, never pauses", "For patterns you need to memorize"};
        std::array values{m_options.deaths, m_options.inputs, m_options.transitions,
            m_options.longerEntries, m_options.restReminders, m_options.memory};
        for (int i = 0; i < 6; ++i) {
            float y = 205.f - i * 31.f;
            ui::panel(m_content, 22, y, 416, 27);
            ui::text(m_content, titles[i], 32, y + 14, 119, 12);
            ui::text(m_content, hints[i], 159, y + 14, 209, 10, ui::Muted);
            button(values[i] ? "On" : "Off", 403, y + 13.5f, 48, OptionFirst + i,
                values[i] ? ui::Tone::Selected : ui::Tone::Normal, 20);
        }
        ui::text(m_content, "Not sure? Keep the defaults and start playing.", 22, 27, 416, 11, ui::Muted);
    }

    void renderRuns() {
        auto const& store = Store::get();
        auto const& plan = store.plan;
        heading(plan.scanning ? "Your first pass" : "Training runs", plan.scanning ?
            "Passed runs are saved. The current run is highlighted." : "Checked runs are included in automatic training.");
        button("Back", 382, 275, 61, Home, ui::Tone::Secondary, 23);
        map(215);
        m_scroll = ui::list(m_content, 22, 57, 416, 139);
        if (plan.scanning) {
            for (size_t i = 0; i < plan.actualStarts.size(); ++i) {
                auto container = CCNode::create();
                container->setContentSize({416, 38});
                double end = i + 1 < plan.actualStarts.size() ? plan.actualStarts[i + 1] : 100.;
                bool passed = plan.stagePassed[i];
                bool current = !passed && static_cast<int>(i) == plan.currentStage;
                row(container, ui::range(plan.actualStarts[i], end),
                    fmt::format("{}   /   {} attempts", passed ? "Passed" : current ? "Current run" : "Waiting", plan.stageAttempts[i]),
                    208, 19, 416, 0, passed, false, current);
                m_scroll->m_contentLayer->addChild(container);
            }
        } else {
            for (auto window : planWindows()) {
                auto container = CCNode::create();
                container->setContentSize({416, 38});
                bool enabled = selected(plan.selectedIds, window->id);
                auto summary = summarize(*window, window->start <= .01 ? RunContext::FromZero : RunContext::Fresh,
                    std::nullopt, Mode::Verify, store.threshold());
                std::string hint = !enabled ? "Not included" : ready(*window) ? "Ready for full runs" :
                    fmt::format("{} passes / {} recent attempts", summary.recentSuccesses, summary.recentAttempts);
                row(container, ui::range(window->start, window->end), hint, 208, 19, 416,
                    RunFirst + window->id, enabled, true, enabled && window->id == store.activeId);
                m_scroll->m_contentLayer->addChild(container);
            }
        }
        m_scroll->m_contentLayer->updateLayout();
        m_scroll->moveToTop();
        button("Change starts", 81, 28, 118, Setup, ui::Tone::Secondary);
        button(store.enabled ? "Pause plan" : "Back to trainer", 326, 28, 224,
            store.enabled ? Stop : Home, ui::Tone::Normal, 31);
    }

    void render(bool keepScroll = false) {
        float oldScroll = keepScroll && m_scroll ? m_scroll->m_contentLayer->getPositionY() : 0;
        m_scroll = nullptr;
        if (m_content) m_content->removeFromParentAndCleanup(true);
        m_content = CCNode::create();
        m_content->setContentSize(m_size);
        m_mainLayer->addChild(m_content);
        m_menu = ui::menu(m_content);
        switch (m_page) {
            case Page::Home: renderHome(); break;
            case Page::Setup: renderSetup(); break;
            case Page::Options: renderOptions(); break;
            case Page::Runs: renderRuns(); break;
            case Page::Pace: renderPace(); break;
            case Page::OriginalLevel: renderOriginalLevel(); break;
        }
        if (keepScroll && m_scroll) {
            float bottom = std::min(0.f, m_scroll->getContentHeight() - m_scroll->m_contentLayer->getContentHeight());
            m_scroll->m_contentLayer->setPositionY(std::clamp(oldScroll, bottom, 0.f));
        }
        m_seenRevision = Store::get().revision;
        m_seenEnabled = Store::get().enabled;
        handleTouchPriority(this);
    }

    void refresh(float) {
        if (m_page == Page::Home && (m_seenRevision != Store::get().revision || m_seenEnabled != Store::get().enabled)) render();
    }

    void openSetup() {
        if (m_page != Page::Options) {
            auto const& store = Store::get();
            m_options = store.plan.created ? store.plan.options : AnalysisOptions{};
            if (!store.plan.created) m_options.restReminders = reminderMinutes() > 0;
            m_selected.clear();
            for (auto const& point : store.startPoints)
                if (!store.plan.created || selected(store.plan.selectedStartIds, point.id)) m_selected.push_back(point.id);
        }
        m_page = Page::Setup;
        render();
    }

    void play() {
        auto pause = m_pause.lock();
        onClose(nullptr);
        if (pause && pause->getParent()) pause->onResume(nullptr);
    }

    void startFirstPass() {
        auto& store = Store::get();
        if (!store.createAutoPlan(m_options, m_selected)) {
            FLAlertLayer::create("Check your starts", store.plan.status.c_str(), "OK")->show();
            return;
        }
        play();
    }

    void onAction(CCObject* sender) {
        sender->retain();
        sender->autorelease();
        int action = static_cast<CCNode*>(sender)->getTag();
        auto& store = Store::get();
        if (action >= RunFirst) {
            int id = action - RunFirst;
            if (selected(store.plan.windowIds, id)) store.selectAutoWindow(id, !selected(store.plan.selectedIds, id));
            render(true);
            return;
        }
        if (action >= StartFirst) {
            size_t index = static_cast<size_t>(action - StartFirst);
            if (index >= store.startPoints.size()) return;
            int id = store.startPoints[index].id;
            if (selected(m_selected, id)) std::erase(m_selected, id);
            else m_selected.push_back(id);
            render(true);
            return;
        }
        if (action >= OptionFirst && action < OptionFirst + 6) {
            std::array values{&m_options.deaths, &m_options.inputs, &m_options.transitions,
                &m_options.longerEntries, &m_options.restReminders, &m_options.memory};
            *values[action - OptionFirst] = !*values[action - OptionFirst];
            render();
            return;
        }
        switch (action) {
            case Home: m_page = Page::Home; render(); break;
            case Setup: openSetup(); break;
            case Options: m_page = Page::Options; render(); break;
            case Runs: m_page = Page::Runs; render(); break;
            case Pace: m_page = Page::Pace; render(); break;
            case OriginalLevel:
                m_originalIdText = store.originalLevelId > 0 ? std::to_string(store.originalLevelId) :
                    store.suggestedOriginalId > 0 ? std::to_string(store.suggestedOriginalId) : "";
                m_page = Page::OriginalLevel; render(); break;
            case LinkOriginal: if (saveOriginalLink()) render(); break;
            case UnlinkOriginal: store.unlinkOriginal(); m_originalIdText.clear(); render(); break;
            case ResumeOriginal: play(); break;
            case SaveAndExit: {
                auto pause = m_pause.lock();
                if (!pause || !pause->getParent()) return;
                if (!saveOriginalLink()) return;
                store.stopTraining();
                onClose(nullptr);
                pause->onQuit(nullptr);
                break;
            }
            case Stats: showTrainingProgress(); break;
            case Tools: showAdvancedTools(); break;
            case LessTime:
            case MoreTime:
                Mod::get()->setSettingValue<int64_t>("run-minutes", std::clamp(runMinutes() + (action == MoreTime ? 1 : -1), 1, 10));
                render();
                break;
            case ReminderOff: setReminder(0); break;
            case Reminder20: setReminder(20); break;
            case Reminder30: setReminder(30); break;
            case Next:
                store.requestNextRun();
                if (store.enabled) play();
                else render();
                break;
            case SelectAll:
                m_selected.clear();
                for (auto const& p : store.startPoints) m_selected.push_back(p.id);
                render(true); break;
            case SelectNone: m_selected.clear(); render(true); break;
            case Stop: store.stopTraining(); m_page = Page::Home; render(); break;
            case Primary:
                if (!store.enabled || (!store.plan.scanning && !store.plan.training) || (store.plan.scanning &&
                    std::find(store.plan.stagePassed.begin(), store.plan.stagePassed.end(), false) == store.plan.stagePassed.end()))
                    store.requestTraining();
                if (!store.enabled) {
                    FLAlertLayer::create("Training is paused", store.plan.status.c_str(), "OK")->show();
                    render();
                    return;
                }
                play();
                break;
            case Begin:
                if (store.plan.created) {
                    createQuickPopup("Repeat the first pass?",
                        "This replaces the current plan and repeats every selected run. Your earlier training results are kept.",
                        "Cancel", "Start over", [weak = WeakRef<TrainerPopup>(this)](FLAlertLayer*, bool confirmed) {
                            if (auto popup = weak.lock(); popup && confirmed) popup->startFirstPass();
                        });
                } else startFirstPass();
                break;
            case Help:
                FLAlertLayer::create("Three simple steps",
                    "<cp>Choose starts</c>: check the parts you want to practice.\n\n"
                    "<cy>Learn the level</c>: pass each run once. Deaths retry the same run.\n\n"
                    "<cg>Train</c>: pass the target and keep playing. Later runs count too. Five passes in a row skip the wait in <cp>Pace</c>. Starts only change between attempts.\n\n"
                    "Break reminders never pause play or appear in the first pass. Your results are saved in <cp>Stats</c>.", "Got it")->show();
                break;
        }
    }

    bool init(PauseLayer* pause) {
        if (!Popup::init(460, 300, "GJ_square01_custom.png"_spr)) return false;
        setID("context-trainer-popup"_spr);
        m_pause = WeakRef<PauseLayer>(pause);
        m_closeBtn->setScale(.6f);
        m_closeBtn->setPosition({444, 283});
        render();
        schedule(schedule_selector(TrainerPopup::refresh), .2f);
        return true;
    }

    void onClose(CCObject* sender) override {
        Store::get().save();
        Store::get().flush();
        Popup::onClose(sender);
    }

public:
    static TrainerPopup* create(PauseLayer* pause) {
        auto popup = new TrainerPopup;
        if (popup->init(pause)) { popup->autorelease(); return popup; }
        delete popup;
        return nullptr;
    }
};
} // namespace

void showTrainer(PauseLayer* pause) {
    if (auto popup = TrainerPopup::create(pause)) popup->show();
}
} // namespace context
