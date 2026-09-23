#include "Store.hpp"
#include "TrainerUI.hpp"

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>

using namespace geode::prelude;

namespace context {
namespace {

class AdvancedPopup final : public Popup {
    enum Action {
        Runs = 1, Session, Help, Previous, Next, NewRun, Save, UseRun, DeleteRun, Close,
        PauseAutomatic, Tracking, ChangeMode, ReturnCheck, BeginBlock, EndBlock, BackFromHelp,
        MoodFirst = 30, TagFirst = 40
    };

    int m_tab = Runs;
    bool m_showHelp = false;
    int m_viewedId = 0;
    std::string m_levelKey;
    Window m_draft;
    std::array<std::string, 5> m_values;
    std::array<std::string, 5> m_savedValues;
    std::uint32_t m_savedTags = 0;
    std::array<TextInput*, 5> m_inputs{};
    CCNode* m_body = nullptr;
    CCMenu* m_menu = nullptr;
    CCLabelBMFont* m_feedback = nullptr;
    std::string m_message;
    bool m_error = false;

    Window const* viewed() const {
        auto const& windows = Store::get().windows;
        auto found = std::find_if(windows.begin(), windows.end(),
            [this](auto const& run) { return run.id == m_viewedId; });
        return found == windows.end() ? nullptr : &*found;
    }

    void clearInputs() {
        for (auto input : m_inputs) if (input) input->defocus();
        m_inputs.fill(nullptr);
    }

    void loadDraft(int id) {
        clearInputs();
        m_viewedId = id;
        auto run = viewed();
        m_draft = run ? *run : Window{};
        if (!run) {
            m_viewedId = 0;
            m_draft.name = fmt::format("Run {}", Store::get().windows.size() + 1);
        }
        m_values = {m_draft.name, m_draft.note, fmt::format("{:g}", m_draft.start),
            fmt::format("{:g}", m_draft.end), fmt::format("{:g}", m_draft.leadIn)};
        m_savedValues = m_values;
        m_savedTags = m_draft.tags;
        m_message.clear();
    }

    void captureDraft() {
        for (std::size_t i = 0; i < m_inputs.size(); ++i)
            if (m_inputs[i]) m_values[i] = m_inputs[i]->getString();
    }

    bool dirty() const { return m_values != m_savedValues || m_draft.tags != m_savedTags; }

    CCMenuItemSpriteExtra* button(std::string const& title, float x, float y, float width,
        int action, ui::Tone tone = ui::Tone::Normal, bool enabled = true, float height = 24,
        char const* captionFont = "bigFont.fnt") {
        auto item = ui::button(m_menu, title, x, y, width, height, this,
            menu_selector(AdvancedPopup::onAction), action, tone, captionFont);
        item->setEnabled(enabled);
        if (!enabled) item->setOpacity(100);
        return item;
    }

    void input(std::size_t index, char const* title, float x, float y, float width) {
        ui::text(m_body, title, x - width / 2, y + 16, width, 9, ui::Muted);
        auto field = TextInput::create(width / .7f, title, "chatFont.fnt");
        field->setScale(.7f);
        field->setPosition({x, y});
        field->setMaxCharCount(index == 0 ? 40 : index == 1 ? 120 : 7);
        if (index >= 2) field->setFilter("0123456789.");
        else field->setCommonFilter(CommonFilter::Any);
        field->setString(m_values[index]);
        field->setCallback([this, index](std::string const& value) {
            m_values[index] = value;
            feedback("Unsaved changes.");
        });
        m_body->addChild(field);
        m_inputs[index] = field;
    }

    void feedback(std::string value, bool error = false) {
        m_message = std::move(value);
        m_error = error;
        if (m_feedback) {
            m_feedback->setString(m_message.c_str());
            m_feedback->setColor(error ? ui::Danger : ui::Success);
            m_feedback->limitLabelWidth(416, .3f, .2f);
        }
    }

    bool init() override {
        if (!Popup::init(460, 300, "GJ_square01_custom.png"_spr)) return false;
        setID("context-tools-popup");
        m_closeBtn->setScale(.6f);
        m_closeBtn->setPosition({444, 283});
        m_levelKey = Store::get().levelKey;
        loadDraft(Store::get().activeId);
        ui::text(m_mainLayer, "Tools", 22, 277, 110, 20, ui::Text, true, "bigFont.fnt");
        render();
        return true;
    }

    void render() {
        captureDraft();
        clearInputs();
        m_feedback = nullptr;
        if (m_body) m_body->removeFromParentAndCleanup(true);
        m_body = CCNode::create();
        m_mainLayer->addChild(m_body);
        m_menu = ui::menu(m_body);
        button("Custom runs", 225, 277, 118, Runs,
            !m_showHelp && m_tab == Runs ? ui::Tone::Selected : ui::Tone::Normal, true, 22);
        button("Session", 337, 277, 94, Session,
            !m_showHelp && m_tab == Session ? ui::Tone::Selected : ui::Tone::Normal, true, 22);
        ui::iconButton(m_menu, "GJ_infoIcon_001.png", 403, 277, 22, this,
            menu_selector(AdvancedPopup::onAction), Help);
        ui::line(m_body, 22, 258, 416);
        if (m_showHelp) renderHelp();
        else if (m_tab == Runs) renderRuns();
        else renderSession();
        handleTouchPriority(this);
    }

    void renderHelp() {
        ui::text(m_body, "Custom runs & sessions", 22, 239, 416, 16);

        ui::panel(m_body, 22, 167, 416, 56);
        ui::text(m_body, "Editing a run", 32, 207, 396, 12, ui::Accent);
        ui::text(m_body, "Save changes keeps your edits. Pause training to edit.", 32, 190, 396, 10);
        ui::text(m_body, "A new range or lead-in creates a run and keeps earlier results.", 32, 176, 396, 10, ui::Muted);

        ui::panel(m_body, 22, 99, 416, 60);
        ui::text(m_body, "Lead-in: an earlier start", 32, 143, 396, 12, ui::Accent);
        ui::text(m_body, "Run 60-70%, lead-in 5%: start at 55% or earlier.", 32, 126, 396, 10);
        ui::text(m_body, "Starting inside the run cannot confirm its missing beginning.", 32, 110, 396, 10, ui::Muted);

        ui::panel(m_body, 22, 40, 416, 51);
        ui::text(m_body, "Sessions and results", 32, 76, 396, 12, ui::Accent);
        ui::text(m_body, "Normal + Practice share progress; Study and Verify stay separate.", 32, 60, 396, 10);
        ui::text(m_body, "Without an automatic plan: Use this run, then open Session.", 32, 46, 396, 10, ui::Muted);

        button(m_tab == Runs ? "Back to editor" : "Back to session", 230, 21, 220,
            BackFromHelp, ui::Tone::Primary, true, 24);
    }

    void renderRuns() {
        auto const& store = Store::get();
        auto const run = viewed();
        auto index = std::find_if(store.windows.begin(), store.windows.end(),
            [this](auto const& entry) { return entry.id == m_viewedId; });
        ui::iconButton(m_menu, "GJ_arrow_01_001.png", 243, 239, 23, this,
            menu_selector(AdvancedPopup::onAction), Previous, false, !store.windows.empty());
        ui::iconButton(m_menu, "GJ_arrow_01_001.png", 272, 239, 23, this,
            menu_selector(AdvancedPopup::onAction), Next, true, !store.windows.empty());
        ui::text(m_body, run ? fmt::format("Run {} of {}", std::distance(store.windows.begin(), index) + 1,
            store.windows.size()) : "New custom run", 22, 241, 172, 14);
        if (run && store.activeId == m_viewedId)
            ui::text(m_body, "Current run", 22, 229, 172, 8, ui::Accent);
        button("New run", 385, 239, 106, NewRun, ui::Tone::Normal, true, 23);

        bool automatic = store.plan.created && store.enabled;
        bool editable = !store.platformer && !automatic;
        ui::panel(m_body, 22, 195, 416, 29);
        if (automatic) {
            ui::text(m_body, "Pause training to edit. Your progress stays saved.", 32, 209, 255, 9, ui::Muted);
            button("Pause to edit", 366, 209, 124, PauseAutomatic, ui::Tone::Primary, true, 21);
        } else {
            ui::text(m_body, store.platformer ? "Percentage runs are available in classic levels."
                : "Changing the range creates a new run and keeps earlier results.",
                32, 209, 396, 10, ui::Muted);
        }

        ui::panel(m_body, 22, 57, 280, 131);
        input(0, "Run name", 162, 157, 256);
        input(1, "Cue or reminder (optional)", 162, 114, 256);
        input(2, "Start %", 73, 71, 78);
        input(3, "End %", 162, 71, 78);
        input(4, "Lead-in %", 251, 71, 78);

        ui::panel(m_body, 310, 57, 128, 131);
        ui::text(m_body, "Training focus", 320, 173, 108, 11);
        constexpr std::array names{"Memory", "Precision", "CPS", "Transition", "Endurance", "Pressure"};
        for (int i = 0; i < 6; ++i)
            button(names[i], 345.f + 58.f * (i % 2), 151.f - 25.f * (i / 2), 54, TagFirst + i,
                m_draft.tags & (1u << i) ? ui::Tone::Selected : ui::Tone::Normal, true, 20, "chatFont.fnt");
        ui::text(m_body, "Select any that fit.", 374, 76, 108, 9, ui::Muted, false);

        m_feedback = ui::text(m_body, m_message.empty() ? "Lead-in: how far before the run an earlier start begins." : m_message,
            22, 46, 416, 9,
            m_message.empty() ? ui::Muted : m_error ? ui::Danger : ui::Success);
        button("Save changes", 95, 25, 146, Save, ui::Tone::Primary, editable);
        if (!store.plan.created)
            button("Use this run", 248, 25, 140, UseRun, ui::Tone::Normal, editable && run);
        else
            ui::text(m_body, "The plan selects runs.", 188, 25, 150, 9, ui::Muted);
        button("Delete", 402, 25, 72, DeleteRun, ui::Tone::Destructive, editable && run, 20);
    }

    void renderSession() {
        auto& store = Store::get();
        if (store.plan.created) {
            ui::panel(m_body, 22, 135, 416, 107);
            ui::text(m_body, "Automatic training", 36, 222, 260, 16);
            ui::text(m_body, store.enabled ? "Active" : "Paused", 356, 222, 70, 11,
                store.enabled ? ui::Success : ui::Current);
            ui::text(m_body, "Your plan chooses the next run and where to start.", 36, 197, 388, 11, ui::Muted);
            ui::text(m_body, "Pausing keeps all runs and recorded progress.", 36, 180, 388, 10, ui::Muted);
            button(store.enabled ? "Pause training" : "Training paused", 120, 154, 168,
                PauseAutomatic, ui::Tone::Primary, store.enabled);
            ui::panel(m_body, 22, 59, 416, 65);
            ui::text(m_body, "Edit your runs", 36, 105, 230, 13);
            ui::text(m_body, "Adjust a range, reminder or training focus.", 36, 84, 230, 10, ui::Muted);
            button("Custom runs", 354, 91, 140, Runs);
            ui::text(m_body, "Resume your plan from the main Training screen.", 22, 35, 416, 10, ui::Muted);
            return;
        }
        if (store.platformer || !store.active()) {
            ui::text(m_body, store.platformer ? "Classic levels only" : "Choose a custom run first", 230, 188, 410, 18, ui::Text, false);
            ui::text(m_body, store.platformer ? "Percentage-based sessions do not support platformer levels."
                : "Save a run, then select Use this run in Custom runs.", 230, 158, 410, 11, ui::Muted, false);
            if (!store.platformer) button("Open custom runs", 230, 117, 226, Runs, ui::Tone::Primary);
            return;
        }
        auto const* active = store.active();
        ui::text(m_body, active->name, 22, 239, 270, 13);
        ui::text(m_body, ui::range(active->start, active->end), 306, 239, 132, 11, ui::Current);
        ui::panel(m_body, 22, 146, 416, 76);
        ui::text(m_body, "Record attempts", 34, 201, 240, 12);
        button(store.enabled ? "Tracking on" : "Tracking off", 352, 201, 146, Tracking,
            store.enabled ? ui::Tone::Selected : ui::Tone::Normal, true, 23);
        ui::text(m_body, "Attempt type", 34, 176, 240, 11);
        ui::text(m_body, store.mode == Mode::Study ? "Study records learning attempts."
            : "Verify checks consistency without aids.", 34, 160, 240, 9, ui::Muted);
        button(store.mode == Mode::Study ? "Study" : "Verify", 352, 171, 146, ChangeMode, ui::Tone::Normal, true, 23);

        ui::panel(m_body, 22, 76, 416, 61);
        ui::text(m_body, "How do you feel?", 34, 119, 147, 10, ui::Muted);
        constexpr std::array moods{"Normal", "Tense", "Tired"};
        for (int i = 0; i < 3; ++i)
            button(moods[i], 216.f + 72.f * i, 119, 66, MoodFirst + i,
                store.mood == i ? ui::Tone::Selected : ui::Tone::Normal, true, 20);
        button(store.returnArmed ? "Return check armed" : "Check after a break", 116, 91, 164, ReturnCheck,
            store.returnArmed ? ui::Tone::Selected : ui::Tone::Normal, true, 21);
        ui::text(m_body, "Marks your next full-run attempt.", 211, 91, 213, 9, ui::Muted);
        std::string summary = fmt::format("{} / {} attempts  /  {} passed  /  {:.0f}s", store.block.attempts,
            store.block.target, store.block.successes, store.block.seconds);
        if (!store.block.active && !store.block.attempts) {
            int budget = static_cast<int>(Mod::get()->getSettingValue<int64_t>("block-attempts"));
            if (active->tags & (1u << 2)) budget = std::min(budget,
                static_cast<int>(Mod::get()->getSettingValue<int64_t>("cps-budget")));
            summary = fmt::format("A block lasts {} attempts. Adjust this in mod settings.", std::clamp(budget, 1, 100));
        }
        ui::text(m_body, summary, 22, 62, 416, 10, ui::Muted);
        button(store.block.active ? "Block in progress" : "Start block", 122, 38, 200, BeginBlock,
            ui::Tone::Primary, !store.block.active);
        button(store.block.active ? "End block + save mood" : "Save after-block mood", 338, 38, 200, EndBlock);
        ui::text(m_body, "Changes apply on restart. Ending a block stops tracking.", 22, 17, 416, 9, ui::Muted);
    }

    static std::optional<double> number(std::string const& value) {
        char* end = nullptr;
        double result = std::strtod(value.c_str(), &end);
        if (value.empty() || end != value.c_str() + value.size() || !std::isfinite(result)) return std::nullopt;
        return result;
    }

    bool saveDraft() {
        auto& store = Store::get();
        if (store.platformer || (store.plan.created && store.enabled)) return false;
        captureDraft();
        auto start = number(m_values[2]), end = number(m_values[3]), entry = number(m_values[4]);
        if (!start || !end || !entry) { feedback("Enter valid numbers for start, end and entry distance.", true); return false; }
        auto draft = m_draft;
        draft.name = m_values[0];
        draft.note = m_values[1];
        draft.start = *start;
        draft.end = *end;
        draft.leadIn = *entry;
        if (draft.name.find_first_not_of(" \t\r\n") == std::string::npos) { feedback("Give this run a short name.", true); return false; }
        if (!validWindow(draft)) { feedback("Need 0 <= start < end <= 100 and 0 < entry distance <= 100.", true); return false; }
        int previousActive = store.activeId;
        if (!store.saveWindow(std::move(draft))) { feedback("Could not save. A level can hold up to 64 runs.", true); return false; }
        int saved = store.activeId;
        // Saving a draft must not also select it as the user's current run.
        store.activeId = previousActive;
        store.save();
        loadDraft(saved);
        render();
        feedback(store.plan.created ? "Saved. Automatic training selects its own runs." : "Saved. Select Use this run when you are ready.");
        return true;
    }

    void navigate(int action) {
        if (action == Close) { clearInputs(); Store::get().flush(); Popup::onClose(nullptr); return; }
        if (action == NewRun) {
            loadDraft(0);
        } else {
            auto const& windows = Store::get().windows;
            if (windows.empty()) return;
            auto found = std::find_if(windows.begin(), windows.end(),
                [this](auto const& run) { return run.id == m_viewedId; });
            int count = static_cast<int>(windows.size());
            int index = found == windows.end() ? (action == Next ? -1 : 0)
                : static_cast<int>(std::distance(windows.begin(), found));
            index = (index + (action == Next ? 1 : count - 1)) % count;
            loadDraft(windows[index].id);
        }
        render();
    }

    void requestNavigate(int action) {
        captureDraft();
        if (!dirty()) { navigate(action); return; }
        createQuickPopup("Unsaved changes", "Discard this draft? Your saved runs and results will stay unchanged.",
            "Keep editing", "Discard", [weak = WeakRef<AdvancedPopup>(this), action](FLAlertLayer*, bool confirmed) {
                auto popup = weak.lock();
                if (confirmed && popup && popup->getParent() && Store::get().levelKey == popup->m_levelKey)
                    popup->navigate(action);
            });
    }

    void deleteViewed() {
        auto& store = Store::get();
        if (!viewed() || store.platformer || (store.plan.created && store.enabled)) return;
        int id = m_viewedId;
        createQuickPopup("Delete run?", fmt::format("Delete this run and its {} saved results? This cannot be undone.", viewed()->history.size()),
            "Cancel", "Delete", [weak = WeakRef<AdvancedPopup>(this), id](FLAlertLayer*, bool confirmed) {
                auto popup = weak.lock();
                if (!confirmed || !popup || !popup->getParent()) return;
                auto& current = Store::get();
                if (current.levelKey != popup->m_levelKey || (current.plan.created && current.enabled) ||
                    std::none_of(current.windows.begin(), current.windows.end(), [id](auto const& run) { return run.id == id; })) return;
                int previousActive = current.activeId;
                current.select(id);
                current.removeActive();
                if (previousActive != id) { current.activeId = previousActive; current.save(); }
                popup->loadDraft(current.activeId);
                popup->render();
                popup->feedback("Run and its results deleted.");
            });
    }

    void onAction(CCObject* sender) {
        sender->retain();
        sender->autorelease();
        int action = static_cast<CCNode*>(sender)->getTag();
        auto& store = Store::get();
        if (store.levelKey != m_levelKey) { navigate(Close); return; }
        captureDraft();
        if (action >= TagFirst && action < TagFirst + 6) {
            m_draft.tags ^= 1u << (action - TagFirst);
            m_message = "Unsaved changes.";
            m_error = false;
            render();
            return;
        }
        if (action == Runs || action == Session) { m_showHelp = false; m_tab = action; render(); return; }
        if (action == BackFromHelp) { m_showHelp = false; render(); return; }
        if (action == Previous || action == Next || action == NewRun) { requestNavigate(action); return; }
        if (action == Save) { saveDraft(); return; }
        if (action == DeleteRun) { deleteViewed(); return; }
        if (action == Help) {
            m_showHelp = !m_showHelp;
            render();
            return;
        }
        if (action == PauseAutomatic) {
            if (store.plan.created && store.enabled) store.stopTraining();
            render();
            return;
        }
        if (store.plan.created || store.platformer) return;
        if (action == UseRun) {
            if (!viewed()) return;
            if (dirty()) { feedback("Save your changes before selecting this run.", true); return; }
            store.select(m_viewedId);
            render();
            feedback("Selected. Open Session to start manual tracking.");
            return;
        }
        if (!store.active()) return;
        if (action >= MoodFirst && action < MoodFirst + 3) {
            store.mood = action - MoodFirst;
            store.save();
        } else if (action == Tracking) store.setEnabled(!store.enabled);
        else if (action == ChangeMode) store.setMode(store.mode == Mode::Study ? Mode::Verify : Mode::Study);
        else if (action == ReturnCheck) store.armReturn(!store.returnArmed);
        else if (action == BeginBlock && !store.block.active) store.startBlock();
        else if (action == EndBlock) store.endBlock(store.mood);
        else return;
        render();
    }

    void onClose(CCObject*) override {
        if (m_showHelp) { m_showHelp = false; render(); return; }
        requestNavigate(Close);
    }

public:
    static AdvancedPopup* create() {
        auto popup = new AdvancedPopup;
        if (popup->init()) { popup->autorelease(); return popup; }
        delete popup;
        return nullptr;
    }
};

} // namespace

void showAdvancedTools() {
    if (auto popup = AdvancedPopup::create()) popup->show();
}

} // namespace context
