#pragma once
#include "Training.hpp"

namespace context {

struct PendingResult {
    int windowId;
    Attempt attempt;
};

// Results remain provisional until the gameplay attempt ends.
class AttemptIntegrity {
public:
    void reset();
    void collision(bool anticheatObject, bool playerDead, bool levelEnding);
    void damageFlags(bool ignoreDamageEnabled, bool ignoreDamage);
    void clock(double incomingDelta, double deliveredDelta, double schedulerScale);
    void queue(int windowId, Attempt const& attempt);
    std::vector<PendingResult> takeResults();
    bool blocked() const { return m_noclip || m_speedhack; }
    char const* reason() const;

private:
    bool m_noclip = false;
    bool m_speedhack = false;
    std::vector<PendingResult> m_pending;
};

} // namespace context
