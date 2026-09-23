#include "../src/Analysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace context;

namespace {
int checks = 0;

void check(bool value, char const* description) {
    ++checks;
    if (!value) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

bool near(double a, double b) { return std::abs(a - b) < 1e-8; }

Survey smoothSurvey() {
    SurveyRecorder recorder;
    recorder.begin(0, 0);
    for (int p = 1; p <= 100; ++p) recorder.sample(p, .5, 0, p % 5 == 0 ? 1 : 0);
    recorder.complete();
    return recorder.data();
}

Window window(int id, double start, double end) {
    Window result;
    result.id = id;
    result.start = start;
    result.end = end;
    return result;
}

void add(Window& target, Outcome outcome, RunContext runContext = RunContext::Fresh,
         bool practice = true, Mode mode = Mode::Verify, double seconds = 5) {
    Attempt attempt;
    attempt.outcome = outcome;
    attempt.context = runContext;
    attempt.practice = practice;
    attempt.mode = mode;
    attempt.seconds = seconds;
    target.history.push_back(attempt);
}

void recorderOriginsAndIntervals() {
    SurveyRecorder recorder;
    recorder.sample(80, 1, 0, 1);
    check(coverage(recorder.data()) == 0, "samples without a run are ignored");
    recorder.begin(40.5, 0);
    check(coverage(recorder.data()) == 0, "begin alone does not invent observed dwell");
    recorder.sample(20, 1, 0, 1);
    check(coverage(recorder.data()) == 0, "samples before the actual start are ignored");
    recorder.sample(42.5, 2, 0, 3);
    check(coverage(recorder.data()) == 3, "small forward interval covers only traversed bins");
    check(!recorder.data().bins[39].covered && !recorder.data().bins[43].covered,
          "traversal cannot leak outside sampled interval");
    check(near(recorder.data().bins[40].seconds, .5) && near(recorder.data().bins[41].seconds, 1) &&
          near(recorder.data().bins[42].seconds, .5), "dwell is distributed across actual traversal");
    check(near(recorder.data().totalSeconds, 2) && recorder.data().bins[42].clicks == 3,
          "elapsed time and observed clicks are accumulated");
    recorder.sample(42.7, .2, 1, 1);
    check(recorder.data().bins[42].visits == 1 && recorder.data().bins[42].transitions == 1,
          "same-run bin visits do not inflate and a form change is observed");
    recorder.sample(80, .2, 2, 1);
    check(recorder.data().bins[80].covered && !recorder.data().bins[60].covered,
          "a large jump does not claim skipped spans");
    check(recorder.data().bins[80].transitions == 0,
          "a skipped span cannot locate an unobserved form transition");
    recorder.sample(75, .3, 2, 0);
    check(recorder.data().bins[75].covered && !recorder.data().bins[77].covered,
          "reverse movement observes its destination without inventing a forward path");
    recorder.cancelRun();
    recorder.sample(76, .3, 2, 10);
    check(!recorder.data().bins[76].covered, "cancelled runs stop collecting");
}

void deathsCheckpointsAndCompletion() {
    SurveyRecorder recorder;
    recorder.begin(59, 0);
    recorder.sample(60, .5, 0, 1);
    recorder.checkpoint(60);
    recorder.sample(61, .5, 0, 0);
    check(recorder.data().bins[60].checkpoints == 1 && recorder.data().bins[61].covered,
          "checkpoint creation records evidence without cancelling the run");
    recorder.died(61.2);
    recorder.died(61.2);
    recorder.sample(62, .5, 0, 1);
    check(recorder.data().bins[61].deaths == 1 && !recorder.data().bins[62].covered,
          "a run can die once and cannot keep collecting after death");
    recorder.begin(61, 0);
    recorder.died(61.2);
    check(recorder.data().bins[61].deaths == 2 && recorder.data().bins[61].visits == 2,
          "a new respawn is a separate run and visit");
    recorder.complete();
    check(!recorder.data().completed, "completion after death is not accepted");
    recorder.begin(98, 0);
    recorder.sample(99, .5, 0, 0);
    recorder.complete();
    recorder.complete();
    check(recorder.data().completed && recorder.data().bins[99].covered,
          "actual completion records the final bin once");
    check(!recorder.data().bins[90].covered, "completion does not fill an unobserved prefix");
    recorder.reset();
    check(recorder.data().runs == 0 && !recorder.data().completed && coverage(recorder.data()) == 0,
          "reset clears survey and pending run state");
}

void invalidSamples() {
    double const nan = std::numeric_limits<double>::quiet_NaN();
    double const infinity = std::numeric_limits<double>::infinity();
    SurveyRecorder recorder;
    recorder.begin(nan, 0);
    recorder.begin(-1, 0);
    recorder.begin(101, 0);
    check(recorder.data().runs == 0, "invalid origins do not become survey runs");
    recorder.begin(0, 0);
    recorder.sample(nan, 1, 1, 1);
    recorder.sample(1, infinity, 1, 1);
    recorder.sample(1, -1, 1, 1);
    recorder.sample(1, 0, 1, 1);
    recorder.died(infinity);
    check(coverage(recorder.data()) == 0 && recorder.data().totalSeconds == 0,
          "nonfinite or nonpositive samples do not poison survey data");
    recorder.sample(1, 1, -1, -10);
    check(recorder.data().bins[1].clicks == 0 && recorder.data().bins[1].transitions == 0,
          "negative clicks and unknown forms do not invent events");
    check(std::all_of(recorder.data().bins.begin(), recorder.data().bins.end(), [](auto const& bin) {
        return std::isfinite(bin.seconds) && bin.seconds >= 0;
    }), "all bin dwell values remain finite and nonnegative");
}

void eventPlansAndOptions() {
    auto survey = smoothSurvey();
    check(coverage(survey) == 100 && survey.completed, "continuous first traversal covers the full level");
    auto smooth = analyze(survey, {});
    check(!smooth.empty() && smooth.size() <= 4, "smooth gameplay gets a small exploration fallback");
    for (auto const& item : smooth) {
        check(item.window.tags == 0, "a smooth scan cannot infer memory, precision, pressure, or fatigue");
        check(item.window.history.empty(), "survey outcomes are not verification history");
    }
    survey.bins[64].deaths = 3;
    survey.bins[30].transitions = 1;
    for (int i = 44; i <= 49; ++i) {
        survey.bins[i].seconds = .6;
        survey.bins[i].clicks = 5;
    }
    auto suggestions = analyze(survey, {});
    check(!suggestions.empty() && suggestions.size() <= 8, "event-based analysis is bounded");
    check(std::any_of(suggestions.begin(), suggestions.end(), [](auto const& item) {
        return item.window.start <= 64 && item.window.end > 64 && item.deaths >= 3;
    }), "repeated deaths receive a surrounding task");
    check(std::any_of(suggestions.begin(), suggestions.end(), [](auto const& item) {
        return (item.window.tags & (1u << 3)) != 0;
    }), "an observed form change receives a transition task");
    check(std::any_of(suggestions.begin(), suggestions.end(), [](auto const& item) {
        return (item.window.tags & (1u << 2)) != 0;
    }), "sustained measured input load receives an input task");
    auto longInterval = analyze(survey, {}, {0, 100});
    check(longInterval.size() == 1 && (longInterval[0].window.tags & (1u << 2)),
          "a local input-heavy part is not hidden by averaging a long selected interval");
    for (auto const& item : suggestions) {
        check(validWindow(item.window) && std::isfinite(item.priority) && item.window.id == 0,
              "all proposed windows have finite usable bounds and await Store IDs");
        check((item.window.tags & (1u | (1u << 1) | (1u << 4) | (1u << 5))) == 0,
              "observed events do not falsely diagnose memory, precision, endurance, or pressure");
    }
    AnalysisOptions off;
    off.deaths = off.inputs = off.transitions = off.memory = false;
    check(analyze(survey, off).empty(), "disabled analysis sources do not generate inferred event windows");
    off.memory = true;
    auto memory = analyze(survey, off);
    check(!memory.empty() && std::all_of(memory.begin(), memory.end(), [](auto const& item) {
        return item.window.tags == 1;
    }), "memory labels are applied only by explicit selection");
    Survey empty;
    check(analyze(empty, {}).empty(), "an empty survey cannot generate a speculative plan");
    empty.bins[60].covered = true;
    empty.bins[60].deaths = 2;
    check(analyze(empty, {}).empty(), "an isolated point without surrounding timing cannot justify a window");
    auto repeatedBursts = smoothSurvey();
    for (int i = 45; i < 48; ++i) {
        repeatedBursts.bins[i].seconds = 6;
        repeatedBursts.bins[i].clicks = 60;
        repeatedBursts.bins[i].visits = 20;
    }
    auto bursts = analyze(repeatedBursts, {}, {0, 100});
    check(bursts.size() == 1 && !(bursts[0].window.tags & (1u << 2)),
          "many repeated short bursts are not mislabeled as one sustained input sequence");
}

void selectedStartPosIntervals() {
    auto survey = smoothSurvey();
    survey.bins[65].deaths = 4;
    auto plan = analyze(survey, {}, {0, 40, 60});
    check(plan.size() == 3, "every selected StartPos produces an interval through the final 100");
    check(plan.front().window.start == 60 && plan.front().window.end == 100,
          "measured repeated failures can prioritize an exact selected interval");
    for (auto const& item : plan) {
        check((item.window.start == 0 && item.window.end == 40) ||
              (item.window.start == 40 && item.window.end == 60) ||
              (item.window.start == 60 && item.window.end == 100),
              "analysis never invents an unsupported StartPos boundary");
        check(item.window.history.empty(), "passing a survey interval once never means repeatable readiness");
    }
    auto sanitized = analyze(survey, {}, {60, 0, 40, 40, 100, -5, 101, std::numeric_limits<double>::quiet_NaN()});
    check(sanitized.size() == plan.size(), "unordered duplicate and invalid boundaries are sanitized");
    check(analyze(survey, {}, {100}).empty(), "100 is an endpoint rather than a zero-length task");
    check(analyze(survey, {}, {-1}).empty(), "invalid selected starts cannot trigger an unrelated fallback");
    AnalysisOptions off;
    off.deaths = off.inputs = off.transitions = off.memory = false;
    auto explicitPlan = analyze(survey, off, {0, 40, 60});
    check(explicitPlan.size() == 3, "selected structure survives disabling automatic evidence labels");
    check(std::all_of(explicitPlan.begin(), explicitPlan.end(), [](auto const& item) {
        return item.window.tags == 0 && item.reason.find("death") == std::string::npos;
    }), "disabled measurements do not drive explanatory claims");
    std::vector<double> manyStarts;
    for (int i = 0; i < 64; ++i) manyStarts.push_back(i * 1.5);
    check(analyze(survey, {}, manyStarts).size() == 64, "boundary mode never silently drops selected intervals above eight");

    SurveyRecorder staged;
    for (double start : {40.0, 60.0}) {
        staged.begin(start, 0);
        int const end = start == 40 ? 60 : 100;
        for (int p = static_cast<int>(start) + 1; p <= end; ++p) staged.sample(p, .2, 0, 1);
        if (end == 100) staged.complete(); else staged.cancelRun();
    }
    auto stages = analyze(staged.data(), {}, {40, 60});
    check(stages.size() == 2 && !staged.data().bins[39].covered,
          "staged native starts do not require or claim an unselected prefix");
    auto absent = analyze(staged.data(), {}, {0, 20});
    check(absent.size() == 2 && std::any_of(absent.begin(), absent.end(), [](auto const& item) {
        return item.window.start == 0 && item.priority == 0 && item.reason.find("insufficient") != std::string::npos;
    }), "an unobserved selected interval remains mandatory without claiming measurement evidence");
    auto untimed = analyze(Survey{}, {}, {0, 40, 60});
    check(untimed.size() == 3 && std::all_of(untimed.begin(), untimed.end(), [](auto const& item) {
        return item.priority == 0 && item.observedSeconds == 0 && item.window.history.empty();
    }), "zero telemetry cannot silently remove any explicitly selected interval or invent readiness");
}

void stageCalibration() {
    std::vector<double> starts{0, 40, 60};
    std::vector<bool> calibrated{true, true, false};
    std::vector<bool> passed{true, true, false};
    std::vector<double> ends{40, 60, 0};
    check(calibrateStage(starts, calibrated, passed, ends, 2, 62) == 1,
          "a newly measured later start revisits the predecessor if its passed exit was too early");
    check(starts[2] == 62 && calibrated[2] && passed[0] && !passed[1] && !passed[2],
          "expanded calibration keeps unrelated passes and never grants a pass to the new stage");
    check(calibrateStage(starts, calibrated, passed, ends, 2, 62) == -1,
          "recalibration does not repeatedly invalidate an already pending predecessor");
    ends[1] = 62;
    passed[1] = true;
    check(calibrateStage(starts, calibrated, passed, ends, 2, 62) == -1 && passed[1] && !passed[2],
          "replayed predecessor reaching the calibrated end remains passed while the final stage is still mandatory");

    starts = {0, 40, 60};
    calibrated = {true, true, false};
    passed = {true, true, false};
    ends = {40, 60, 0};
    check(calibrateStage(starts, calibrated, passed, ends, 2, 58) == -1 && passed[1],
          "a shorter calibrated interval does not erase a sufficient predecessor pass");
    auto const unchangedStarts = starts;
    auto const unchangedCalibrated = calibrated;
    auto const unchangedPassed = passed;
    check(calibrateStage(starts, calibrated, passed, ends, 1, 59) == -2 &&
          starts == unchangedStarts && calibrated == unchangedCalibrated && passed == unchangedPassed,
          "crossing a calibrated neighbor is rejected without partial mutation");
    check(calibrateStage(starts, calibrated, passed, ends, 1, 0) == -2,
          "duplicate calibrated starts cannot form a false zero-length stage");
    check(calibrateStage(starts, calibrated, passed, ends, 1, std::numeric_limits<double>::quiet_NaN()) == -2 &&
          calibrateStage(starts, calibrated, passed, ends, 1, 100) == -2,
          "nonfinite and completion-position origins are rejected");

    starts = {0, 40, 45};
    calibrated = {true, false, false};
    passed = {false, false, false};
    ends = {0, 0, 0};
    check(calibrateStage(starts, calibrated, passed, ends, 1, 52.125) == -1 && starts[1] == 52.125,
          "an unknown next estimate cannot reject a valid measured double-precision origin");
    check(calibrateStage(starts, calibrated, passed, ends, 2, 66.375) == -1,
          "later calibration restores actual order without relying on stale estimates");
    starts = {0, 60};
    calibrated = {true, false};
    passed = {true, false};
    ends = {60, 0};
    check(calibrateStage(starts, calibrated, passed, ends, 1, 60.009) == -1 && passed[0],
          "sub-tolerance measurement drift does not repeat a sufficient pass");
    check(calibrateStage(starts, calibrated, passed, ends, 1, 60.011) == 0 && !passed[0],
          "expanded exit beyond tolerance reopens the prior stage");
    auto const beforeSizeError = starts;
    ends.pop_back();
    check(calibrateStage(starts, calibrated, passed, ends, 1, 62) == -2 && starts == beforeSizeError,
          "inconsistent persisted vector sizes cannot mutate calibration state");
}

void nextChoiceAndSeparation() {
    auto early = window(1, 10, 20);
    auto middle = window(2, 40, 50);
    auto late = window(3, 80, 90);
    check(chooseNext({early, middle}, {}, {}, true) == 0, "an empty selection selects no task");
    check(chooseNext({early, middle}, {9}, {}, true) == 0, "unknown selection IDs are ignored");
    check(chooseNext({middle, early}, {1, 2}, {}, true) == 1, "unexplored ties prefer earlier cheaper access deterministically");
    check(chooseNext({early, middle}, {2}, {}, true) == 2, "only selected tasks are eligible");
    for (int i = 0; i < 3; ++i) add(middle, Outcome::Failure);
    check(chooseNext({early, middle}, {1, 2}, {}, true) == 2,
          "repeated measured failures justify focused attention");
    for (int i = 0; i < 30; ++i) add(middle, Outcome::Failure);
    check(chooseNext({early, middle}, {1, 2}, {}, true) == 1,
          "persistent difficulty cannot starve unexplored tasks indefinitely");
    for (int i = 0; i < 6; ++i) add(late, Outcome::Success);
    check(chooseNext({early, late}, {1, 3}, {}, true) == 1,
          "an already repeatable interval yields time to an unexplored one");
    auto studying = window(4, 40, 50);
    for (int i = 0; i < 6; ++i) add(studying, Outcome::Success, RunContext::Fresh, true, Mode::Study);
    check(nextTask(studying, {}, {}).find("scan is not readiness") != std::string::npos,
          "study successes do not silently unlock a longer entry");
    auto separate = window(5, 50, 60);
    for (int i = 0; i < 3; ++i) {
        add(separate, Outcome::Success, RunContext::Fresh, true);
        add(separate, Outcome::Success, RunContext::Fresh, false);
    }
    check(nextTask(separate, {}, {}).find("Short starts repeat") != std::string::npos,
          "complete practice and normal samples combine into a six-attempt baseline");
    auto mixed = window(7, 10, 20);
    for (int i = 0; i < 3; ++i) add(mixed, Outcome::Success);
    add(mixed, Outcome::Success, RunContext::Fresh, false);
    for (int i = 0; i < 2; ++i) add(mixed, Outcome::Failure, RunContext::Fresh, false);
    auto unexplored = window(8, 80, 90);
    check(chooseNext({mixed, unexplored}, {7, 8}, {}, false) == 8,
          "combined four-of-six baseline yields to an unexplored run despite two latest Normal failures");
    for (auto& attempt : mixed.history) attempt.practice = true;
    check(chooseNext({mixed, unexplored}, {7, 8}, {}, false) == 8,
          "game-mode labels alone cannot change the training priority");
    auto contextsSeparate = window(6, 60, 70);
    for (int i = 0; i < 3; ++i) {
        add(contextsSeparate, Outcome::Success, RunContext::Fresh);
        add(contextsSeparate, Outcome::Success, RunContext::LeadIn);
    }
    check(nextTask(contextsSeparate, {}, {}).find("This entry repeats") == std::string::npos,
          "short starts and long entries cannot combine into readiness");
    check(nextTask(late, {}, {}).find("previous selected StartPos") != std::string::npos,
          "fresh repeatability asks for a real earlier selected start");
    AnalysisOptions simple;
    simple.longerEntries = false;
    check(nextTask(late, {}, simple).find("previous selected StartPos") == std::string::npos,
          "longer-entry requests respect the selected option");
    late.tags = 1u << 2;
    check(nextTask(late, {}, simple).find("rest") != std::string::npos,
          "observed input tasks can receive a selected rest reminder");
    simple.restReminders = false;
    check(nextTask(late, {}, simple).find("rest") == std::string::npos,
          "rest reminders can be disabled without altering readiness");

    auto observedEasy = window(10, 10, 20);
    auto observedDifficult = window(11, 60, 70);
    add(observedEasy, Outcome::Success);
    add(observedDifficult, Outcome::Success);
    observedDifficult.diagnosticWeight = 100;
    check(chooseNext({observedEasy, observedDifficult}, {10, 11}, {}, true) == 11,
          "first-stage measured priority still affects selection after the first training result");
    auto zero = window(12, 0, 10);
    auto another = window(13, 40, 50);
    for (int i = 0; i < 30; ++i) {
        add(zero, Outcome::Success, RunContext::FromZero);
        add(another, Outcome::Success);
    }
    check(chooseNext({zero, another}, {12, 13}, {}, true) == 13,
          "a repeatable zero-origin task cannot demand an impossible earlier transfer forever");
    zero.diagnosticWeight = 100;
    for (auto& attempt : zero.history) attempt.timestamp = 900;
    for (auto& attempt : another.history) attempt.timestamp = 600;
    check(chooseNext({zero, another}, {12, 13}, {}, false) == 13,
          "aging gives an older task another turn after exposure penalties have saturated");
    another.diagnosticWeight = std::numeric_limits<double>::quiet_NaN();
    check(chooseNext({zero, another}, {12, 13}, {}, false) == 13,
          "a malformed diagnostic weight cannot break deterministic selection");
}

void continuedAttemptsInformSelection() {
    auto easy = window(1, 10, 11.5);
    auto hard = window(2, 11.5, 20);
    hard.leadIn = 1.5;
    easy.diagnosticWeight = 100;
    check(chooseNext({easy, hard}, {1, 2}, {}, true) == 1,
          "a noisy first pass initially prioritizes the short easy target");
    for (int i = 0; i < 6; ++i) {
        RunTracker target, continued;
        target.begin(easy, 10, Mode::Verify, true, false, 100 + i);
        continued.begin(hard, 10, Mode::Verify, true, false, 100 + i);
        auto passed = target.sample(11.5, 1.5, 2, 2);
        auto died = continued.finish(Outcome::Failure, 15, 5, 8, 3);
        check(passed && died, "one physical attempt supplies both a target pass and a later failure");
        easy.history.push_back(*passed);
        hard.history.push_back(*died);
    }
    check(summarize(easy, RunContext::Fresh, true, Mode::Verify, {}).repeatable,
          "current repeated short passes establish reliability despite the old diagnostic weight");
    auto longer = summarize(hard, RunContext::LeadIn, true, Mode::Verify, {});
    check(longer.attempts == 6 && longer.successes == 0,
          "failures beyond the target remain useful evidence of the earlier entry");
    check(summarize(hard, RunContext::Fresh, true, Mode::Verify, {}).attempts == 0,
          "continued attempts are not mislabeled as fresh starts");
    check(chooseNext({easy, hard}, {1, 2}, {}, true) == 2,
          "new failures beyond the target outweigh old diagnostic difficulty on a now reliable part");
    check(chooseNext({easy, hard}, {1}, {}, true) == 1,
          "passively recorded unselected runs do not become automatic tasks");
}
} // namespace

int main() {
    recorderOriginsAndIntervals();
    deathsCheckpointsAndCompletion();
    invalidSamples();
    eventPlansAndOptions();
    selectedStartPosIntervals();
    stageCalibration();
    nextChoiceAndSeparation();
    continuedAttemptsInformSelection();
    std::cout << "Passed " << checks << " automatic analysis checks.\n";
}
