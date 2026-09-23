#include "Analysis.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>

namespace context {
namespace {
constexpr std::uint32_t memoryTag = 1u;
constexpr std::uint32_t inputTag = 1u << 2;
constexpr std::uint32_t transitionTag = 1u << 3;

bool validPercent(double value) {
    return std::isfinite(value) && value >= 0 && value <= 100;
}

int binAt(double value) { return std::min(99, static_cast<int>(value)); }

void addCount(int& target, int value = 1) {
    target = static_cast<int>(std::min<std::int64_t>(
        std::numeric_limits<int>::max(), std::max(0, target) + static_cast<std::int64_t>(std::max(0, value))));
}

double positive(double value) { return std::isfinite(value) && value > 0 ? value : 0; }

std::string decimal(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(value == std::floor(value) ? 0 : 1) << value;
    return out.str();
}

struct Evidence {
    double seconds = 0;
    int deaths = 0;
    int clicks = 0;
    int transitions = 0;
    int covered = 0;
    int inputBins = 0;
    int visits = 0;
};

Evidence evidence(Survey const& survey, double start, double end) {
    Evidence result;
    int const first = binAt(start);
    int const last = std::min(99, static_cast<int>(std::ceil(end)) - 1);
    for (int i = first; i <= last; ++i) {
        auto const& bin = survey.bins[i];
        if (!bin.covered) continue;
        ++result.covered;
        auto seconds = positive(bin.seconds);
        // Survey bins have one-percent resolution; do not invent finer event positions.
        result.seconds += std::min(seconds, 1e9);
        addCount(result.deaths, bin.deaths);
        addCount(result.clicks, bin.clicks);
        addCount(result.transitions, bin.transitions);
        result.visits = std::max(result.visits, bin.visits);
        if (bin.clicks > 0 && seconds > 0) ++result.inputBins;
    }
    return result;
}

bool inputHeavy(Evidence const& value) {
    return value.seconds / std::max(1, value.visits) >= 2.5 &&
           value.clicks >= 15 && value.inputBins >= 2 &&
           value.clicks / value.seconds >= 5;
}

Evidence strongestInput(Survey const& survey, double start, double end) {
    Evidence result;
    double best = 0;
    for (int i = binAt(start); i < std::min(100, static_cast<int>(std::ceil(end))); ++i) {
        auto const current = evidence(survey, std::max(start, i - 2.0), std::min(end, i + 3.0));
        if (!inputHeavy(current)) continue;
        double const score = current.clicks / current.seconds;
        if (score > best) {
            result = current;
            best = score;
        }
    }
    return result;
}

SuggestedWindow proposal(double start, double end, Evidence const& observed,
                         AnalysisOptions const& options, bool fallback = false,
                         Evidence const* inputSource = nullptr) {
    SuggestedWindow result;
    auto& window = result.window;
    window.start = start;
    window.end = end;
    window.leadIn = std::max(1.0, std::min(5.0, (end - start) / 2));
    result.deaths = observed.deaths;
    result.observedSeconds = observed.seconds;
    result.priority = 10 + (100 - start) / 100;
    std::string title = fallback ? "Explore" : "Interval";
    std::vector<std::string> reasons;
    if (options.deaths && observed.deaths > 0) {
        result.priority += (observed.deaths >= 2 ? 35 : 8) + 8 * std::log1p(observed.deaths);
        reasons.push_back(std::to_string(observed.deaths) + " observed death" + (observed.deaths == 1 ? "" : "s"));
        if (observed.deaths >= 2) title = "Retry";
    }
    if (options.transitions && observed.transitions > 0) {
        window.tags |= transitionTag;
        result.priority += 15 + 3 * std::log1p(observed.transitions);
        reasons.push_back("observed mode change");
        if (title == "Interval" || title == "Explore") title = "Entry";
    }
    auto const& inputs = inputSource ? *inputSource : observed;
    if (options.inputs && inputHeavy(inputs)) {
        window.tags |= inputTag;
        result.priority += 25 + std::min(20.0, inputs.clicks / inputs.seconds);
        reasons.push_back(std::to_string(inputs.clicks) + " inputs in " + decimal(inputs.seconds) + "s locally");
        if (title == "Interval" || title == "Explore") title = "Inputs";
    }
    if (options.memory) {
        window.tags |= memoryTag;
        reasons.push_back("memory practice selected");
    }
    if (reasons.empty()) reasons.push_back("observed during the first pass");
    for (auto const& reason : reasons) {
        if (!result.reason.empty()) result.reason += "; ";
        result.reason += reason;
    }
    result.reason += ".";
    window.name = title + " " + decimal(start) + "-" + decimal(end);
    window.note = result.reason;
    // Duration influences scheduling cost; it is not a diagnosis of fatigue.
    result.priority -= std::min(10.0, std::log1p(observed.seconds) * 1.5);
    return result;
}

bool higherPriority(SuggestedWindow const& a, SuggestedWindow const& b) {
    if (a.priority != b.priority) return a.priority > b.priority;
    return a.window.start < b.window.start;
}

constexpr std::array<RunContext, 3> contexts = {
    RunContext::Fresh, RunContext::LeadIn, RunContext::FromZero
};
} // namespace

void SurveyRecorder::observe(int index) {
    auto& bin = m_data.bins[index];
    bin.covered = true;
    if (!m_visited[index]) {
        addCount(bin.visits);
        m_visited[index] = true;
    }
}

void SurveyRecorder::begin(double actualStart, int form) {
    cancelRun();
    if (!validPercent(actualStart)) return;
    m_visited.fill(false);
    m_start = m_previous = actualStart;
    m_form = form;
    m_active = true;
    addCount(m_data.runs);
}

void SurveyRecorder::sample(double percent, double dt, int form, int newClicks) {
    if (!m_active || !validPercent(percent) || percent < m_start ||
        !std::isfinite(dt) || dt <= 0) return;
    // A long scheduling gap cannot claim an unbounded amount of observed play.
    dt = std::min(dt, 60.0);
    int const destination = binAt(percent);
    double const delta = percent - m_previous;
    if (delta > 0 && delta <= 5) {
        for (int i = binAt(m_previous); i <= destination; ++i) {
            observe(i);
            double const distance = std::max(0.0, std::min(percent, i + 1.0) - std::max(m_previous, static_cast<double>(i)));
            m_data.bins[i].seconds += dt * distance / delta;
        }
    } else {
        observe(destination);
        m_data.bins[destination].seconds += dt;
    }
    m_data.totalSeconds += dt;
    addCount(m_data.bins[destination].clicks, newClicks);
    if (delta >= 0 && delta <= 5 && form >= 0 && m_form >= 0 && form != m_form)
        addCount(m_data.bins[destination].transitions);
    m_previous = percent;
    m_form = form;
}

void SurveyRecorder::died(double percent) {
    if (!m_active || !validPercent(percent) || percent < m_start) return;
    observe(binAt(percent));
    addCount(m_data.bins[binAt(percent)].deaths);
    cancelRun();
}

void SurveyRecorder::checkpoint(double percent) {
    if (!m_active || !validPercent(percent) || percent < m_start) return;
    observe(binAt(percent));
    addCount(m_data.bins[binAt(percent)].checkpoints);
}

void SurveyRecorder::complete() {
    if (!m_active) return;
    observe(99);
    m_data.completed = true;
    cancelRun();
}

void SurveyRecorder::reset() {
    m_data = {};
    m_visited.fill(false);
    m_start = m_previous = 0;
    m_form = -1;
    m_active = false;
}

void SurveyRecorder::cancelRun() { m_active = false; }

double coverage(Survey const& survey) {
    return static_cast<double>(std::count_if(survey.bins.begin(), survey.bins.end(),
                                            [](auto const& bin) { return bin.covered; }));
}

int calibrateStage(std::vector<double>& starts, std::vector<bool>& calibrated,
                   std::vector<bool>& passed, std::vector<double> const& passedEnds,
                   std::size_t index, double actual) {
    if (index >= starts.size() || calibrated.size() != starts.size() ||
        passed.size() != starts.size() || passedEnds.size() != starts.size() ||
        !validPercent(actual) || actual == 100) return -2;
    for (std::size_t i = 0; i < starts.size(); ++i) {
        if (i == index || !calibrated[i]) continue;
        if (!validPercent(starts[i]) || starts[i] == 100 ||
            (i < index && starts[i] >= actual) || (i > index && starts[i] <= actual)) return -2;
    }
    if (index > 0 && passed[index - 1] && !validPercent(passedEnds[index - 1])) return -2;
    starts[index] = actual;
    calibrated[index] = true;
    if (index > 0 && passed[index - 1] && actual > passedEnds[index - 1] + .01) {
        passed[index - 1] = false;
        return static_cast<int>(index - 1);
    }
    return -1;
}

std::vector<SuggestedWindow> analyze(Survey const& survey, AnalysisOptions const& options,
                                   std::vector<double> const& boundaries) {
    std::vector<SuggestedWindow> candidates;
    if (!boundaries.empty()) {
        std::vector<double> starts;
        for (double start : boundaries) if (validPercent(start)) starts.push_back(start);
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        if (starts.empty()) return candidates;
        if (starts.back() != 100) starts.push_back(100);
        for (std::size_t i = 0; i + 1 < starts.size(); ++i) {
            auto observed = evidence(survey, starts[i], starts[i + 1]);
            auto const inputs = strongestInput(survey, starts[i], starts[i + 1]);
            auto candidate = proposal(starts[i], starts[i + 1], observed, options, false, &inputs);
            if (observed.covered == 0 || observed.seconds <= 0) {
                candidate.priority = 0;
                candidate.reason = "Selected interval; insufficient timing evidence.";
                candidate.window.note = candidate.reason;
            }
            candidates.push_back(std::move(candidate));
        }
        std::sort(candidates.begin(), candidates.end(), higherPriority);
        return candidates;
    }
    if (coverage(survey) == 0) return candidates;
    if (!options.deaths && !options.inputs && !options.transitions && !options.memory) return candidates;

    auto appendAround = [&](int center, int before, int after, bool fallback) {
        if (!survey.bins[center].covered) return;
        int left = center;
        int right = center;
        while (left > 0 && survey.bins[left - 1].covered) --left;
        while (right < 99 && survey.bins[right + 1].covered) ++right;
        double const start = std::max(left, center - before);
        double const end = std::min(right + 1, center + after + 1);
        auto const observed = evidence(survey, start, end);
        if (end - start < 3 || observed.seconds < .5) return;
        auto const inputs = strongestInput(survey, start, end);
        candidates.push_back(proposal(start, end, observed, options, fallback, &inputs));
    };
    for (int i = 0; i < 100; ++i) {
        if (!survey.bins[i].covered) continue;
        auto const local = evidence(survey, std::max(0, i - 2), std::min(100, i + 3));
        if (options.deaths && survey.bins[i].deaths > 0 && local.deaths >= 2)
            appendAround(i, 4, 3, false);
        if (options.transitions && survey.bins[i].transitions > 0)
            appendAround(i, 4, 3, false);
        if (options.inputs && inputHeavy(local)) appendAround(i, 4, 3, false);
    }

    if (candidates.empty()) {
        // A smooth first pass still permits a small, explicit exploration plan.
        int longest = -1;
        double longestSeconds = 0;
        for (int i = 0; i < 100; ++i) {
            if (survey.bins[i].covered && positive(survey.bins[i].seconds) > longestSeconds) {
                longest = i;
                longestSeconds = survey.bins[i].seconds;
            }
        }
        if (longest >= 0) appendAround(longest, 3, 5, true);
        if (survey.completed && survey.bins[99].covered) appendAround(99, 7, 0, true);
    }
    std::sort(candidates.begin(), candidates.end(), higherPriority);
    std::vector<SuggestedWindow> result;
    for (auto& candidate : candidates) {
        bool overlaps = false;
        for (auto& chosen : result) {
            double const common = std::max(0.0, std::min(chosen.window.end, candidate.window.end) -
                                                   std::max(chosen.window.start, candidate.window.start));
            double const shorter = std::min(chosen.window.end - chosen.window.start,
                                            candidate.window.end - candidate.window.start);
            if (common / shorter >= .6) {
                chosen.window.tags |= candidate.window.tags;
                overlaps = true;
                break;
            }
        }
        if (!overlaps) result.push_back(std::move(candidate));
        if (result.size() == 8) break;
    }
    return result;
}

int chooseNext(std::vector<Window> const& windows, std::vector<int> const& selectedIds,
               Threshold threshold, bool longerEntries) {
    int chosen = 0;
    double best = -std::numeric_limits<double>::infinity();
    double chosenStart = 100;
    std::int64_t newest = 0;
    for (auto const& window : windows) {
        if (std::find(selectedIds.begin(), selectedIds.end(), window.id) == selectedIds.end()) continue;
        for (auto const& attempt : window.history) {
            if (attempt.mode == Mode::Verify && attempt.outcome != Outcome::Abandoned)
                newest = std::max(newest, attempt.timestamp);
        }
    }
    for (auto const& window : windows) {
        if (window.id <= 0 || !validWindow(window) ||
            std::find(selectedIds.begin(), selectedIds.end(), window.id) == selectedIds.end()) continue;
        int attempts = 0;
        double failureRate = 0;
        bool repeated = false;
        bool transferred = false;
        for (auto context : contexts) {
            auto const summary = summarize(window, context, std::nullopt, Mode::Verify, threshold);
            attempts += summary.attempts;
            repeated = repeated || summary.repeatable;
            transferred = transferred || summary.transferred;
            if (summary.recentAttempts >= 3)
                failureRate = std::max(failureRate, 1.0 - static_cast<double>(summary.recentSuccesses) / summary.recentAttempts);
        }
        double cost = 0;
        int timed = 0;
        std::int64_t lastTrial = 0;
        for (auto const& attempt : window.history) {
            if (!matchesRecord(attempt, std::nullopt, Mode::Verify)) continue;
            lastTrial = std::max(lastTrial, attempt.timestamp);
            if (positive(attempt.seconds) <= 0) continue;
            cost += std::min(attempt.seconds, 1e6);
            ++timed;
        }
        if (timed) cost /= timed;
        double score = attempts == 0 ? 90 : attempts < 3 ? 100 : 85 + 20 * failureRate;
        if (repeated) score = longerEntries && window.start > .01 && !transferred ? 68 : 35;
        score -= std::min(40.0, attempts * 1.5);
        score -= std::min(25.0, 5 * std::log1p(cost / 5));
        score += (100 - window.start) / 50;
        score += std::clamp(positive(window.diagnosticWeight) / 5, 0.0, 20.0);
        if (lastTrial > 0 && newest > lastTrial)
            score += std::min(200.0, static_cast<double>(newest - lastTrial) / 5);
        if (score > best || (score == best && (window.start < chosenStart ||
            (window.start == chosenStart && window.id < chosen)))) {
            chosen = window.id;
            chosenStart = window.start;
            best = score;
        }
    }
    return chosen;
}

std::string nextTask(Window const& window, Threshold threshold, AnalysisOptions options) {
    if (!validWindow(window)) return "Choose a valid interval.";
    auto const fresh = summarize(window, RunContext::Fresh, std::nullopt, Mode::Verify, threshold);
    auto const lead = summarize(window, RunContext::LeadIn, std::nullopt, Mode::Verify, threshold);
    auto const zero = summarize(window, RunContext::FromZero, std::nullopt, Mode::Verify, threshold);
    std::string task;
    if (fresh.repeatable && options.longerEntries && !lead.transferred && !zero.transferred) {
        task = lead.attempts >= 3 && !lead.repeatable
            ? "Short starts repeat; inspect the earlier entry, then retry it."
            : "Short starts repeat; use the previous selected StartPos for a longer entry.";
    } else if (fresh.repeatable || lead.repeatable || zero.repeatable) {
        task = "This entry repeats. Work on another interval or check it again after a break.";
    } else if (fresh.attempts + lead.attempts + zero.attempts == 0) {
        task = options.memory
            ? "Recall the pattern in Study, then check this interval in Verify."
            : "Repeat " + decimal(window.start) + "-" + decimal(window.end) + "% in Verify; the scan is not readiness evidence.";
    } else {
        task = "Repeat the same entry to measure consistency. Normal and Practice both count.";
    }
    if (options.restReminders && (window.tags & inputTag)) task += " Keep the block short, then rest.";
    return task;
}

} // namespace context
