#include "Store.hpp"
#include "Integrity.hpp"
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>

using namespace geode::prelude;
using namespace context;

namespace {
std::optional<float> schedulerInputDelta;
}

class $modify(ContextPlayLayer, PlayLayer) {
    struct AutoWindow {
        int id = 0;
        RunTracker tracker;
    };
    struct Fields {
        StartPositions starts;
        AttemptIntegrity integrity;
        GameObject* disabledCheat = nullptr;
        SurveyRecorder survey;
        std::vector<AutoWindow> autoWindows;
        bool autoRun = false;
        bool originalRun = false;
        bool autoScanAtBegin = false;
        bool autoManaged = false;
        StartPosObject* autoStart = nullptr;
        bool restartQueued = false;
        bool quitting = false;
        int targetWindow = 0;
        bool targetPassed = false;
        int surveyClicks = 0;
        RunTracker tracker;
        bool ready = false;
        bool tracking = false;
        bool positionBased = false;
        bool practice = false;
        bool held[2] = {false, false};
        unsigned revision = 0;
        int windowId = 0;
        int clicks = 0;
        int peakCps = 0;
        double seconds = 0;
        float hudTimer = 0;
        std::deque<double> presses;
        CCLabelBMFont* hud = nullptr;
        CCLayerColor* hudPanel = nullptr;
        std::string status = "Open pause > Trainer";
    };

    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("PlayLayer::destroyPlayer", Priority::First))
            log::warn("Context: could not set noclip detection hook priority");
    }

    void contextClock(float incoming, float delivered, float scale) {
        if (!m_fields->ready || m_isPaused || m_levelEndAnimationStarted ||
            !m_player1 || m_player1->m_isDead) return;
        m_fields->integrity.clock(incoming, delivered, scale);
    }

    bool checkContextIntegrity() {
        auto& integrity = m_fields->integrity;
        integrity.damageFlags(m_isIgnoreDamageEnabled, m_ignoreDamage);
        integrity.clock(0, 0, CCDirector::get()->getScheduler()->getTimeScale());
        if (integrity.blocked()) m_fields->targetPassed = false;
        return !integrity.blocked();
    }

    void commitContextResults() {
        checkContextIntegrity();
        auto& store = Store::get();
        auto results = m_fields->integrity.takeResults();
        if (m_fields->revision != store.revision) return;
        for (auto const& result : results) {
            store.record(result.windowId, result.attempt);
            if (result.attempt.returnCheck) store.returnArmed = false;
            if (!store.plan.created && result.attempt.outcome == Outcome::Success &&
                Mod::get()->getSettingValue<bool>("success-notification"))
                Notification::create("Context: window passed", NotificationIcon::Success, 1.4f)->show();
        }
        if (!results.empty()) store.save();
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        Store::get().load(level);
        m_fields->survey.data() = Store::get().plan.survey;
        m_fields->ready = true;
        refreshContextStarts();
        if (!level->isPlatformer() && m_uiLayer) {
            auto panel = CCLayerColor::create({24, 22, 36, 178}, 220, 40);
            panel->setID("context-status-bg"_spr);
            m_uiLayer->addChild(panel, 100);
            m_fields->hudPanel = panel;
            auto label = CCLabelBMFont::create("", "chatFont.fnt");
            label->setID("context-status"_spr);
            label->setAnchorPoint({0, 1});
            label->setAlignment(kCCTextAlignmentLeft);
            label->setPosition({16, CCDirector::get()->getWinSize().height - 48});
            label->setColor({229, 223, 246});
            m_uiLayer->addChild(label, 101);
            m_fields->hud = label;
        }
        beginContextRun();
        return true;
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        refreshContextStarts();
    }

    void refreshContextStarts() {
        if (Store::get().originalSession) return;
        if (m_fields->ready && m_fields->starts.discover(this))
            Store::get().syncStartPoints(m_fields->starts.points());
    }

    double contextPercent() {
        if (!m_fields->positionBased) return getCurrentPercent();
        if (!m_player1 || !std::isfinite(m_levelLength) || m_levelLength <= 0) return 0.;
        return std::clamp(100.0 * m_player1->getPositionX() / m_levelLength, 0.0, 100.0);
    }

    bool autoConfigMatches(bool nativeCompleted = false) {
        auto f = m_fields.self();
        auto const& store = Store::get();
        bool managedTest = f->autoManaged && f->autoStart && m_isTestMode && m_startPosObject == f->autoStart;
        return f->revision == store.revision && store.enabled &&
            validAutoRunMode(f->practice, m_isPracticeMode, managedTest, nativeCompleted);
    }

    bool canReplayContext() {
        auto const& store = Store::get();
        return m_fields->autoManaged && !m_fields->quitting && m_levelEndAnimationStarted &&
            store.plan.created && store.enabled && (store.plan.scanning || store.plan.training);
    }

    bool originalConfigMatches() {
        return Store::get().originalSession && m_fields->revision == Store::get().revision &&
            validOriginalRunMode(m_isPracticeMode, m_isTestMode, m_startPosObject != nullptr, m_isIgnoreDamageEnabled);
    }

    void updateContextHud() {
        auto f = m_fields.self();
        if (!f->hud) return;
        auto& store = Store::get();
        bool visible = Mod::get()->getSettingValue<bool>("show-hud");
        f->hud->setVisible(visible);
        if (f->hudPanel) f->hudPanel->setVisible(visible);
        if (!visible) return;

        auto range = [](double start, double end) {
            return end >= 100. ? fmt::format("{:.1f}%-Finish", start)
                : fmt::format("{:.1f}%-{:.1f}%", start, end);
        };
        std::string text = "Trainer\nPause to create your plan";
        if (store.originalSession) {
            text = f->originalRun ? "Original | Normal from 0%\nRecording to your training plan" :
                "Original | Shared statistics\nPlay Normal from 0% to record";
        } else if (store.plan.created && (store.plan.scanning || store.plan.training || !store.enabled)) {
            if (store.plan.scanning) {
                auto passed = std::count(store.plan.stagePassed.begin(), store.plan.stagePassed.end(), true);
                auto total = store.plan.selectedStartIds.size();
                text = fmt::format("Learn | {} of {} passed", passed, total);
                if (passed == static_cast<int>(total)) {
                    text += store.enabled ? "\nRuns complete | Open Trainer" : "\nPaused | Open Trainer";
                } else {
                    auto index = static_cast<size_t>(std::max(0, store.plan.currentStage));
                    double start = index < store.plan.actualStarts.size() ? store.plan.actualStarts[index] : 0.;
                    text += fmt::format("\n{} {}", store.enabled ? "Current" : "Paused |",
                        range(start, store.stageEnd()));
                }
            } else if (auto window = store.active()) {
                text = "Train | " + range(window->start, window->end);
                if (!store.enabled) text += "\nPaused | Open Trainer";
                else if (f->autoRun && f->targetPassed) text += "\nPassed | Saves at attempt end - keep playing";
                else {
                    double progress = std::clamp((contextPercent() - window->start) /
                        std::max(.01, window->end - window->start), 0., 1.) * 100.;
                    auto selected = std::find(store.plan.selectedIds.begin(), store.plan.selectedIds.end(), window->id);
                    if (selected != store.plan.selectedIds.end()) {
                        auto task = std::distance(store.plan.selectedIds.begin(), selected) + 1;
                        text += fmt::format("\nRun {:.0f}% | Task {} of {}", progress, task, store.plan.selectedIds.size());
                    } else text += fmt::format("\nThis run {:.0f}%", progress);
                }
            } else text = "Train | No task selected\nPaused | Open Trainer";
        } else if (auto window = store.active()) {
            text = "Manual | " + range(window->start, window->end);
            if (!store.enabled) text += "\nPaused | Open Trainer";
            else if (store.block.active) {
                text += fmt::format("\n{} of {} attempts | {}", store.block.attempts,
                    store.block.target, f->tracking ? "Playing" : "Restart");
            } else if (f->status == "Inside window") {
                text += "\nPlaying this run";
            } else if (f->status == "Approaching window") {
                text += "\nApproaching this run";
            } else if (f->status == "Window passed") {
                text += "\nRun passed";
            } else if (f->status == "Start before window") {
                text += "\nRestart before this run";
            } else {
                text += "\n" + f->status;
            }
        }
        if (f->integrity.blocked())
            text = fmt::format("{} | Attempt excluded\nDisable assistance and restart to record", f->integrity.reason());
        f->hud->setString(text.c_str());
        f->hud->setColor(f->integrity.blocked() ? ccColor3B{236, 150, 149} :
            store.enabled || f->originalRun ? ccColor3B{229, 223, 246} : ccColor3B{199, 193, 216});
        f->hud->limitLabelWidth(232.f, .5f, .4f);
        if (f->hudPanel) {
            auto size = f->hud->getContentSize() * f->hud->getScale();
            f->hudPanel->setContentSize({size.width + 16, size.height + 12});
            f->hudPanel->setPosition({8, f->hud->getPositionY() - size.height - 6});
        }
    }

    void acceptContextResult(std::optional<Attempt> result) {
        if (!result) return;
        m_fields->integrity.queue(m_fields->windowId, *result);
        m_fields->tracking = false;
        m_fields->status = result->outcome == Outcome::Success ? "Window passed" :
                           result->outcome == Outcome::Failure ? "Try again" : "Restart to track";
        updateContextHud();
    }

    void finishContextRun(Outcome outcome) {
        if (!m_fields->ready || !m_fields->tracking) return;
        auto f = m_fields.self();
        acceptContextResult(f->tracker.finish(outcome, contextPercent(), f->seconds, f->clicks, f->peakCps));
        f->tracking = false;
    }

    void beginTrackedWindows(double percent, Mode mode, bool practice) {
        auto f = m_fields.self();
        auto const& store = Store::get();
        auto stamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        for (auto const& w : store.windows) {
            if (std::find(store.plan.windowIds.begin(), store.plan.windowIds.end(), w.id) == store.plan.windowIds.end()) continue;
            AutoWindow tracked;
            tracked.id = w.id;
            tracked.tracker.begin(w, percent, mode, practice, !store.originalSession && store.returnArmed && w.id == f->targetWindow, stamp);
            f->autoWindows.push_back(std::move(tracked));
        }
    }

    void beginContextRun() {
        auto f = m_fields.self();
        auto& store = Store::get();
        f->integrity.reset();
        f->disabledCheat = m_anticheatSpike;
        checkContextIntegrity();
        f->tracking = false;
        f->positionBased = false;
        f->revision = store.revision;
        f->practice = m_isPracticeMode;
        f->seconds = 0;
        f->clicks = 0;
        f->peakCps = 0;
        f->presses.clear();
        f->surveyClicks = 0;
        f->autoRun = false;
        f->originalRun = false;
        f->targetWindow = 0;
        f->targetPassed = false;
        f->autoWindows.clear();
        f->held[0] = f->held[1] = false;
        f->status = "Open pause > Trainer";
        if (store.originalSession) {
            f->positionBased = true;
            if (originalConfigMatches()) {
                f->originalRun = true;
                beginTrackedWindows(0., Mode::Verify, false);
            }
            updateContextHud();
            return;
        }
        if (store.plan.created && store.enabled && (store.plan.scanning || store.plan.training)) {
            f->autoRun = true;
            f->autoScanAtBegin = store.plan.scanning;
            f->targetWindow = store.activeId;
            f->positionBased = true;
            double percent = contextPercent();
            if (store.plan.scanning) {
                auto index = static_cast<size_t>(store.plan.currentStage);
                int revisit = calibrateStage(store.plan.actualStarts, store.plan.calibrated,
                    store.plan.stagePassed, store.plan.passedEnds, index, percent);
                if (revisit == -2) {
                    f->autoRun = false;
                    store.stopTraining();
                    store.plan.status = "Start positions overlap in gameplay. Choose a different set.";
                    updateContextHud();
                    return;
                }
                if (revisit >= 0) {
                    f->autoRun = false;
                    store.plan.currentStage = revisit;
                    store.pendingAction = AutoAction::Scan;
                    store.plan.status = "Boundary refined. Rechecking the preceding run.";
                    store.save();
                    queueContextRestart();
                    return;
                }
                f->survey.data() = store.plan.survey;
                f->survey.begin(percent, contextForm());
                f->status = fmt::format("Pass to {:.1f}%", store.stageEnd());
            } else {
                beginTrackedWindows(percent, store.mode, true);
                f->status = "Automatic practice";
            }
            updateContextHud();
            return;
        }
        auto window = store.active();
        if (f->ready && store.enabled && window && !store.platformer) {
            f->positionBased = window->positionBased;
            auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            f->tracker.begin(*window, contextPercent(), store.mode, m_isPracticeMode, store.returnArmed, stamp);
            f->tracking = f->tracker.eligible();
            f->windowId = window->id;
            f->status = f->tracking ? "Approaching window" : "Start before window";
        } else if (window) f->status = "Tracking paused";
        updateContextHud();
    }

    bool contextConfigMatches() {
        auto& store = Store::get();
        if (m_fields->revision == store.revision && m_fields->practice == m_isPracticeMode && store.enabled)
            return true;
        finishContextRun(Outcome::Abandoned);
        m_fields->status = "Restart to apply setup";
        return false;
    }

    void resetLevel() {
        if (m_fields->ready) {
            checkContextIntegrity();
            if (m_fields->originalRun) finishOriginalRun(false, false);
            if (m_fields->autoRun) finishAutoRun(false, false);
            // A manual restart after entering the window counts as a failed trial.
            if (contextConfigMatches()) finishContextRun(Outcome::Failure);
            commitContextResults();
            prepareAutoReset();
        }
        PlayLayer::resetLevel();
        if (m_fields->ready) beginContextRun();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();
        if (!f->ready || m_isPaused) return;
        // A requested restart must also work after an excluded attempt.
        if (Store::get().pendingAction != AutoAction::None && !m_levelEndAnimationStarted) {
            queueContextRestart();
            return;
        }
        if (!checkContextIntegrity()) {
            f->hudTimer += std::max(0.f, dt);
            if (f->hudTimer >= .2f) { f->hudTimer = 0; updateContextHud(); }
            return;
        }
        if (f->originalRun) {
            if (!originalConfigMatches()) finishOriginalRun(false, true);
            else if (m_levelEndAnimationStarted && m_player1 && !m_player1->m_isDead) completeContextRun();
            else if (m_player1 && !m_player1->m_isDead) {
                f->seconds += std::max(0.f, dt);
                for (auto& w : f->autoWindows)
                    recordAutoResult(w.id, w.tracker.sample(contextPercent(), f->seconds, f->clicks, f->peakCps));
            }
        } else if (f->autoRun) {
            updateAutoRun(dt);
        } else if (f->tracking && contextConfigMatches() && m_player1 && !m_player1->m_isDead) {
            f->seconds += std::max(0.f, dt);
            auto result = f->tracker.sample(contextPercent(), f->seconds, f->clicks, f->peakCps);
            if (f->tracker.entered()) f->status = "Inside window";
            acceptContextResult(result);
        }
        f->hudTimer += dt;
        if (f->hudTimer >= .2f) { f->hudTimer = 0; updateContextHud(); }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        auto f = m_fields.self();
        if (!f->ready) return;
        bool wasBlocked = f->integrity.blocked();
        // Blitzkrieg's first-object fallback handles builds without an anticheat pointer.
        if (!f->disabledCheat) f->disabledCheat = m_anticheatSpike ? m_anticheatSpike : object;
        f->integrity.collision(object == m_anticheatSpike || object == f->disabledCheat,
            player->m_isDead, m_levelEndAnimationStarted);
        checkContextIntegrity();
        if (!player->m_isDead) {
            if (!wasBlocked && f->integrity.blocked()) updateContextHud();
            return;
        }
        if (f->originalRun) finishOriginalRun(false, false);
        else if (f->autoRun) finishAutoRun(false, false);
        else if (f->tracking && contextConfigMatches()) finishContextRun(Outcome::Failure);
        commitContextResults();
    }

    void completeContextRun() {
        if (!m_fields->ready) return;
        checkContextIntegrity();
        if (m_fields->originalRun) finishOriginalRun(true, false);
        else if (m_fields->autoRun) finishAutoRun(true, false);
        else if (m_fields->tracking && contextConfigMatches()) {
            auto f = m_fields.self();
            acceptContextResult(f->tracker.sample(100, f->seconds, f->clicks, f->peakCps, true));
        }
        commitContextResults();
        Store::get().flush();
    }

    void levelComplete() {
        completeContextRun();
        PlayLayer::levelComplete();
    }

    void onQuit() {
        m_fields->quitting = true;
        if (m_fields->originalRun) finishOriginalRun(false, true);
        if (m_fields->autoRun) finishAutoRun(false, true);
        finishContextRun(Outcome::Abandoned);
        commitContextResults();
        if (m_fields->ready) {
            if (Store::get().plan.created && !Store::get().originalSession) {
                Store::get().plan.training = false;
                Store::get().enabled = false;
                Store::get().pendingAction = AutoAction::None;
                m_fields->starts.restore(this);
            }
            if (Store::get().block.active) Store::get().endBlock(Store::get().mood);
            Store::get().save();
            Store::get().flush();
        }
        PlayLayer::onQuit();
    }

    void contextButton(bool down, int button, bool playerOne) {
        auto f = m_fields.self();
        if (!f->ready || (!f->tracking && !f->autoRun && !f->originalRun) || m_isPaused || button != 1) return;
        int index = playerOne ? 0 : 1;
        bool pressed = down && !f->held[index];
        f->held[index] = down;
        if (!pressed) return;
        ++f->clicks;
        f->presses.push_back(f->seconds);
        while (!f->presses.empty() && f->presses.front() <= f->seconds - 1.) f->presses.pop_front();
        f->peakCps = std::max(f->peakCps, static_cast<int>(f->presses.size()));
    }

    int contextForm() const {
        if (!m_player1) return 0;
        if (m_player1->m_isShip) return 1;
        if (m_player1->m_isBall) return 2;
        if (m_player1->m_isBird) return 3;
        if (m_player1->m_isDart) return 4;
        if (m_player1->m_isRobot) return 5;
        if (m_player1->m_isSpider) return 6;
        if (m_player1->m_isSwing) return 7;
        return 0;
    }

    void queueContextRestart() {
        if (m_fields->restartQueued || m_fields->quitting) return;
        m_fields->restartQueued = true;
        Loader::get()->queueInMainThread([weak = WeakRef<PlayLayer>(this)] {
            auto play = weak.lock();
            if (!play || play.data() != PlayLayer::get()) return;
            auto self = static_cast<ContextPlayLayer*>(play.data());
            self->m_fields->restartQueued = false;
            if (self->m_fields->quitting || self->m_isPaused || self->m_levelEndAnimationStarted) return;
            self->resetLevel();
        });
    }

    void prepareAutoReset() {
        auto& store = Store::get();
        auto f = m_fields.self();
        if (store.plan.created && store.enabled && (store.plan.scanning || store.plan.training)) {
            if (!m_isPracticeMode) togglePracticeMode(true);
            if (store.plan.training) store.prepareTrainingRun();
            if (!f->starts.prepareReset(this, store.autoStartId())) {
                store.stopTraining();
                store.plan.status = "This StartPos is unavailable. Reopen the level and create a plan.";
            } else {
                f->autoManaged = true;
                f->autoStart = m_startPosObject;
                store.pendingAction = AutoAction::None;
                return;
            }
        }
        if (f->autoManaged) {
            f->starts.restore(this);
            f->autoManaged = false;
            f->autoStart = nullptr;
        }
        store.pendingAction = AutoAction::None;
    }

    void recordAutoResult(int id, std::optional<Attempt> const& result) {
        if (!result) return;
        if (id == m_fields->targetWindow && result->outcome == Outcome::Success)
            m_fields->targetPassed = true;
        m_fields->integrity.queue(id, *result);
    }

    void finishTrackedWindows(bool completed, bool abandoned) {
        auto f = m_fields.self();
        for (auto& w : f->autoWindows) {
            auto result = completed && !abandoned
                ? w.tracker.sample(100., f->seconds, f->clicks, f->peakCps, true)
                : w.tracker.finish(abandoned ? Outcome::Abandoned : Outcome::Failure,
                    contextPercent(), f->seconds, f->clicks, f->peakCps);
            recordAutoResult(w.id, result);
        }
    }

    void finishOriginalRun(bool completed, bool abandoned) {
        if (!m_fields->originalRun) return;
        checkContextIntegrity();
        abandoned = abandoned || !originalConfigMatches() || m_fields->integrity.blocked();
        m_fields->originalRun = false;
        finishTrackedWindows(completed, abandoned);
        commitContextResults();
        Store::get().save();
        updateContextHud();
    }

    void finishAutoRun(bool completed, bool abandoned) {
        auto f = m_fields.self();
        if (!f->autoRun) return;
        auto& store = Store::get();
        checkContextIntegrity();
        abandoned = abandoned || !autoConfigMatches(completed) || f->integrity.blocked();
        double percent = completed ? 100. : contextPercent();
        f->autoRun = false;
        if (f->autoScanAtBegin) {
            if (abandoned) {
                f->survey.cancelRun();
                f->survey.data() = store.plan.survey;
            } else {
                bool passed = completed || (store.stageEnd() < 100. && percent >= store.stageEnd());
                if (completed) f->survey.complete();
                else if (!passed) f->survey.died(percent);
                else f->survey.cancelRun();
                store.diagnosticAttempt(passed, f->survey.data(), percent);
                store.autoRunFinished(f->seconds, true);
            }
        } else {
            finishTrackedWindows(completed, abandoned);
            commitContextResults();
            if (f->integrity.blocked()) store.plan.pacing.consecutivePasses = 0;
            if (!abandoned) store.autoRunFinished(f->seconds, false, f->targetPassed);
        }
        f->status = store.enabled ? "Next attempt is ready" : "Training paused";
        store.save();
        updateContextHud();
    }

    void updateAutoRun(float dt) {
        auto f = m_fields.self();
        auto& store = Store::get();
        bool nativeCompleted = m_levelEndAnimationStarted && m_player1 && !m_player1->m_isDead;
        if (!autoConfigMatches(nativeCompleted)) {
            finishAutoRun(false, true);
            if (store.enabled) queueContextRestart();
            return;
        }
        if (!m_player1 || m_player1->m_isDead) return;
        if (nativeCompleted) {
            completeContextRun();
            return;
        }
        f->seconds += std::max(0.f, dt);
        double percent = contextPercent();
        if (store.plan.scanning) {
            f->survey.sample(percent, dt, contextForm(), f->clicks - f->surveyClicks);
            f->surveyClicks = f->clicks;
            if (store.stageEnd() < 100. && percent >= store.stageEnd()) {
                finishAutoRun(false, false);
                queueContextRestart();
            }
            return;
        }
        for (auto& w : f->autoWindows) {
            auto result = w.tracker.sample(percent, f->seconds, f->clicks, f->peakCps);
            recordAutoResult(w.id, result);
        }
    }

    CheckpointObject* markCheckpoint() {
        auto checkpoint = PlayLayer::markCheckpoint();
        if (checkpoint && m_fields->ready && m_fields->autoRun && Store::get().plan.scanning)
            m_fields->survey.checkpoint(contextPercent());
        return checkpoint;
    }
};

// Bracket other scheduler hooks so their delta multiplier is observed before gameplay runs.
class $modify(ContextSchedulerInput, CCScheduler) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("cocos2d::CCScheduler::update", Priority::First))
            log::warn("Context: could not set speedhack input hook priority");
    }
    void update(float dt) {
        auto previous = schedulerInputDelta;
        schedulerInputDelta = dt;
        CCScheduler::update(dt);
        schedulerInputDelta = previous;
    }
};

class $modify(ContextSchedulerGuard, CCScheduler) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("cocos2d::CCScheduler::update", Priority::Last))
            log::warn("Context: could not set speedhack output hook priority");
    }
    void update(float dt) {
        if (auto play = PlayLayer::get(); play && schedulerInputDelta &&
            this == CCDirector::get()->getScheduler())
            static_cast<ContextPlayLayer*>(play)->contextClock(*schedulerInputDelta, dt, getTimeScale());
        CCScheduler::update(dt);
    }
};

class $modify(ContextInput, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool playerOne) {
        GJBaseGameLayer::handleButton(down, button, playerOne);
        if (static_cast<GJBaseGameLayer*>(this) == static_cast<GJBaseGameLayer*>(PlayLayer::get()))
            static_cast<ContextPlayLayer*>(PlayLayer::get())->contextButton(down, button, playerOne);
    }
};

class $modify(ContextPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (!PlayLayer::get()) return;
        if (auto menu = getChildByID("right-button-menu")) {
            auto sprite = CircleButtonSprite::createWithSprite(
                "logo.png"_spr, 1.f, CircleBaseColor::DarkPurple, CircleBaseSize::Small);
            auto button = CCMenuItemSpriteExtra::create(
                sprite, this, menu_selector(ContextPauseLayer::onContext));
            button->setID("context-button"_spr);
            menu->addChild(button);
            menu->updateLayout();
        }
        Store::get().save();
        Store::get().flush();
    }

    void onContext(CCObject*) {
        if (auto play = PlayLayer::get())
            static_cast<ContextPlayLayer*>(play)->refreshContextStarts();
        showTrainer(this);
    }
};

class $modify(ContextEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        if (m_playLayer && m_playLayer == PlayLayer::get() && m_playLayer->m_levelEndAnimationStarted &&
            m_playLayer->m_player1 && !m_playLayer->m_player1->m_isDead)
            static_cast<ContextPlayLayer*>(m_playLayer)->completeContextRun();
        if (m_playLayer && m_playLayer == PlayLayer::get() &&
            static_cast<ContextPlayLayer*>(m_playLayer)->canReplayContext())
            scheduleOnce(schedule_selector(ContextEndLevelLayer::continueTraining), .8f);
    }

    void continueTraining(float) {
        if (m_playLayer && m_playLayer == PlayLayer::get() &&
            static_cast<ContextPlayLayer*>(m_playLayer)->canReplayContext())
            onReplay(nullptr);
    }
};

