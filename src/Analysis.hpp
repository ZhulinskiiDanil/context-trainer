#pragma once

#include "Training.hpp"
#include <array>

namespace context {

struct AnalysisOptions {
    bool deaths = true;
    bool inputs = true;
    bool transitions = true;
    bool longerEntries = true;
    bool restReminders = true;
    bool memory = false;
};

struct SurveyBin {
    double seconds = 0;
    int deaths = 0;
    int clicks = 0;
    int visits = 0;
    int transitions = 0;
    int checkpoints = 0;
    bool covered = false;
};

struct Survey {
    std::array<SurveyBin, 100> bins{};
    int runs = 0;
    bool completed = false;
    double totalSeconds = 0;
};

class SurveyRecorder {
public:
    void begin(double actualStart, int form);
    void sample(double percent, double dt, int form, int newClicks);
    void died(double percent);
    void checkpoint(double percent);
    void complete();
    Survey& data() { return m_data; }
    Survey const& data() const { return m_data; }
    void reset();
    void cancelRun();

private:
    void observe(int index);
    Survey m_data;
    std::array<bool, 100> m_visited{};
    double m_start = 0;
    double m_previous = 0;
    int m_form = -1;
    bool m_active = false;
};

struct SuggestedWindow {
    Window window;
    double priority = 0;
    int deaths = 0;
    double observedSeconds = 0;
    std::string reason;
};

// Approximate percentage observed, at one-percent resolution.
double coverage(Survey const& survey);
// Returns -2 for invalid input, -1 for a calibrated stage, or the predecessor to replay.
// Invalid input leaves every vector unchanged. Unknown neighboring estimates are ignored.
int calibrateStage(std::vector<double>& starts, std::vector<bool>& calibrated,
                   std::vector<bool>& passed, std::vector<double> const& passedEnds,
                   std::size_t index, double actual);
// New proposals have id=0 and no training history; Store assigns persistent IDs.
// When supplied, selected StartPos boundaries determine the exact window edges.
std::vector<SuggestedWindow> analyze(Survey const& survey, AnalysisOptions const& options,
                                   std::vector<double> const& boundaries = {});
// Empty selections produce 0. Invalid IDs and windows are ignored.
int chooseNext(std::vector<Window> const& windows, std::vector<int> const& selectedIds,
               Threshold threshold, bool longerEntries);
std::string nextTask(Window const& window, Threshold threshold,
                     AnalysisOptions options);

} // namespace context
