#include "Store.hpp"
#include <Geode/ui/Notification.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace geode::prelude;

namespace context {
namespace {
int number(matjson::Value const& value, int fallback = 0) {
    auto result = value.asDouble();
    if (!result || !std::isfinite(result.unwrap()) || std::floor(result.unwrap()) != result.unwrap() ||
        result.unwrap() < std::numeric_limits<int>::min() ||
        result.unwrap() > std::numeric_limits<int>::max()) return fallback;
    return static_cast<int>(result.unwrap());
}

std::string identity(GJGameLevel* level) {
    int id = level->m_levelID.value();
    std::string data = level->m_levelString;
    if (!data.empty() && data.find(';') == std::string::npos)
        data = ZipUtils::decompressString(level->m_levelString, false, 0);
    // StartPos edits must retain the same training history.
    return fmt::format("{}-{}-{:016x}", static_cast<int>(level->m_levelType), id, levelFingerprint(data));
}

matjson::Value encode(Attempt const& a) {
    auto j = matjson::Value();
    j["context"] = static_cast<int>(a.context);
    j["mode"] = static_cast<int>(a.mode);
    j["outcome"] = static_cast<int>(a.outcome);
    j["practice"] = a.practice;
    j["return"] = a.returnCheck;
    j["start"] = a.startPercent;
    j["end"] = a.endPercent;
    j["seconds"] = a.seconds;
    j["clicks"] = a.clicks;
    j["peak-cps"] = a.peakCps;
    j["timestamp"] = a.timestamp;
    return j;
}

std::optional<Attempt> decode(matjson::Value const& j) {
    int context = number(j["context"], -1);
    int mode = number(j["mode"], -1);
    int outcome = number(j["outcome"], -1);
    if (context < 0 || context > 2 || mode < 0 || mode > 1 || outcome < 0 || outcome > 2)
        return std::nullopt;
    Attempt a;
    a.context = static_cast<RunContext>(context);
    a.mode = static_cast<Mode>(mode);
    a.outcome = static_cast<Outcome>(outcome);
    a.practice = j["practice"].asBool().unwrapOr(false);
    a.returnCheck = j["return"].asBool().unwrapOr(false);
    a.startPercent = j["start"].asDouble().unwrapOr(-1);
    a.endPercent = j["end"].asDouble().unwrapOr(-1);
    a.seconds = j["seconds"].asDouble().unwrapOr(0);
    a.clicks = std::max(0, number(j["clicks"]));
    a.peakCps = std::max(0, number(j["peak-cps"]));
    double timestamp = j["timestamp"].asDouble().unwrapOr(0);
    a.timestamp = std::isfinite(timestamp) && timestamp >= 0 && timestamp <= 32503680000.
        ? static_cast<int64_t>(timestamp) : 0;
    if (!std::isfinite(a.startPercent) || !std::isfinite(a.endPercent) || !std::isfinite(a.seconds) ||
        a.startPercent < 0 || a.startPercent > 100 || a.endPercent < 0 || a.endPercent > 100 || a.seconds < 0)
        return std::nullopt;
    return a;
}
}

Store& Store::get() { static Store store; return store; }

void Store::load(GJGameLevel* level) {
    if (!levelKey.empty()) { save(); flush(); }
    levelKey = identity(level);
    levelName = level->m_levelName;
    profileName = levelName;
    currentLevelId = level->m_levelID.value();
    currentIsOnline = level->m_levelType == GJLevelType::Saved || level->m_levelType == GJLevelType::SearchResult;
    originalLevelId = 0;
    suggestedOriginalId = std::max(0, level->m_originalLevel.value());
    originalSession = false;
    platformer = level->isPlatformer();
    auto const& saved = Mod::get()->getSaveContainerConst();
    if (!platformer && currentLevelId > 0 && currentIsOnline) {
        auto source = saved["original-links"][std::to_string(currentLevelId)].asString().unwrapOr("");
        auto const& profile = saved["levels"][source];
        if (!source.empty() && source != levelKey && number(profile["original-id"]) == currentLevelId &&
            profile["auto-plan"]["created"].asBool().unwrapOr(false) &&
            !profile["auto-plan"]["diagnostic"].asBool().unwrapOr(true) && profile["windows"].isArray()) {
            levelKey = source;
            profileName = profile["name"].asString().unwrapOr(levelName);
            originalSession = true;
        }
    }
    windows.clear();
    activeId = 0;
    mode = Mode::Verify;
    enabled = false;
    mood = 0;
    returnArmed = false;
    block = {};
    plan = {};
    startPoints.clear();
    pendingAction = AutoAction::None;
    ++revision;
    auto const& data = Mod::get()->getSaveContainerConst()["levels"][levelKey];
    if (!data.isObject()) return;
    originalLevelId = std::max(0, number(data["original-id"]));
    activeId = number(data["active"]);
    mode = number(data["mode"], 1) == 0 ? Mode::Study : Mode::Verify;
    enabled = data["enabled"].asBool().unwrapOr(false) && !platformer;
    mood = std::clamp(number(data["mood"]), 0, 2);
    loadAuto(data["auto-plan"]);
    if (plan.created || originalSession) enabled = false;
    auto const& lastBlock = data["last-block"];
    block.target = std::clamp(number(lastBlock["target"], 8), 1, 100);
    block.attempts = std::clamp(number(lastBlock["attempts"]), 0, 100);
    block.successes = std::clamp(number(lastBlock["successes"]), 0, block.attempts);
    block.moodBefore = std::clamp(number(lastBlock["mood-before"]), 0, 2);
    block.moodAfter = std::clamp(number(lastBlock["mood-after"]), 0, 2);
    double seconds = lastBlock["seconds"].asDouble().unwrapOr(0);
    block.seconds = std::isfinite(seconds) ? std::max(0., seconds) : 0;
    auto const& list = data["windows"];
    if (!list.isArray()) return;
    for (auto const& j : list) {
        if (windows.size() >= 64) break;
        Window w;
        w.id = number(j["id"]);
        w.name = j["name"].asString().unwrapOr("Window").substr(0, 40);
        w.note = j["note"].asString().unwrapOr("").substr(0, 120);
        w.start = j["start"].asDouble().unwrapOr(-1);
        w.end = j["end"].asDouble().unwrapOr(-1);
        w.leadIn = j["lead-in"].asDouble().unwrapOr(5);
        w.tags = static_cast<uint32_t>(number(j["tags"])) & 63u;
        double weight = j["diagnostic-weight"].asDouble().unwrapOr(0);
        w.diagnosticWeight = std::isfinite(weight) ? std::clamp(weight, 0., 1000.) : 0.;
        w.positionBased = j["position-based"].asBool().unwrapOr(false);
        if (!validWindow(w) || w.id <= 0 || w.id >= std::numeric_limits<int>::max() ||
            std::any_of(windows.begin(), windows.end(), [&](Window const& other) { return other.id == w.id; })) continue;
        auto const& history = j["history"];
        if (history.isArray()) {
            auto first = history.size() > 1000 ? history.size() - 1000 : 0;
            for (size_t i = first; i < history.size(); ++i)
                if (auto a = decode(history[i])) w.history.push_back(*a);
        }
        windows.push_back(std::move(w));
    }
    std::erase_if(plan.windowIds, [&](int id) {
        return std::none_of(windows.begin(), windows.end(), [id](auto const& w) { return w.id == id; });
    });
    std::erase_if(plan.selectedIds, [&](int id) {
        return std::find(plan.windowIds.begin(), plan.windowIds.end(), id) == plan.windowIds.end();
    });
    if (!active() && !windows.empty()) activeId = windows.front().id;
    if (!active()) enabled = false;
}

bool Store::linkOriginal(int id) {
    if (originalSession || !plan.created || plan.scanning || plan.windowIds.empty() ||
        id <= 0 || (currentIsOnline && id == currentLevelId)) return false;
    auto& links = Mod::get()->getSaveContainer()["original-links"];
    auto key = std::to_string(id);
    auto existing = links[key].asString().unwrapOr("");
    if (!existing.empty() && existing != levelKey) return false;
    if (originalLevelId > 0 && links[std::to_string(originalLevelId)].asString().unwrapOr("") == levelKey)
        links.erase(std::to_string(originalLevelId));
    originalLevelId = id;
    links[key] = levelKey;
    save();
    flush();
    return true;
}

void Store::unlinkOriginal() {
    if (originalSession) return;
    auto& links = Mod::get()->getSaveContainer()["original-links"];
    if (originalLevelId > 0 && links[std::to_string(originalLevelId)].asString().unwrapOr("") == levelKey)
        links.erase(std::to_string(originalLevelId));
    originalLevelId = 0;
    save();
    flush();
}

Window* Store::active() {
    auto it = std::find_if(windows.begin(), windows.end(), [&](Window const& w) { return w.id == activeId; });
    return it == windows.end() ? nullptr : &*it;
}

Threshold Store::threshold() const {
    return clampThreshold({static_cast<int>(Mod::get()->getSettingValue<int64_t>("required-successes")),
                           static_cast<int>(Mod::get()->getSettingValue<int64_t>("recent-attempts"))});
}

bool Store::saveWindow(Window w) {
    if (!validWindow(w) || levelKey.empty() || platformer) return false;
    auto previous = std::find_if(windows.begin(), windows.end(), [&](Window const& old) { return old.id == w.id; });
    bool same = previous != windows.end() && previous->start == w.start && previous->end == w.end && previous->leadIn == w.leadIn;
    w.name = w.name.empty() ? "Window" : w.name.substr(0, 40);
    w.note = w.note.substr(0, 120);
    w.tags &= 63u;
    if (same) {
        // Metadata edits retain the coordinate system of the existing history.
        previous->name = w.name;
        previous->note = w.note;
        previous->tags = w.tags;
        activeId = previous->id;
    } else {
        if (windows.size() >= 64) return false;
        w.id = 1;
        for (auto const& old : windows) w.id = std::max(w.id, old.id + 1);
        w.history.clear();
        activeId = w.id;
        windows.push_back(std::move(w));
    }
    returnArmed = false;
    ++revision;
    save();
    return true;
}

void Store::select(int id) {
    if (std::none_of(windows.begin(), windows.end(), [id](Window const& w) { return w.id == id; })) return;
    activeId = id;
    returnArmed = false;
    ++revision;
    save();
}

void Store::removeActive() {
    std::erase(plan.windowIds, activeId);
    std::erase(plan.selectedIds, activeId);
    if (plan.created && !plan.scanning && plan.selectedIds.empty()) stopTraining();
    std::erase_if(windows, [&](Window const& w) { return w.id == activeId; });
    activeId = windows.empty() ? 0 : windows.front().id;
    if (windows.empty()) enabled = false;
    returnArmed = false;
    ++revision;
    save();
}

void Store::setMode(Mode value) { mode = value; ++revision; save(); }
void Store::setEnabled(bool value) { enabled = value && active() && !platformer; ++revision; save(); }
void Store::armReturn(bool value) { returnArmed = value && active() && !platformer; ++revision; }

void Store::startBlock() {
    if (!active() || platformer) return;
    block = {};
    block.active = true;
    block.moodBefore = mood;
    block.moodAfter = mood;
    block.target = static_cast<int>(Mod::get()->getSettingValue<int64_t>("block-attempts"));
    if (active()->tags & (1u << 2))
        block.target = std::min(block.target, static_cast<int>(Mod::get()->getSettingValue<int64_t>("cps-budget")));
    block.target = std::clamp(block.target, 1, 100);
    enabled = true;
    ++revision;
    save();
}

void Store::endBlock(int after) {
    block.active = false;
    block.moodAfter = mood = std::clamp(after, 0, 2);
    enabled = false;
    ++revision;
    auto& data = Mod::get()->getSaveContainer()["levels"][levelKey]["last-block"];
    data["attempts"] = block.attempts;
    data["successes"] = block.successes;
    data["target"] = block.target;
    data["mood-before"] = block.moodBefore;
    data["mood-after"] = block.moodAfter;
    data["seconds"] = block.seconds;
    save();
}

void Store::record(int windowId, Attempt a) {
    auto found = std::find_if(windows.begin(), windows.end(), [&](Window const& w) { return w.id == windowId; });
    if (found == windows.end()) return;
    found->history.push_back(a);
    if (found->history.size() > 1000) found->history.erase(found->history.begin());
    if (block.active && !plan.training) {
        if (a.outcome != Outcome::Abandoned) {
            block.seconds += a.seconds;
            ++block.attempts;
            if (a.outcome == Outcome::Success) ++block.successes;
            if (block.attempts >= block.target) {
                endBlock(mood);
                Notification::create("Context: block finished. Check how you feel in Session.", NotificationIcon::Info, 4.f)->show();
            }
        }
    }
    save();
}

void Store::save() {
    if (levelKey.empty()) return;
    auto& root = Mod::get()->getSaveContainer();
    root["schema"] = 1;
    auto& j = root["levels"][levelKey];
    j["name"] = profileName;
    j["original-id"] = originalLevelId;
    j["active"] = activeId;
    j["mode"] = static_cast<int>(mode);
    j["enabled"] = enabled;
    j["mood"] = mood;
    j["auto-plan"] = saveAuto();
    auto list = matjson::Value::array();
    for (auto const& w : windows) {
        auto item = matjson::Value();
        item["id"] = w.id;
        item["name"] = w.name;
        item["note"] = w.note;
        item["start"] = w.start;
        item["end"] = w.end;
        item["lead-in"] = w.leadIn;
        item["tags"] = w.tags;
        item["diagnostic-weight"] = w.diagnosticWeight;
        item["position-based"] = w.positionBased;
        auto history = matjson::Value::array();
        for (auto const& a : w.history) history.push(encode(a));
        item["history"] = std::move(history);
        list.push(std::move(item));
    }
    j["windows"] = std::move(list);
}

void Store::flush() {
    if (auto result = Mod::get()->saveData(); !result) {
        log::error("Could not save Context Trainer data: {}", result.unwrapErr());
        Notification::create("Context: could not save data. See Geode log.", NotificationIcon::Error)->show();
    }
}
}
