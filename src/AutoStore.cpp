#include "Store.hpp"
#include <Geode/ui/Notification.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>

using namespace geode::prelude;

namespace context {
namespace {
constexpr char progressSystem[] = "position-2.1";

StartPoint const* point(Store const& store, int id) {
    auto it = std::find_if(store.startPoints.begin(), store.startPoints.end(),
        [id](auto const& p) { return p.id == id; });
    return it == store.startPoints.end() ? nullptr : &*it;
}
double finiteNumber(matjson::Value const& value, double maximum = 1.e9) {
    double n = value.asDouble().unwrapOr(0);
    return std::isfinite(n) ? std::clamp(n, 0., maximum) : 0.;
}
std::vector<int> integerList(matjson::Value const& value) {
    std::vector<int> result;
    if (value.isArray()) for (auto const& item : value) {
        if (result.size() == 64) break;
        result.push_back(static_cast<int>(finiteNumber(item, 1000000)));
    }
    return result;
}
}

void Store::syncStartPoints(std::vector<StartPoint> points) {
    if (originalSession) return;
    bool changed = startPoints.size() != points.size();
    for (size_t i = 0; !changed && i < points.size(); ++i)
        changed = startPoints[i].id != points[i].id || std::abs(startPoints[i].percent - points[i].percent) > .05;
    if (plan.created && changed) {
        plan.created = false;
        plan.scanning = plan.training = enabled = false;
        pendingAction = AutoAction::None;
        plan.status = "Start positions changed. Create a new plan; old results are kept.";
    }
    startPoints = std::move(points);
    save();
}

bool Store::createAutoPlan(AnalysisOptions options, std::vector<int> const& selectedStarts) {
    if (originalSession) return false;
    if (platformer || selectedStarts.empty() || selectedStarts.size() > 64) {
        plan.status = platformer ? "Classic levels only." : "Select between 1 and 64 starts.";
        return false;
    }
    std::vector<int> ids = selectedStarts;
    for (int id : ids) if (!point(*this, id)) { plan.status = "Start position no longer exists."; return false; }
    std::sort(ids.begin(), ids.end(), [&](int a, int b) { return point(*this, a)->percent < point(*this, b)->percent; });
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (size_t i = 1; i < ids.size(); ++i) {
        if (point(*this, ids[i])->percent - point(*this, ids[i-1])->percent < .05) {
            plan.status = "Two selected starts overlap. Select one of them.";
            return false;
        }
    }
    if (windows.size() + ids.size() > 64) {
        plan.status = "Not enough run slots. Remove unused runs in Tools > Custom runs first.";
        return false;
    }
    plan = {};
    plan.created = true;
    plan.scanning = true;
    plan.options = options;
    plan.selectedStartIds = std::move(ids);
    for (int id : plan.selectedStartIds) plan.actualStarts.push_back(point(*this, id)->percent);
    plan.stagePassed.assign(plan.selectedStartIds.size(), false);
    plan.calibrated.assign(plan.selectedStartIds.size(), false);
    plan.passedEnds.assign(plan.selectedStartIds.size(), 0.);
    plan.stageAttempts.assign(plan.selectedStartIds.size(), 0);
    plan.status = "First pass: complete each selected run once.";
    enabled = true;
    block.active = false;
    returnArmed = false;
    mode = Mode::Study;
    pendingAction = AutoAction::Scan;
    ++revision;
    save();
    return true;
}

double Store::stageEnd() const {
    size_t next = static_cast<size_t>(std::max(0, plan.currentStage)) + 1;
    if (next < plan.actualStarts.size()) return plan.actualStarts[next];
    return 100.;
}

int Store::autoStartId() const {
    if (plan.scanning && plan.currentStage >= 0 &&
        static_cast<size_t>(plan.currentStage) < plan.selectedStartIds.size())
        return plan.selectedStartIds[plan.currentStage];
    if (plan.focusWindowId != 0 && plan.focusWindowId == activeId)
        return plan.focusStartId;
    auto w = std::find_if(windows.begin(), windows.end(), [&](auto const& value) { return value.id == activeId; });
    if (w == windows.end()) return 0;
    double target = w->start;
    bool repeatable = summarize(*w, RunContext::Fresh, std::nullopt, Mode::Verify, threshold()).repeatable;
    if (w->start <= .01) repeatable = summarize(*w, RunContext::FromZero, std::nullopt, Mode::Verify, threshold()).repeatable;
    int recentEntryFailures = 0;
    for (auto it = w->history.rbegin(); it != w->history.rend(); ++it) {
        if (it->mode != Mode::Verify || !it->practice || it->outcome == Outcome::Abandoned) continue;
        if (it->context == RunContext::Fresh || it->outcome == Outcome::Success) break;
        if (++recentEntryFailures >= 3) break;
    }
    if (plan.options.longerEntries && repeatable && recentEntryFailures < 3) target = std::max(0., w->start - w->leadIn);
    int result = 0;
    double best = -1;
    for (size_t i = 0; i < plan.selectedStartIds.size(); ++i) {
        double percent = plan.actualStarts[i];
        if (percent <= target + .01 && percent > best) {
            result = plan.selectedStartIds[i];
            best = percent;
        }
    }
    return result;
}

void Store::diagnosticAttempt(bool success, Survey const& survey, double reachedPercent) {
    if (!plan.created || !plan.scanning || !enabled || plan.currentStage < 0 ||
        static_cast<size_t>(plan.currentStage) >= plan.stagePassed.size()) return;
    plan.survey = survey;
    ++plan.stageAttempts[plan.currentStage];
    if (success) {
        plan.stagePassed[plan.currentStage] = true;
        plan.passedEnds[plan.currentStage] = reachedPercent;
    }
    auto next = std::find(plan.stagePassed.begin(), plan.stagePassed.end(), false);
    if (next == plan.stagePassed.end()) {
        plan.survey.completed = true;
        buildAutoPlan(plan.survey);
    } else {
        plan.currentStage = static_cast<int>(std::distance(plan.stagePassed.begin(), next));
        plan.status = fmt::format("First pass: {} / {} runs passed", std::count(plan.stagePassed.begin(), plan.stagePassed.end(), true), plan.stagePassed.size());
        save();
    }
}

bool Store::buildAutoPlan(Survey const& survey) {
    if (!plan.created || plan.stagePassed.empty() ||
        std::find(plan.stagePassed.begin(), plan.stagePassed.end(), false) != plan.stagePassed.end()) {
        plan.status = "Pass every selected run once to finish learning the level.";
        return false;
    }
    auto const& boundaries = plan.actualStarts;
    auto suggestions = analyze(survey, plan.options, boundaries);
    if (suggestions.empty() || windows.size() + suggestions.size() > 64) {
        plan.status = "Unable to create plan. Check available windows and start positions.";
        enabled = false;
        return false;
    }
    plan.survey = survey;
    plan.survey.completed = true;
    plan.windowIds.clear();
    plan.selectedIds.clear();
    int nextId = 1;
    for (auto const& w : windows) nextId = std::max(nextId, w.id + 1);
    for (auto& suggested : suggestions) {
        auto& w = suggested.window;
        w.id = nextId++;
        w.diagnosticWeight = suggested.priority;
        w.positionBased = true;
        w.note = suggested.reason.substr(0, 120);
        // Any earlier selected start must count as a different entry context.
        double previous = 0;
        for (double boundary : boundaries) if (boundary < w.start - .01) previous = std::max(previous, boundary);
        w.leadIn = std::max(.05, w.start - previous);
        plan.windowIds.push_back(w.id);
        plan.selectedIds.push_back(w.id);
        windows.push_back(std::move(w));
    }
    activeId = plan.windowIds.front();
    plan.scanning = false;
    plan.training = true;
    mode = Mode::Verify;
    enabled = true;
    returnArmed = false;
    pendingAction = AutoAction::Train;
    plan.status = "Training: the next run is selected automatically.";
    ++revision;
    save();
    Notification::create("Trainer: first pass complete. Your training is ready.", NotificationIcon::Success, 4.f)->show();
    return true;
}

void Store::selectAutoWindow(int id, bool selected) {
    if (std::find(plan.windowIds.begin(), plan.windowIds.end(), id) == plan.windowIds.end()) return;
    std::erase(plan.selectedIds, id);
    if (selected) plan.selectedIds.push_back(id);
    if (plan.selectedIds.empty()) {
        stopTraining();
        plan.status = "Select at least one training task.";
    }
    ++revision;
    save();
}

void Store::requestTraining() {
    if (!plan.created || platformer || originalSession) return;
    if (plan.scanning && !plan.stagePassed.empty() &&
        std::find(plan.stagePassed.begin(), plan.stagePassed.end(), false) == plan.stagePassed.end()) {
        block.active = false;
        if (!buildAutoPlan(plan.survey)) {
            save();
            Notification::create(plan.status, NotificationIcon::Error, 4.f)->show();
        }
        return;
    }
    if (plan.scanning || !plan.survey.completed) {
        plan.scanning = true;
        plan.training = false;
        mode = Mode::Study;
        pendingAction = AutoAction::Scan;
        plan.status = "Your first pass continues when you press Play.";
    } else {
        if (plan.selectedIds.empty()) { plan.status = "Select at least one task."; return; }
        plan.training = true;
        mode = Mode::Verify;
        pendingAction = AutoAction::Train;
        if (!active() || std::find(plan.selectedIds.begin(), plan.selectedIds.end(), activeId) == plan.selectedIds.end())
            activeId = chooseNext(windows, plan.selectedIds, threshold(), plan.options.longerEntries);
        plan.status = "Automatic training continues on resume.";
    }
    enabled = true;
    block.active = false;
    ++revision;
    save();
}

void Store::prepareTrainingRun() {
    if (!plan.training || !enabled || plan.selectedIds.empty()) return;
    bool selected = active() &&
        std::find(plan.selectedIds.begin(), plan.selectedIds.end(), activeId) != plan.selectedIds.end();
    double minimumSeconds = 60. * Mod::get()->getSettingValue<int64_t>("run-minutes");
    bool hasFocus = selected && plan.focusWindowId == activeId && plan.focusWindowId != 0;
    if (hasFocus && !plan.nextRunRequested && !plan.pacing.canSwitch(minimumSeconds)) return;

    int previousWindow = plan.focusWindowId;
    int previousStart = plan.focusStartId;
    auto candidates = plan.selectedIds;
    if ((plan.nextRunRequested || plan.pacing.consecutivePasses >= 5) && candidates.size() > 1)
        std::erase(candidates, activeId);
    if (!selected || hasFocus || plan.nextRunRequested)
        activeId = chooseNext(windows, candidates, threshold(), plan.options.longerEntries);

    // Clear the old lock only here, at the boundary of an attempt.
    plan.focusWindowId = 0;
    plan.focusStartId = autoStartId();
    plan.focusWindowId = activeId;
    bool changed = previousWindow != activeId || previousStart != plan.focusStartId;
    if (changed || plan.nextRunRequested) plan.pacing.resetRun();
    plan.nextRunRequested = false;
    if (previousWindow && changed) {
        if (auto w = active()) {
            auto start = point(*this, plan.focusStartId);
            Notification::create(fmt::format("Next run: {:.1f}% - {:.1f}%", start ? start->percent : 0., w->end),
                NotificationIcon::Info, 3.f)->show();
        }
    }
    save();
}

void Store::requestNextRun() {
    if (!plan.created || plan.scanning || plan.selectedIds.empty()) return;
    if (!enabled) requestTraining();
    if (!plan.training || !enabled) return;
    plan.nextRunRequested = true;
    pendingAction = AutoAction::Train;
    ++revision;
    save();
}

void Store::stopTraining() {
    plan.training = false;
    enabled = false;
    block.active = false;
    pendingAction = AutoAction::Stop;
    plan.status = plan.scanning ? "First pass paused. Continue when ready." : "Plan paused. Press Continue training when ready.";
    ++revision;
    save();
}

void Store::autoRunFinished(double seconds, bool firstPass, bool targetPassed) {
    ++plan.runs;
    plan.activeSeconds += std::isfinite(seconds) ? std::max(0., seconds) : 0.;
    plan.pacing.addAttempt(seconds, firstPass, targetPassed);
    if (!firstPass && plan.training && !plan.completeNotified && !plan.selectedIds.empty()) {
        bool ready = true;
        for (int id : plan.selectedIds) {
            auto w = std::find_if(windows.begin(), windows.end(), [id](auto const& v) { return v.id == id; });
            if (w == windows.end()) { ready = false; break; }
            auto fresh = summarize(*w, RunContext::Fresh, std::nullopt, Mode::Verify, threshold());
            auto zero = summarize(*w, RunContext::FromZero, std::nullopt, Mode::Verify, threshold());
            auto lead = summarize(*w, RunContext::LeadIn, std::nullopt, Mode::Verify, threshold());
            bool repeated = w->start <= .01 ? zero.repeatable : fresh.repeatable;
            bool needsTransfer = plan.options.longerEntries && w->start > .01;
            if (!repeated || (needsTransfer && !fresh.transferred && !zero.transferred && !lead.transferred)) { ready = false; break; }
        }
        if (ready) {
            plan.completeNotified = true;
            plan.status = "Selected runs are ready. Keep practicing or try full runs.";
            Notification::create("Context: plan complete. Ready for full attempts.", NotificationIcon::Success, 4.f)->show();
        }
    }
    auto minutes = Mod::get()->getSettingValue<int64_t>("break-reminder-minutes");
    if (plan.pacing.takeReminder(60. * minutes, plan.options.restReminders, firstPass))
        Notification::create(fmt::format("{} minutes of practice. Take a break when you want.", minutes),
            NotificationIcon::Info, 5.f)->show();
    save();
}

std::string Store::autoNextTask() const {
    if (plan.scanning) return fmt::format("First pass: run {} / {} to {:.1f}%", plan.currentStage + 1, plan.selectedStartIds.size(), stageEnd());
    auto w = std::find_if(windows.begin(), windows.end(), [&](auto const& value) { return value.id == activeId; });
    if (w == windows.end()) return plan.status;
    return nextTask(*w, threshold(), plan.options);
}

matjson::Value Store::saveAuto() const {
    auto j = matjson::Value();
    j["progress-system"] = progressSystem;
    j["created"] = plan.created;
    j["diagnostic"] = plan.scanning;
    j["selected-starts"] = plan.selectedStartIds;
    j["actual-starts"] = plan.actualStarts;
    j["passed-ends"] = plan.passedEnds;
    auto calibrated = matjson::Value::array();
    for (bool value : plan.calibrated) calibrated.push(value);
    j["calibrated"] = std::move(calibrated);
    auto passed = matjson::Value::array();
    for (bool value : plan.stagePassed) passed.push(value);
    j["passed"] = std::move(passed);
    j["attempts"] = plan.stageAttempts;
    j["current-stage"] = plan.currentStage;
    j["windows"] = plan.windowIds;
    j["selected-windows"] = plan.selectedIds;
    j["runs"] = plan.runs;
    j["seconds"] = plan.activeSeconds;
    auto& options = j["options"];
    options["deaths"] = plan.options.deaths;
    options["inputs"] = plan.options.inputs;
    options["transitions"] = plan.options.transitions;
    options["longer-entries"] = plan.options.longerEntries;
    options["rest-reminders"] = plan.options.restReminders;
    options["memory"] = plan.options.memory;
    auto points = matjson::Value::array();
    for (auto p : startPoints) points.push(matjson::makeObject({{"id", p.id}, {"percent", p.percent}}));
    j["start-points"] = std::move(points);
    auto& survey = j["survey"];
    survey["runs"] = plan.survey.runs;
    survey["completed"] = plan.survey.completed;
    survey["seconds"] = plan.survey.totalSeconds;
    auto bins = matjson::Value::array();
    for (auto const& bin : plan.survey.bins) {
        bins.push(matjson::makeObject({{"seconds", bin.seconds}, {"deaths", bin.deaths}, {"clicks", bin.clicks},
            {"visits", bin.visits}, {"transitions", bin.transitions}, {"checkpoints", bin.checkpoints}, {"covered", bin.covered}}));
    }
    survey["bins"] = std::move(bins);
    return j;
}

void Store::loadAuto(matjson::Value const& j) {
    if (!j.isObject() || !j["created"].asBool().unwrapOr(false)) return;
    if (j["progress-system"].asString().unwrapOr("") != progressSystem) {
        plan = {};
        plan.status = "Progress measurement changed. Create a new plan; window history is kept.";
        enabled = false;
        pendingAction = AutoAction::None;
        return;
    }
    plan.created = true;
    plan.scanning = j["diagnostic"].asBool().unwrapOr(true);
    plan.selectedStartIds = integerList(j["selected-starts"]);
    plan.stageAttempts = integerList(j["attempts"]);
    plan.windowIds = integerList(j["windows"]);
    plan.selectedIds = integerList(j["selected-windows"]);
    plan.currentStage = static_cast<int>(finiteNumber(j["current-stage"], 63));
    plan.runs = static_cast<int>(finiteNumber(j["runs"], 1000000));
    plan.activeSeconds = finiteNumber(j["seconds"]);
    if (j["passed"].isArray()) for (auto const& value : j["passed"]) {
        if (plan.stagePassed.size() == 64) break;
        plan.stagePassed.push_back(value.asBool().unwrapOr(false));
    }
    if (j["actual-starts"].isArray()) for (auto const& value : j["actual-starts"]) {
        if (plan.actualStarts.size() == 64) break;
        plan.actualStarts.push_back(finiteNumber(value, 99.99));
    }
    if (j["passed-ends"].isArray()) for (auto const& value : j["passed-ends"]) {
        if (plan.passedEnds.size() == 64) break;
        plan.passedEnds.push_back(finiteNumber(value, 100.));
    }
    if (j["calibrated"].isArray()) for (auto const& value : j["calibrated"]) {
        if (plan.calibrated.size() == 64) break;
        plan.calibrated.push_back(value.asBool().unwrapOr(false));
    }
    if (plan.selectedStartIds.empty() || plan.stagePassed.size() != plan.selectedStartIds.size() ||
        plan.stageAttempts.size() != plan.selectedStartIds.size() || plan.actualStarts.size() != plan.selectedStartIds.size() ||
        plan.passedEnds.size() != plan.selectedStartIds.size() || plan.calibrated.size() != plan.selectedStartIds.size() ||
        static_cast<size_t>(plan.currentStage) >= plan.selectedStartIds.size()) {
        plan = {};
        plan.status = "Saved plan was incomplete. Create a new plan.";
        return;
    }
    auto const& o = j["options"];
    plan.options.deaths = o["deaths"].asBool().unwrapOr(true);
    plan.options.inputs = o["inputs"].asBool().unwrapOr(true);
    plan.options.transitions = o["transitions"].asBool().unwrapOr(true);
    plan.options.longerEntries = o["longer-entries"].asBool().unwrapOr(true);
    plan.options.restReminders = o["rest-reminders"].asBool().unwrapOr(true);
    plan.options.memory = o["memory"].asBool().unwrapOr(false);
    if (j["start-points"].isArray()) for (auto const& p : j["start-points"]) {
        if (startPoints.size() == 4096) break;
        startPoints.push_back({static_cast<int>(finiteNumber(p["id"], 1000000)), finiteNumber(p["percent"], 99.99)});
    }
    auto const& s = j["survey"];
    plan.survey.runs = static_cast<int>(finiteNumber(s["runs"], 1000000));
    plan.survey.completed = s["completed"].asBool().unwrapOr(false);
    plan.survey.totalSeconds = finiteNumber(s["seconds"]);
    auto const& bins = s["bins"];
    if (bins.isArray()) for (size_t i = 0; i < std::min<size_t>(100, bins.size()); ++i) {
        auto const& b = bins[i];
        auto& target = plan.survey.bins[i];
        target.seconds = finiteNumber(b["seconds"]);
        target.deaths = static_cast<int>(finiteNumber(b["deaths"], 1000000));
        target.clicks = static_cast<int>(finiteNumber(b["clicks"], 1000000));
        target.visits = static_cast<int>(finiteNumber(b["visits"], 1000000));
        target.transitions = static_cast<int>(finiteNumber(b["transitions"], 1000000));
        target.checkpoints = static_cast<int>(finiteNumber(b["checkpoints"], 1000000));
        target.covered = b["covered"].asBool().unwrapOr(false);
    }
    plan.status = plan.scanning ? "First pass saved. Continue when ready." : "Plan saved. Continue training when ready.";
}
}

