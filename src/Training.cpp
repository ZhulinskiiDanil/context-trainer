#include "Training.hpp"

#include <algorithm>
#include <cmath>
#include <deque>

namespace context {
namespace {
constexpr double startTolerance = 0.01;

bool startPositionRecord(std::string_view object) {
    std::size_t pos = 0;
    while (pos < object.size()) {
        auto const keyEnd = object.find(',', pos);
        if (keyEnd == std::string_view::npos) break;
        auto valueEnd = object.find(',', keyEnd + 1);
        if (valueEnd == std::string_view::npos) valueEnd = object.size();
        if (object.substr(pos, keyEnd - pos) == "1" &&
            object.substr(keyEnd + 1, valueEnd - keyEnd - 1) == "31") return true;
        pos = valueEnd + 1;
    }
    return false;
}

bool matches(Attempt const& attempt, RunContext runContext, std::optional<bool> practice, Mode mode) {
    return attempt.context == runContext && matchesRecord(attempt, practice, mode);
}

Summary count(Window const& window, RunContext runContext, std::optional<bool> practice,
              Mode mode, Threshold threshold) {
    Summary result;
    for (auto const& attempt : window.history) {
        if (!matches(attempt, runContext, practice, mode)) continue;
        ++result.attempts;
        if (attempt.outcome == Outcome::Success) ++result.successes;
    }
    for (auto it = window.history.rbegin(); it != window.history.rend(); ++it) {
        if (!matches(*it, runContext, practice, mode)) continue;
        ++result.recentAttempts;
        if (it->outcome == Outcome::Success) ++result.recentSuccesses;
        if (result.recentAttempts == threshold.total) break;
    }
    result.repeatable = mode == Mode::Verify &&
                        result.recentAttempts == threshold.total &&
                        result.recentSuccesses >= threshold.required;
    return result;
}
} // namespace

bool matchesRecord(Attempt const& attempt, std::optional<bool> practice, Mode mode) {
    return (!practice.has_value() || attempt.practice == *practice) &&
        attempt.mode == mode && attempt.outcome != Outcome::Abandoned;
}

bool validWindow(Window const& window) {
    return std::isfinite(window.start) && std::isfinite(window.end) &&
           std::isfinite(window.leadIn) && window.start >= 0 &&
           window.start < window.end && window.end <= 100 &&
           window.leadIn > 0 && window.leadIn <= 100;
}

bool validAutoRunMode(bool beganInPractice, bool practiceNow,
                      bool atManagedStartPosTest, bool nativeCompleted) {
    return beganInPractice && (practiceNow || atManagedStartPosTest || nativeCompleted);
}

Threshold clampThreshold(Threshold threshold) {
    threshold.total = std::clamp(threshold.total, 1, 100);
    threshold.required = std::clamp(threshold.required, 1, threshold.total);
    return threshold;
}

bool validOriginalRunMode(bool practice, bool testMode, bool hasStartPos, bool ignoreDamage) {
    return !practice && !testMode && !hasStartPos && !ignoreDamage;
}

RunContext classify(double startPercent, Window const& window) {
    if (startPercent <= startTolerance) return RunContext::FromZero;
    if (startPercent <= window.start - window.leadIn) return RunContext::LeadIn;
    return RunContext::Fresh;
}

char const* contextName(RunContext value) {
    switch (value) {
        case RunContext::Fresh: return "Short start";
        case RunContext::LeadIn: return "Long entry";
        case RunContext::FromZero: return "From zero";
    }
    return "Unknown";
}

char const* outcomeName(Outcome value) {
    switch (value) {
        case Outcome::Success: return "Success";
        case Outcome::Failure: return "Failure";
        case Outcome::Abandoned: return "Unfinished";
    }
    return "Unknown";
}

char const* modeName(Mode value) {
    switch (value) {
        case Mode::Study: return "Study";
        case Mode::Verify: return "Verify";
    }
    return "Unknown";
}

std::uint64_t levelFingerprint(std::string_view decoded) {
    std::uint64_t hash = 14695981039346656037ull;
    auto const headerEnd = decoded.find(';');
    if (headerEnd == std::string_view::npos) return hash;
    auto pos = headerEnd + 1;
    while (pos < decoded.size()) {
        auto end = decoded.find(';', pos);
        if (end == std::string_view::npos) end = decoded.size();
        auto const object = decoded.substr(pos, end - pos);
        if (!object.empty() && !startPositionRecord(object)) {
            for (unsigned char value : object) {
                hash ^= value;
                hash *= 1099511628211ull;
            }
            hash ^= static_cast<unsigned char>(';');
            hash *= 1099511628211ull;
        }
        pos = end + 1;
    }
    return hash;
}

Summary summarize(Window const& window, RunContext runContext, std::optional<bool> practice,
                  Mode mode, Threshold threshold) {
    threshold = clampThreshold(threshold);
    Summary result = count(window, runContext, practice, mode, threshold);
    if (mode != Mode::Verify ||
        !count(window, RunContext::Fresh, practice, mode, threshold).repeatable) {
        return result;
    }

    std::deque<bool> freshResults;
    int freshSuccesses = 0;
    bool baselineEstablished = false;
    for (auto const& attempt : window.history) {
        if (!matchesRecord(attempt, practice, Mode::Verify)) continue;

        if (baselineEstablished && attempt.context == runContext &&
            attempt.outcome == Outcome::Success &&
            (runContext != RunContext::Fresh || attempt.returnCheck)) {
            result.transferred = true;
            break;
        }

        if (attempt.context != RunContext::Fresh) continue;
        bool const success = attempt.outcome == Outcome::Success;
        freshResults.push_back(success);
        freshSuccesses += success ? 1 : 0;
        if (static_cast<int>(freshResults.size()) > threshold.total) {
            freshSuccesses -= freshResults.front() ? 1 : 0;
            freshResults.pop_front();
        }
        if (static_cast<int>(freshResults.size()) == threshold.total &&
            freshSuccesses >= threshold.required) {
            baselineEstablished = true;
        }
    }
    return result;
}

std::string recommendation(Window const& window, Threshold threshold, int mood) {
    if (!validWindow(window)) return "Choose valid window boundaries before starting.";
    if (mood >= 2) {
        return "Finish this block or take a break. Return when you feel ready; stop if your hand hurts.";
    }
    if (mood == 1) {
        return "Keep the block short. Pick one question: the entry, a cue, or a longer run.";
    }
    if (window.history.empty()) {
        return "Choose one cause of failure and try this window. Include its entry and exit.";
    }
    auto const& latest = window.history.back();
    if (latest.mode == Mode::Study) {
        return "When the pattern is clear, switch to Verify at normal speed without learning aids.";
    }
    auto const fresh = summarize(window, RunContext::Fresh, std::nullopt, Mode::Verify, threshold);
    auto const leadIn = summarize(window, RunContext::LeadIn, std::nullopt, Mode::Verify, threshold);
    auto const zero = summarize(window, RunContext::FromZero, std::nullopt, Mode::Verify, threshold);
    if (fresh.transferred || leadIn.transferred || zero.transferred) {
        return "This window has transfer evidence. Test a wider entry or choose the next problem.";
    }
    if (fresh.repeatable) {
        if (leadIn.attempts > 0 && leadIn.successes == 0) {
            return "Short starts repeat, but longer entries fail. Inspect the preceding transition or workload.";
        }
        return "Short starts repeat. Try an earlier entry or mark one return check after a break.";
    }
    if (zero.repeatable || leadIn.repeatable) {
        return "This entry repeats. Compare it with a short start or verify it after a break.";
    }
    if (fresh.successes + leadIn.successes + zero.successes == 0) {
        return "Identify the cause of failure. Use Study for an unclear pattern, then verify its entry and exit.";
    }
    return "You have a successful run. Repeat this same context before testing a longer entry.";
}

void RunTracker::begin(Window const& window, double startPercent, Mode mode,
                       bool practice, bool returnCheck, std::int64_t timestamp) {
    m_attempt = Attempt{};
    m_attempt.context = classify(startPercent, window);
    m_attempt.mode = mode;
    m_attempt.practice = practice;
    m_attempt.returnCheck = returnCheck;
    m_attempt.startPercent = std::isfinite(startPercent) ? startPercent : 0;
    m_attempt.endPercent = m_attempt.startPercent;
    m_attempt.timestamp = timestamp;
    m_start = window.start;
    m_end = window.end;
    m_eligible = validWindow(window) && std::isfinite(startPercent) &&
                 startPercent >= 0 && startPercent <= 100 &&
                 startPercent <= m_start + startTolerance;
    m_entered = m_eligible && startPercent >= m_start - startTolerance;
    m_finished = false;
}

std::optional<Attempt> RunTracker::sample(double percent, double seconds, int clicks,
                                         int peakCps, bool completed) {
    if (m_finished || !m_eligible) return std::nullopt;
    if (completed) percent = 100;
    if (!std::isfinite(percent)) return std::nullopt;
    if (percent >= m_start) m_entered = true;
    if (!m_entered) return std::nullopt;
    // Displayed percentages can round to 100 before the actual completion event.
    if ((m_end < 100 && percent >= m_end) || completed) {
        return record(Outcome::Success, percent, seconds, clicks, peakCps);
    }
    return std::nullopt;
}

std::optional<Attempt> RunTracker::finish(Outcome outcome, double percent,
                                         double seconds, int clicks, int peakCps) {
    if (m_finished) return std::nullopt;
    if (!m_eligible || !std::isfinite(percent)) {
        m_finished = true;
        return std::nullopt;
    }
    // A death callback can be the first observation past the exit in this frame.
    if (outcome == Outcome::Failure) {
        if (auto result = sample(percent, seconds, clicks, peakCps)) return result;
    }
    if (percent >= m_start) m_entered = true;
    if (!m_entered) {
        m_finished = true;
        return std::nullopt;
    }
    // Successful finishes must be observed by sample(), including the completion hook.
    if (outcome == Outcome::Success) return std::nullopt;
    return record(outcome, percent, seconds, clicks, peakCps);
}

std::optional<Attempt> RunTracker::record(Outcome outcome, double percent,
                                         double seconds, int clicks, int peakCps) {
    m_finished = true;
    m_attempt.outcome = outcome;
    m_attempt.endPercent = std::clamp(percent, 0.0, 100.0);
    m_attempt.seconds = std::isfinite(seconds) ? std::max(0.0, seconds) : 0;
    m_attempt.clicks = std::max(0, clicks);
    m_attempt.peakCps = std::max(0, peakCps);
    return m_attempt;
}

} // namespace context
