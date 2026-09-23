#include "../src/Training.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace context;

namespace {
int checks = 0;

void check(bool condition, char const* description) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

Window makeWindow(double start = 59, double end = 69) {
    Window window;
    window.start = start;
    window.end = end;
    return window;
}

void add(Window& window, Outcome outcome, RunContext runContext = RunContext::Fresh,
         Mode mode = Mode::Verify, bool practice = false, bool returnCheck = false) {
    Attempt attempt;
    attempt.outcome = outcome;
    attempt.context = runContext;
    attempt.mode = mode;
    attempt.practice = practice;
    attempt.returnCheck = returnCheck;
    window.history.push_back(attempt);
}

void repeatableBaseline(Window& window, bool practice = false) {
    for (int i = 0; i < 6; ++i) {
        add(window, i < 2 ? Outcome::Failure : Outcome::Success,
            RunContext::Fresh, Mode::Verify, practice);
    }
}

void boundariesAndContexts() {
    Window window = makeWindow();
    check(validWindow(window), "ordinary window is valid");
    check(classify(59, window) == RunContext::Fresh, "window start is fresh");
    check(classify(54, window) == RunContext::LeadIn, "exact lead-in boundary is long entry");
    check(classify(54.1, window) == RunContext::Fresh, "shorter entry remains fresh");
    check(classify(0, window) == RunContext::FromZero, "zero remains separate from long entry");
    window.end = window.start;
    check(!validWindow(window), "zero-length window is invalid");
    window = makeWindow(-1, 20);
    check(!validWindow(window), "negative boundary is invalid");
    window = makeWindow(1, 101);
    check(!validWindow(window), "boundary beyond completion is invalid");
    window = makeWindow();
    window.leadIn = 0;
    check(!validWindow(window), "long-entry distance must be positive");
    window = makeWindow();
    window.start = std::numeric_limits<double>::quiet_NaN();
    check(!validWindow(window), "NaN boundary is invalid");
    auto threshold = clampThreshold({99, 0});
    check(threshold.total == 1 && threshold.required == 1, "threshold respects total lower bound");
    threshold = clampThreshold({-10, 1000});
    check(threshold.total == 100 && threshold.required == 1, "threshold has safe positive bounds");
}

void trackerLifecycle() {
    Window window = makeWindow();
    RunTracker tracker;
    check(!tracker.eligible() && !tracker.entered(), "tracker starts inactive");
    tracker.begin(window, 59, Mode::Verify, false, false, 1234);
    check(tracker.eligible() && tracker.entered(), "start at window boundary enters immediately");
    check(!tracker.sample(68.999, 1, 2, 3), "no rounded early success at exit");
    auto result = tracker.sample(69, 2, 4, 5);
    check(result && result->outcome == Outcome::Success, "crossing exit succeeds");
    check(result->timestamp == 1234 && result->clicks == 4 && result->seconds == 2,
          "success records attempt metadata");
    check(!tracker.sample(80, 3, 5, 6), "later frames do not duplicate success");
    check(!tracker.finish(Outcome::Failure, 80, 3, 5, 6), "later death cannot overwrite success");

    tracker.begin(window, 59, Mode::Verify, false, false, 0);
    result = tracker.finish(Outcome::Failure, 64, 1, 3, 3);
    check(result && result->outcome == Outcome::Failure, "death inside window fails");
    check(!tracker.finish(Outcome::Failure, 64, 1, 3, 3), "death and restart callbacks count once");
    tracker.begin(window, 59, Mode::Verify, false, false, 0);
    tracker.sample(68.99, 1, 0, 0);
    result = tracker.finish(Outcome::Failure, 69.02, 2, 0, 0);
    check(result && result->outcome == Outcome::Success,
          "crossing the exit on a death frame confirms the already-passed window");
    check(!tracker.finish(Outcome::Failure, 69.02, 2, 0, 0),
          "death-frame success is recorded only once");
    tracker.begin(window, 59, Mode::Verify, false, false, 0);
    result = tracker.finish(Outcome::Abandoned, 62, 1, 0, 0);
    check(result && result->outcome == Outcome::Abandoned, "quit is recorded separately");
    tracker.begin(window, 59, Mode::Verify, false, false, 0);
    result = tracker.finish(Outcome::Abandoned, 69.02, 2, 0, 0);
    check(result && result->outcome == Outcome::Abandoned,
          "invalidation cannot manufacture success beyond the exit");

    tracker.begin(window, 0, Mode::Verify, false, false, 0);
    check(tracker.eligible() && !tracker.entered(), "early run waits for entry");
    check(!tracker.finish(Outcome::Failure, 40, 1, 0, 0), "death before entry is not an attempt");
    check(!tracker.sample(69, 2, 0, 0), "finished pre-entry run cannot resume");
    tracker.begin(window, 0, Mode::Verify, false, false, 0);
    result = tracker.finish(Outcome::Failure, 64, 1, 0, 0);
    check(result && result->context == RunContext::FromZero, "death event can detect crossed entry");
    tracker.begin(window, 40, Mode::Verify, false, false, 0);
    result = tracker.sample(70, 1, 0, 0);
    check(result && result->context == RunContext::LeadIn, "one frame can cross both boundaries");
}

void checkpointsAndCompletion() {
    Window window = makeWindow();
    RunTracker tracker;
    tracker.begin(window, 63, Mode::Verify, true, false, 0);
    check(!tracker.eligible() && !tracker.sample(69, 1, 0, 0),
          "startpos inside window cannot confirm the missing entry");
    tracker.begin(window, 59, Mode::Verify, true, false, 0);
    check(!tracker.sample(62, 1, 0, 0), "checkpoint creation does not end an attempt");
    auto result = tracker.sample(69, 2, 0, 0);
    check(result && result->practice, "uninterrupted practice attempt can succeed");
    tracker.begin(window, 62, Mode::Verify, true, false, 0);
    check(!tracker.eligible() && !tracker.sample(69, 1, 0, 0),
          "checkpoint respawn inside window cannot confirm missing entry");
    tracker.begin(window, 59.005, Mode::Verify, false, false, 0);
    check(tracker.eligible(), "small starting-position drift is tolerated");
    tracker.begin(window, 59.02, Mode::Verify, false, false, 0);
    check(!tracker.eligible(), "starting tolerance is tight");

    window = makeWindow(91, 100);
    tracker.begin(window, 91, Mode::Verify, false, false, 0);
    check(!tracker.sample(99.99, 1, 0, 0), "near 100 is not completion");
    check(!tracker.sample(100, 1, 0, 0), "displayed 100 alone is not completion");
    result = tracker.sample(99.9, 2, 0, 0, true);
    check(result && result->outcome == Outcome::Success && result->endPercent == 100,
          "level completion confirms a 100-percent exit");
    check(!tracker.sample(100, 3, 0, 0, true), "duplicate completion callbacks count once");
    tracker.begin(window, 91, Mode::Verify, false, false, 0);
    result = tracker.finish(Outcome::Failure, 100, 2, 0, 0);
    check(result && result->outcome == Outcome::Failure,
          "death at displayed 100 does not replace actual level completion");
}

void automaticStartPosCompletion() {
    check(validAutoRunMode(true, true, false, false),
          "an automatic attempt remains valid in ordinary practice mode");
    check(validAutoRunMode(true, false, true, false),
          "the same managed StartPos test mode keeps a live attempt active after the practice flag clears");
    check(validAutoRunMode(true, false, true, true),
          "the observed native finish in managed test mode must not become abandoned");
    check(validAutoRunMode(true, false, false, true),
          "a native completion event preserves a run that began in practice");
    check(!validAutoRunMode(true, false, false, false),
          "switching to unconfirmed normal gameplay invalidates an automatic attempt");
    check(!validAutoRunMode(false, true, true, true),
          "later practice, test mode and completion cannot enroll a run that did not begin in practice");
    check(!validAutoRunMode(false, false, true, false),
          "an unrelated editor test run is not an automatic practice attempt");

    Window window = makeWindow(91, 100);
    RunTracker tracker;
    bool const beganInPractice = true;
    tracker.begin(window, 91, Mode::Verify, beganInPractice, false, 4567);
    if (validAutoRunMode(beganInPractice, false, true, false)) {
        check(!tracker.sample(99.99, 12.0, 7, 3),
              "managed test mode still requires actual completion at a 100-percent boundary");
        check(!tracker.sample(100, 12.1, 7, 3),
              "numeric 100 in managed test mode is not a native completion event");
    } else {
        tracker.finish(Outcome::Abandoned, 99.99, 12.0, 7, 3);
    }

    std::optional<Attempt> completion;
    if (validAutoRunMode(beganInPractice, false, true, true))
        completion = tracker.sample(100, 12.204167, 7, 3, true);
    check(completion && completion->outcome == Outcome::Success && completion->practice,
          "the native finish succeeds and retains the practice classification captured at run start");
    window.history.push_back(*completion);
    check(!tracker.sample(100, 12.204167, 7, 3, true) &&
          !tracker.finish(Outcome::Failure, 100, 12.204167, 7, 3),
          "completion-screen and restart callbacks cannot duplicate or overwrite the recorded native finish");
    check(summarize(window, RunContext::Fresh, true, Mode::Verify, {}).successes == 1 &&
          summarize(window, RunContext::Fresh, false, Mode::Verify, {}).attempts == 0,
          "a cleared native practice flag does not leak automatic StartPos results into normal records");
}

void continuousRunAcrossWindows() {
    auto target = makeWindow(10, 11.5);
    auto later = makeWindow(11.5, 20);
    later.leadIn = 1.5;
    auto dying = makeWindow(20, 30);
    auto unreached = makeWindow(30, 40);
    RunTracker targetTracker, laterTracker, dyingTracker, unreachedTracker;
    targetTracker.begin(target, 10, Mode::Verify, true, false, 100);
    laterTracker.begin(later, 10, Mode::Verify, true, false, 100);
    dyingTracker.begin(dying, 10, Mode::Verify, true, false, 100);
    unreachedTracker.begin(unreached, 10, Mode::Verify, true, false, 100);

    auto targetPass = targetTracker.sample(11.5, 1.5, 2, 2);
    check(targetPass && targetPass->outcome == Outcome::Success && targetPass->seconds == 1.5,
          "the short target is credited at its exit without finishing the other trackers");
    check(!laterTracker.sample(11.5, 1.5, 2, 2) && laterTracker.entered(),
          "crossing the target exit enters the next run in the same attempt");
    auto laterPass = laterTracker.sample(20, 10, 12, 4);
    check(laterPass && laterPass->outcome == Outcome::Success,
          "continuing beyond the target records the next run as passed");
    check(laterPass->context == RunContext::LeadIn && laterPass->startPercent == 10 && laterPass->practice,
          "a later pass retains the real earlier entry and practice mode");
    check(laterPass->seconds == 10 && laterPass->clicks == 12 && laterPass->peakCps == 4,
          "telemetry keeps advancing after the short target has passed");
    check(!dyingTracker.sample(23, 13, 15, 4), "the next entered run remains live before its exit");
    auto death = dyingTracker.finish(Outcome::Failure, 25, 15, 17, 5);
    check(death && death->outcome == Outcome::Failure && death->endPercent == 25,
          "a later death belongs to the run actually reached");
    check(!unreachedTracker.finish(Outcome::Failure, 25, 15, 17, 5),
          "a death does not create failures for unreached runs");
    check(!targetTracker.sample(25, 15, 17, 5) &&
          !targetTracker.finish(Outcome::Failure, 25, 15, 17, 5) &&
          !laterTracker.finish(Outcome::Failure, 25, 15, 17, 5),
          "later samples and death cannot duplicate or overwrite earlier passes");
    check(!dyingTracker.finish(Outcome::Failure, 25, 15, 17, 5),
          "death followed by reset cannot record a second failure");

    auto ending = makeWindow(90, 100);
    RunTracker finishTracker;
    finishTracker.begin(ending, 10, Mode::Verify, true, false, 100);
    check(!finishTracker.sample(100, 70, 100, 6),
          "a continued run at displayed 100 still waits for native completion");
    auto finish = finishTracker.sample(100, 71, 102, 6, true);
    check(finish && finish->outcome == Outcome::Success && finish->context == RunContext::LeadIn,
          "native completion credits a later final run from the original entry");
    check(!finishTracker.sample(100, 71, 102, 6, true) &&
          !targetTracker.sample(100, 71, 102, 6, true),
          "completion callbacks do not duplicate final or earlier passes");

    RunTracker abandoned;
    abandoned.begin(later, 10, Mode::Verify, true, false, 100);
    auto quit = abandoned.finish(Outcome::Abandoned, 15, 5, 5, 3);
    check(quit && quit->outcome == Outcome::Abandoned,
          "quitting a continued run leaves the incomplete later window unfinished");
    later.history.push_back(*quit);
    check(summarize(later, RunContext::LeadIn, true, Mode::Verify, {}).attempts == 0,
          "unfinished continued runs do not dilute consistency");
}

void originalNormalRunsShareWindowHistory() {
    check(validOriginalRunMode(false, false, false, false), "a clean normal run can record original-level results");
    check(!validOriginalRunMode(true, false, false, false), "practice in the original cannot count as normal from zero");
    check(!validOriginalRunMode(false, true, false, false), "test mode cannot supply original-level evidence");
    check(!validOriginalRunMode(false, false, true, false), "a native StartPos cannot masquerade as an original zero start");
    check(!validOriginalRunMode(false, false, false, true), "ignore-damage runs cannot supply original-level evidence");

    auto trained = makeWindow(40, 50);
    repeatableBaseline(trained, true);
    RunTracker original;
    original.begin(trained, 0, Mode::Verify, false, false, 100);
    check(!original.sample(30, 12, 7, 3), "the original must reach a trained part before recording it");
    auto pass = original.sample(50, 20, 12, 4);
    check(pass && pass->context == RunContext::FromZero && !pass->practice && pass->startPercent == 0,
          "original results retain normal mode and the real zero-origin context");
    trained.history.push_back(*pass);
    check(summarize(trained, RunContext::Fresh, true, Mode::Verify, {}).attempts == 6,
          "shared original evidence leaves the six practice baseline attempts unchanged");
    auto normal = summarize(trained, RunContext::FromZero, false, Mode::Verify, {});
    check(normal.attempts == 1 && normal.successes == 1,
          "the same window history exposes the original's normal from-zero pass");
    check(summarize(trained, RunContext::FromZero, true, Mode::Verify, {}).attempts == 0,
          "normal passes cannot enter the practice denominator");
    check(!original.finish(Outcome::Failure, 80, 30, 20, 4),
          "a later death in the original cannot overwrite a trained-part pass");

    auto finishWindow = makeWindow(90, 100);
    original.begin(finishWindow, 0, Mode::Verify, false, false, 101);
    check(!original.sample(100, 50, 30, 5), "original displayed 100 still requires native completion");
    auto finish = original.sample(100, 51, 31, 5, true);
    check(finish && !finish->practice && finish->context == RunContext::FromZero && finish->outcome == Outcome::Success,
          "a native original finish records a normal success from zero");
    check(!original.sample(100, 51, 31, 5, true), "the original completion screen cannot duplicate its final pass");
    original.begin(trained, 0, Mode::Verify, false, false, 102);
    auto changedMode = original.finish(Outcome::Abandoned, 45, 19, 10, 4);
    check(changedMode && changedMode->outcome == Outcome::Abandoned,
          "changing mode mid-original-attempt leaves the unfinished part unverified");
}

void historySeparationAndRollingWindow() {
    Window window = makeWindow();
    for (int i = 0; i < 6; ++i) add(window, Outcome::Success, RunContext::Fresh, Mode::Study);
    auto summary = summarize(window, RunContext::Fresh, false, Mode::Study, {});
    check(summary.successes == 6 && !summary.repeatable && !summary.transferred,
          "study remains visible without asserting readiness");
    summary = summarize(window, RunContext::Fresh, false, Mode::Verify, {});
    check(summary.attempts == 0, "study cannot enter verification denominator");
    repeatableBaseline(window, true);
    summary = summarize(window, RunContext::Fresh, false, Mode::Verify, {});
    check(summary.attempts == 0, "practice cannot enter normal denominator");
    summary = summarize(window, RunContext::Fresh, true, Mode::Verify, {});
    check(summary.attempts == 6 && summary.successes == 4 && summary.repeatable,
          "four of six is repeatable within its own slice");
    summary = summarize(window, RunContext::LeadIn, true, Mode::Verify, {});
    check(summary.attempts == 0, "fresh results cannot enter long-entry denominator");
    add(window, Outcome::Abandoned, RunContext::Fresh, Mode::Verify, true);
    summary = summarize(window, RunContext::Fresh, true, Mode::Verify, {});
    check(summary.attempts == 6 && summary.recentAttempts == 6 && summary.repeatable,
          "abandonments neither dilute nor displace rolling results");
    for (int i = 0; i < 3; ++i) add(window, Outcome::Failure, RunContext::Fresh, Mode::Verify, true);
    summary = summarize(window, RunContext::Fresh, true, Mode::Verify, {});
    check(summary.attempts == 9 && summary.recentSuccesses == 3 && !summary.repeatable,
          "recent failures can remove repeatability despite historical successes");
    window = makeWindow();
    for (int i = 0; i < 4; ++i) add(window, Outcome::Success);
    check(!summarize(window, RunContext::Fresh, false, Mode::Verify, {}).repeatable,
          "a full six-attempt sample is required");
}

void transferEvidence() {
    Window window = makeWindow();
    add(window, Outcome::Success, RunContext::LeadIn);
    repeatableBaseline(window);
    check(!summarize(window, RunContext::LeadIn, false, Mode::Verify, {}).transferred,
          "an earlier long run does not retroactively confirm transfer");
    add(window, Outcome::Failure, RunContext::Fresh, Mode::Verify, false, true);
    check(!summarize(window, RunContext::Fresh, false, Mode::Verify, {}).transferred,
          "a failed marked return does not confirm transfer");
    add(window, Outcome::Success, RunContext::LeadIn, Mode::Study);
    check(!summarize(window, RunContext::LeadIn, false, Mode::Verify, {}).transferred,
          "study success cannot confirm transfer");
    add(window, Outcome::Success, RunContext::LeadIn, Mode::Verify, true);
    check(!summarize(window, RunContext::LeadIn, false, Mode::Verify, {}).transferred,
          "practice transfer is separate from normal play");
    add(window, Outcome::Success, RunContext::LeadIn);
    check(summarize(window, RunContext::LeadIn, false, Mode::Verify, {}).transferred,
          "later long-entry success confirms transfer from fresh baseline");
    check(!summarize(window, RunContext::Fresh, false, Mode::Verify, {}).transferred,
          "long-entry evidence is attributed to its own context");
    add(window, Outcome::Success, RunContext::Fresh, Mode::Verify, false, true);
    check(summarize(window, RunContext::Fresh, false, Mode::Verify, {}).transferred,
          "successful marked return confirms fresh-context transfer");
    add(window, Outcome::Success, RunContext::FromZero);
    check(summarize(window, RunContext::FromZero, false, Mode::Verify, {}).transferred,
          "from-zero transfer remains distinguishable from long entry");
    for (int i = 0; i < 4; ++i) add(window, Outcome::Failure);
    check(!summarize(window, RunContext::LeadIn, false, Mode::Verify, {}).transferred,
          "historical transfer does not mask lost current fresh repeatability");
}

void combinedGameModes() {
    Window window = makeWindow();
    for (int i = 0; i < 6; ++i)
        add(window, Outcome::Success, RunContext::Fresh, Mode::Verify, i % 2 == 0);
    auto all = summarize(window, RunContext::Fresh, std::nullopt, Mode::Verify, {});
    check(all.attempts == 6 && all.repeatable, "mixed full runs establish one baseline");
    check(summarize(window, RunContext::Fresh, false, Mode::Verify, {}).attempts == 3 &&
          summarize(window, RunContext::Fresh, true, Mode::Verify, {}).attempts == 3,
          "optional filters retain original mode metadata");
    add(window, Outcome::Success, RunContext::FromZero, Mode::Verify, false);
    check(summarize(window, RunContext::FromZero, std::nullopt, Mode::Verify, {}).transferred,
          "original normal run confirms transfer after a mixed training baseline");
    check(summarize(window, RunContext::LeadIn, std::nullopt, Mode::Verify, {}).attempts == 0,
          "merging modes does not merge entry contexts");
    add(window, Outcome::Failure, RunContext::Fresh, Mode::Study, true);
    add(window, Outcome::Abandoned, RunContext::Fresh, Mode::Verify, false);
    check(summarize(window, RunContext::Fresh, std::nullopt, Mode::Verify, {}).attempts == 6,
          "learning and unfinished records stay out of combined reliability");
    for (int i = 0; i < 4; ++i)
        add(window, Outcome::Failure, RunContext::Fresh, Mode::Verify, i % 2 == 0);
    all = summarize(window, RunContext::Fresh, std::nullopt, Mode::Verify, {});
    check(all.recentAttempts == 6 && all.recentSuccesses == 2 && !all.repeatable,
          "combined rolling history respects chronological failures across both modes");
    RunTracker tracker;
    tracker.begin(window, window.start + 2, Mode::Verify, true, false, 1);
    check(!tracker.sample(window.end, 1, 0, 0) &&
          !tracker.finish(Outcome::Failure, window.end, 1, 0, 0),
          "checkpoint inside a window contributes neither a whole-run pass nor failure");
    tracker.begin(window, window.start, Mode::Verify, true, false, 2);
    auto pass = tracker.sample(window.end, 1, 0, 0);
    check(pass && pass->context == RunContext::Fresh,
          "practice respawn at the boundary still plays the full window");
}

void invalidInputsAndAdvice() {
    Window window = makeWindow();
    RunTracker tracker;
    double const nan = std::numeric_limits<double>::quiet_NaN();
    tracker.begin(window, nan, Mode::Verify, false, false, 0);
    check(!tracker.eligible(), "invalid start cannot create an eligible run");
    tracker.begin(window, 59, Mode::Verify, false, false, 0);
    check(!tracker.sample(nan, 1, 0, 0), "invalid sample does not create a result");
    auto result = tracker.sample(69, nan, -1, -3);
    check(result && result->seconds == 0 && result->clicks == 0 && result->peakCps == 0,
          "invalid telemetry is sanitized");
    tracker.begin(window, 59, Mode::Verify, false, false, 0);
    check(!tracker.finish(Outcome::Success, 60, 1, 0, 0), "caller cannot invent early success");
    check(recommendation(window, {}, 2).find("break") != std::string::npos,
          "fatigue can recommend stopping without a win");
    repeatableBaseline(window);
    check(recommendation(window, {}, 0).find("earlier entry") != std::string::npos,
          "repeatability leads to context testing");
}

void levelIdentity() {
    auto const original = levelFingerprint("kS38,1;1,1,2,10,3,20;1,8,2,50,3,40;");
    check(levelFingerprint("different-header;1,1,2,10,3,20;1,8,2,50,3,40;") == original,
          "mutable header metadata does not change level identity");
    check(levelFingerprint("kS38,1;1,31,2,55,3,20;1,1,2,10,3,20;1,8,2,50,3,40;") == original,
          "adding a start position does not change identity");
    check(levelFingerprint("kS38,1;1,1,2,10,3,20;1,31,2,155,3,90;1,8,2,50,3,40;") == original,
          "moving a start position does not change identity");
    check(levelFingerprint("kS38,1;1,1,2,10,3,20;2,155,3,90,1,31;1,8,2,50,3,40;") == original,
          "start object ID need not be the first property");
    check(levelFingerprint("kS38,1;1,1,2,11,3,20;1,8,2,50,3,40;") != original,
          "a changed gameplay object changes identity");
    check(levelFingerprint("kS38,1;1,1,2,10,3,20;1,8,2,50,3,40;21,31;") != original,
          "property 21 with value 31 is not an object ID");
    check(levelFingerprint("kS38,1;1,1,2,10,3,20;1,8,2,50,3,40;1,131;") != original,
          "object 131 is not a start position");
    check(levelFingerprint("kS38,1;1,1,2,10,3,20;1,8,2,50,3,40") == original,
          "optional final separator does not change identity");
}
} // namespace

int main() {
    boundariesAndContexts();
    trackerLifecycle();
    checkpointsAndCompletion();
    automaticStartPosCompletion();
    continuousRunAcrossWindows();
    originalNormalRunsShareWindowHistory();
    historySeparationAndRollingWindow();
    transferEvidence();
    combinedGameModes();
    invalidInputsAndAdvice();
    levelIdentity();
    std::cout << "Passed " << checks << " Context Trainer engine checks.\n";
}
