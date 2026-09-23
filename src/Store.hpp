#pragma once
#include "Training.hpp"
#include "Analysis.hpp"
#include "StartPositions.hpp"
#include "Pacing.hpp"
#include <Geode/Geode.hpp>

namespace context {
enum class AutoAction { None, Scan, Train, Stop };
struct AutoPlan {
    bool created = false;
    bool scanning = false;
    bool training = false;
    AnalysisOptions options;
    Survey survey;
    std::vector<int> selectedStartIds;
    std::vector<double> actualStarts;
    std::vector<double> passedEnds;
    std::vector<bool> calibrated;
    std::vector<bool> stagePassed;
    std::vector<int> stageAttempts;
    int currentStage = 0;
    std::vector<int> windowIds;
    std::vector<int> selectedIds;
    std::string status;
    int runs = 0;
    double activeSeconds = 0;
    PracticePacing pacing;
    int focusWindowId = 0;
    int focusStartId = 0;
    bool nextRunRequested = false;
    bool completeNotified = false;
};
struct Block {
    bool active = false;
    int target = 8;
    int attempts = 0;
    int successes = 0;
    int moodBefore = 0;
    int moodAfter = 0;
    double seconds = 0;
};

class Store {
public:
    std::string levelKey;
    std::string levelName;
    std::string profileName;
    int currentLevelId = 0;
    bool currentIsOnline = false;
    int originalLevelId = 0;
    int suggestedOriginalId = 0;
    bool originalSession = false;
    std::vector<Window> windows;
    int activeId = 0;
    Mode mode = Mode::Verify;
    int mood = 0;
    bool enabled = false;
    bool returnArmed = false;
    bool platformer = false;
    unsigned revision = 0;
    Block block;
    AutoPlan plan;
    AutoAction pendingAction = AutoAction::None;
    std::vector<StartPoint> startPoints;

    static Store& get();
    void load(GJGameLevel* level);
    bool linkOriginal(int id);
    void unlinkOriginal();
    Window* active();
    Threshold threshold() const;
    // Same bounds update metadata; changed bounds preserve the old window/history.
    bool saveWindow(Window window);
    void select(int id);
    void removeActive();
    void setMode(Mode value);
    void setEnabled(bool value);
    void armReturn(bool value);
    void startBlock();
    void endBlock(int moodAfter);
    void record(int windowId, Attempt attempt);
    bool createAutoPlan(AnalysisOptions options, std::vector<int> const& selectedStarts);
    bool buildAutoPlan(Survey const& survey);
    void diagnosticAttempt(bool success, Survey const& survey, double reachedPercent);
    void syncStartPoints(std::vector<StartPoint> points);
    double stageEnd() const;
    int autoStartId() const;
    void selectAutoWindow(int id, bool selected);
    void requestTraining();
    void prepareTrainingRun();
    void requestNextRun();
    void stopTraining();
    void autoRunFinished(double seconds, bool firstPass, bool targetPassed = false);
    std::string autoNextTask() const;
    void save();
    void flush();
private:
    void loadAuto(matjson::Value const& value);
    matjson::Value saveAuto() const;
};

void showTrainer(PauseLayer* pause = nullptr);
void showAdvancedTools();
void showTrainingProgress();
}
