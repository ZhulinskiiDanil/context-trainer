#include "Integrity.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace context {

void AttemptIntegrity::reset() {
    m_noclip = m_speedhack = false;
    m_pending.clear();
}

void AttemptIntegrity::collision(bool anticheatObject, bool playerDead, bool levelEnding) {
    if (!anticheatObject && !playerDead && !levelEnding) m_noclip = true;
}

void AttemptIntegrity::damageFlags(bool ignoreDamageEnabled, bool ignoreDamage) {
    m_noclip = m_noclip || ignoreDamageEnabled || ignoreDamage;
}

void AttemptIntegrity::clock(double incomingDelta, double deliveredDelta, double schedulerScale) {
    if (!std::isfinite(schedulerScale) || std::abs(schedulerScale - 1.) > .0001)
        m_speedhack = true;
    // Compare the same scheduler call before/after other hooks, not wall time or physics ticks.
    if (std::isfinite(incomingDelta) && incomingDelta > 0 && std::isfinite(deliveredDelta) &&
        std::abs(deliveredDelta - incomingDelta) > std::max(.000001, incomingDelta * .001))
        m_speedhack = true;
}

void AttemptIntegrity::queue(int windowId, Attempt const& attempt) {
    if (!blocked()) m_pending.push_back({windowId, attempt});
}

std::vector<PendingResult> AttemptIntegrity::takeResults() {
    if (blocked()) m_pending.clear();
    return std::exchange(m_pending, {});
}

char const* AttemptIntegrity::reason() const {
    if (m_noclip && m_speedhack) return "Noclip + speedhack";
    return m_noclip ? "Noclip" : m_speedhack ? "Speedhack" : "";
}

} // namespace context
