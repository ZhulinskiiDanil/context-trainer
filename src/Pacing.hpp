#pragma once

namespace context {

struct PracticePacing {
    double runSeconds = 0;
    double reminderSeconds = 0;
    int runAttempts = 0;
    int consecutivePasses = 0;

    // Keep the break-reminder clock when moving to another training task.
    void resetRun();

    // Submit once at the end of a completed attempt, never for an abandoned one.
    // First-pass diagnosis does not contribute to either clock.
    void addAttempt(double seconds, bool firstPass, bool targetPassed = false);

    // Five consecutive target passes can release the time minimum.
    // A zero or invalid minimum disables automatic switching.
    bool canSwitch(double minimumSeconds) const;

    // Check at an attempt boundary. Consumes at most one reminder, without catch-up.
    // Disabling reminders clears their clock; first-pass checks only suppress them.
    bool takeReminder(double intervalSeconds, bool enabled, bool firstPass);
};

} // namespace context
