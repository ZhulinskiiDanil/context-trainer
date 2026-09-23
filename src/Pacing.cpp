#include "Pacing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace context {

void PracticePacing::resetRun() {
    runSeconds = 0;
    runAttempts = 0;
    consecutivePasses = 0;
}

void PracticePacing::addAttempt(double seconds, bool firstPass, bool targetPassed) {
    if (firstPass || !std::isfinite(seconds) || seconds < 0) return;
    double const maximum = std::numeric_limits<double>::max();
    runSeconds += std::min(seconds, maximum - runSeconds);
    reminderSeconds += std::min(seconds, maximum - reminderSeconds);
    if (runAttempts < std::numeric_limits<int>::max()) ++runAttempts;
    consecutivePasses = targetPassed ? std::min(consecutivePasses + 1, 5) : 0;
}

bool PracticePacing::canSwitch(double minimumSeconds) const {
    return std::isfinite(minimumSeconds) && minimumSeconds > 0 &&
        std::isfinite(runSeconds) && runAttempts > 0 &&
        (runSeconds >= minimumSeconds || consecutivePasses >= 5);
}

bool PracticePacing::takeReminder(double intervalSeconds, bool enabled, bool firstPass) {
    if (!enabled || !std::isfinite(intervalSeconds) || intervalSeconds <= 0) {
        reminderSeconds = 0;
        return false;
    }
    if (firstPass || !std::isfinite(reminderSeconds) || reminderSeconds < intervalSeconds) return false;
    reminderSeconds = 0;
    return true;
}

} // namespace context
