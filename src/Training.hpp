#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace context {

enum class RunContext { Fresh, LeadIn, FromZero };
enum class Mode { Study, Verify };
enum class Outcome { Success, Failure, Abandoned };

struct Attempt {
    RunContext context = RunContext::Fresh;
    Mode mode = Mode::Verify;
    Outcome outcome = Outcome::Abandoned;
    bool practice = false;
    bool returnCheck = false;
    double startPercent = 0;
    double endPercent = 0;
    double seconds = 0;
    int clicks = 0;
    int peakCps = 0;
    std::int64_t timestamp = 0;
};

struct Window {
    int id = 0;
    std::string name;
    std::string note;
    double start = 0;
    double end = 100;
    double leadIn = 5;
    std::uint32_t tags = 0;
    double diagnosticWeight = 0;
    std::vector<Attempt> history;
    bool positionBased = false;
};

struct Threshold {
    int required = 4;
    int total = 6;
};

struct Summary {
    int successes = 0;
    int attempts = 0;
    int recentSuccesses = 0;
    int recentAttempts = 0;
    bool repeatable = false;
    bool transferred = false;
};

bool validWindow(Window const& window);
// Managed test mode must refer to the same StartPos selected at run start.
// nativeCompleted is an engine completion event, never a displayed percentage.
bool validAutoRunMode(bool beganInPractice, bool practiceNow,
                      bool atManagedStartPosTest, bool nativeCompleted);
bool validOriginalRunMode(bool practice, bool testMode, bool hasStartPos, bool ignoreDamage);
Threshold clampThreshold(Threshold threshold);
RunContext classify(double startPercent, Window const& window);
char const* contextName(RunContext value);
char const* outcomeName(Outcome value);
char const* modeName(Mode value);

// Hashes decoded object records, ignoring StartPos objects (ID 31) and the header.
// Initial gameplay settings in the header are therefore not part of this identity.
std::uint64_t levelFingerprint(std::string_view decoded);

// History belongs to these exact window boundaries. Editing them starts a new history.
// Transfer is measured in the selected context, after a repeatable Fresh baseline.
// An empty game-mode filter combines Normal and Practice without changing entry context.
bool matchesRecord(Attempt const& attempt, std::optional<bool> practice, Mode mode);
Summary summarize(Window const& window, RunContext runContext, std::optional<bool> practice,
                  Mode mode, Threshold threshold);
std::string recommendation(Window const& window, Threshold threshold, int mood);

class RunTracker {
public:
    void begin(Window const& window, double startPercent, Mode mode,
               bool practice, bool returnCheck, std::int64_t timestamp);
    std::optional<Attempt> sample(double percent, double seconds, int clicks,
                                  int peakCps, bool completed = false);
    std::optional<Attempt> finish(Outcome outcome, double percent, double seconds,
                                  int clicks, int peakCps);
    bool entered() const { return m_entered; }
    bool eligible() const { return m_eligible; }

private:
    std::optional<Attempt> record(Outcome outcome, double percent, double seconds,
                                  int clicks, int peakCps);

    Attempt m_attempt;
    double m_start = 0;
    double m_end = 100;
    bool m_eligible = false;
    bool m_entered = false;
    bool m_finished = true;
};

} // namespace context
