#include "Pacing.hpp"

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

void minimumActivePlay() {
    PracticePacing pacing;
    check(!pacing.canSwitch(60), "a new task cannot switch before any completed attempt");
    check(!pacing.takeReminder(1200, true, false), "a new session has no reminder due");
    pacing.addAttempt(59.9, false);
    check(!pacing.canSwitch(60), "59.9 seconds is below the one-minute minimum");
    pacing.addAttempt(.1, false);
    check(pacing.canSwitch(60), "exactly 60 active seconds permits a switch");
    check(pacing.runAttempts == 2 && near(pacing.runSeconds, 60), "multiple completed attempts share the task budget");
    check(pacing.canSwitch(60) && near(pacing.runSeconds, 60), "checking eligibility does not consume active play");
    check(!pacing.canSwitch(0), "zero means manual switching only");
    check(!pacing.canSwitch(120), "raising the minimum preserves an unfinished budget");

    PracticePacing longAttempt;
    check(!longAttempt.canSwitch(60), "an in-progress attempt has not been submitted at its boundary");
    longAttempt.addAttempt(95, false);
    check(longAttempt.canSwitch(60) && longAttempt.runAttempts == 1,
        "a long attempt becomes eligible only after its completed duration is submitted");

    PracticePacing instant;
    instant.addAttempt(0, false);
    check(instant.runAttempts == 1, "a zero-second completed attempt still counts as an attempt");
    check(!instant.canSwitch(60) && instant.reminderSeconds == 0,
        "an immediate death invents no active time");
}

void taskChangesKeepReminderTime() {
    PracticePacing pacing;
    pacing.addAttempt(90, false);
    pacing.resetRun();
    check(pacing.runSeconds == 0 && pacing.runAttempts == 0, "a new task starts with an empty task budget");
    check(near(pacing.reminderSeconds, 90), "switching tasks retains active time toward the break reminder");
    check(!pacing.canSwitch(60), "the previous task cannot satisfy the next task minimum");
    pacing.addAttempt(40, false);
    check(near(pacing.runSeconds, 40) && pacing.runAttempts == 1, "only new task attempts count toward its minimum");
    check(near(pacing.reminderSeconds, 130), "the reminder accumulates across tasks");
    pacing.resetRun();
    pacing.resetRun();
    check(near(pacing.reminderSeconds, 130), "repeated resets do not erase the reminder clock");
}

void fivePassesReleaseTheMinimum() {
    PracticePacing pacing;
    for (int i = 1; i <= 4; ++i) {
        pacing.addAttempt(1.5, false, true);
        check(pacing.consecutivePasses == i && !pacing.canSwitch(60),
            "four short passes do not release the minimum before the fifth attempt ends");
    }
    check(!pacing.canSwitch(60), "sampling an unfinished fifth attempt cannot release the start");
    pacing.addAttempt(1.5, false, true);
    check(pacing.consecutivePasses == 5 && near(pacing.runSeconds, 7.5) && pacing.canSwitch(60),
        "five consecutive 1.5-second passes release the one-minute minimum");
    check(pacing.canSwitch(600), "five clean passes override a longer configured minimum too");
    check(!pacing.canSwitch(0), "manual-only switching remains manual even after five passes");
    pacing.addAttempt(4, false, true);
    check(pacing.consecutivePasses == 5 && pacing.canSwitch(60),
        "a death beyond the passed target keeps its success streak and caps it at five");
    pacing.addAttempt(.5, false, false);
    check(pacing.consecutivePasses == 0 && !pacing.canSwitch(60),
        "a death before passing the target breaks the streak, including an early unentered death");
    for (int i = 0; i < 4; ++i) pacing.addAttempt(1.5, false, true);
    check(pacing.consecutivePasses == 4 && !pacing.canSwitch(60),
        "old successes cannot bridge a failure to create a new five-pass streak");
    pacing.addAttempt(1.5, false, true);
    check(pacing.canSwitch(60), "a new complete streak can release the minimum again");
    auto reminderTime = pacing.reminderSeconds;
    pacing.resetRun();
    check(pacing.consecutivePasses == 0 && !pacing.canSwitch(60),
        "changing the target or StartPos clears its streak");
    check(near(pacing.reminderSeconds, reminderTime),
        "moving on after a streak retains the break-reminder clock");

    PracticePacing diagnosis;
    for (int i = 0; i < 5; ++i) diagnosis.addAttempt(1.5, true, true);
    check(diagnosis.consecutivePasses == 0 && !diagnosis.canSwitch(60),
        "first-pass successes do not create a training streak");
    for (int i = 0; i < 4; ++i) diagnosis.addAttempt(1.5, false, true);
    diagnosis.addAttempt(std::numeric_limits<double>::quiet_NaN(), false, true);
    check(diagnosis.consecutivePasses == 4 && !diagnosis.canSwitch(60),
        "an invalid attempt cannot supply the fifth success");
    diagnosis.addAttempt(54, false, false);
    check(diagnosis.consecutivePasses == 0 && diagnosis.canSwitch(60),
        "elapsed play still releases an unstable run without a five-pass streak");
}

void firstPassIsExcluded() {
    PracticePacing pacing;
    pacing.addAttempt(3600, true);
    pacing.addAttempt(0, true);
    check(pacing.runSeconds == 0 && pacing.runAttempts == 0 && pacing.reminderSeconds == 0,
        "all first-pass attempts are excluded from training pacing");
    check(!pacing.canSwitch(60), "first-pass play cannot satisfy the next training task minimum");
    check(!pacing.takeReminder(1200, true, true), "first pass never emits a reminder");
    pacing.addAttempt(300, false);
    pacing.addAttempt(900, true);
    check(near(pacing.runSeconds, 300) && near(pacing.reminderSeconds, 300) && pacing.runAttempts == 1,
        "diagnosis mixed between training attempts adds no training time");
    pacing.addAttempt(900, false);
    check(!pacing.takeReminder(1200, true, true), "a due training reminder is suppressed during first pass");
    check(near(pacing.reminderSeconds, 1200), "first-pass checks preserve legitimate earlier training time");
    check(pacing.takeReminder(1200, true, false), "a due reminder is available again at a training boundary");
}

void remindersAreSoftAndOncePerBoundary() {
    PracticePacing pacing;
    pacing.addAttempt(1199.9, false);
    check(!pacing.takeReminder(1200, true, false), "no reminder before 20 active minutes");
    pacing.addAttempt(.1, false);
    check(pacing.takeReminder(1200, true, false), "one reminder is due at exactly 20 active minutes");
    check(pacing.reminderSeconds == 0, "taking a reminder starts its next interval");
    check(!pacing.takeReminder(1200, true, false), "checking the same boundary twice does not duplicate a reminder");
    check(near(pacing.runSeconds, 1200) && pacing.runAttempts == 2 && pacing.canSwitch(60),
        "a soft reminder leaves the current task and switching eligibility intact");
    pacing.addAttempt(3600, false);
    check(pacing.takeReminder(1200, true, false), "a very long attempt produces one reminder at its end");
    check(!pacing.takeReminder(1200, true, false) && pacing.reminderSeconds == 0,
        "missed intervals never create a catch-up notification queue");
    pacing.addAttempt(600, false);
    check(!pacing.takeReminder(1200, true, false), "the next reminder interval starts from zero after a long attempt");
}

void disabledRemindersHaveNoBacklog() {
    PracticePacing pacing;
    pacing.addAttempt(1500, false);
    check(!pacing.takeReminder(1200, false, false), "disabled reminders never emit");
    check(pacing.reminderSeconds == 0 && near(pacing.runSeconds, 1500) && pacing.runAttempts == 1,
        "disabling clears reminder debt without changing task history");
    pacing.addAttempt(600, false);
    check(!pacing.takeReminder(1200, true, false), "reenabling does not resurrect previously disabled intervals");
    pacing.addAttempt(600, false);
    check(pacing.takeReminder(1200, true, false), "a full enabled interval can produce a new reminder");
}

void invalidDurationsAndIntervals() {
    PracticePacing pacing;
    pacing.addAttempt(25, false);
    double const nan = std::numeric_limits<double>::quiet_NaN();
    double const infinity = std::numeric_limits<double>::infinity();
    for (double invalid : {-1., nan, infinity, -infinity}) {
        pacing.addAttempt(invalid, false);
        check(near(pacing.runSeconds, 25) && near(pacing.reminderSeconds, 25) && pacing.runAttempts == 1,
            "invalid attempt durations change neither time nor attempt count");
        check(!pacing.canSwitch(invalid), "invalid switching thresholds never become eligible");
    }
    for (double invalid : {0., -1., nan, infinity, -infinity}) {
        PracticePacing reminder;
        reminder.addAttempt(2400, false);
        check(!reminder.takeReminder(invalid, true, false) && reminder.reminderSeconds == 0,
            "invalid reminder intervals emit nothing and clear disabled interval debt");
    }

    PracticePacing large;
    double const maximum = std::numeric_limits<double>::max();
    large.addAttempt(maximum, false);
    large.addAttempt(maximum, false);
    check(std::isfinite(large.runSeconds) && std::isfinite(large.reminderSeconds),
        "large finite durations never overflow either clock");
    check(large.canSwitch(60) && large.takeReminder(1200, true, false),
        "a saturated valid clock can still switch and emit one reminder");
    large.runAttempts = std::numeric_limits<int>::max();
    large.addAttempt(0, false);
    check(large.runAttempts == std::numeric_limits<int>::max(), "attempt counts saturate without signed overflow");
}
} // namespace

int main() {
    minimumActivePlay();
    taskChangesKeepReminderTime();
    fivePassesReleaseTheMinimum();
    firstPassIsExcluded();
    remindersAreSoftAndOncePerBoundary();
    disabledRemindersHaveNoBacklog();
    invalidDurationsAndIntervals();
    std::cout << "Passed " << checks << " practice pacing checks.\n";
}
