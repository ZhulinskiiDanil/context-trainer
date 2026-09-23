#include "StartPositions.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace context {
namespace {
void selectStart(PlayLayer* play, StartPosObject* object) {
    play->removeAllCheckpoints();
    // Native reset otherwise reloads the cached snapshot of the previous StartPos.
    CC_SAFE_RELEASE_NULL(play->m_currentCheckpoint);
    play->setStartPosObject(object);
}
}

bool StartPositions::discover(PlayLayer* play) {
    if (!play || play->m_isPlatformer || !play->m_objects || play->m_loadingProgress < 1.f) return false;
    auto const length = play->m_levelLength;
    if (!std::isfinite(length) || length <= 0) return false;
    if (play != m_owner) {
        m_original = play->m_startPosObject;
        m_owner = play;
        m_changed = false;
    }
    m_points.clear();
    m_points.push_back({{0, 0}, nullptr});

    for (auto object : CCArrayExt<GameObject*>(play->m_objects)) {
        auto start = typeinfo_cast<StartPosObject*>(object);
        if (!start || !start->m_startSettings) continue;
        auto const x = start->getPositionX();
        if (!std::isfinite(x) || x < 0 || x >= length) continue;
        // Match Blitzkrieg's 2.1 search: progress depends only on X and level length.
        auto const percent = 100.0 * x / length;
        m_points.push_back({{0, percent}, start});
    }

    std::stable_sort(m_points.begin() + 1, m_points.end(), [](auto const& left, auto const& right) {
        return left.point.percent < right.point.percent;
    });
    for (std::size_t i = 1; i < m_points.size(); ++i) {
        m_points[i].point.id = static_cast<int>(i);
    }
    return true;
}

std::vector<StartPoint> StartPositions::points() const {
    std::vector<StartPoint> result;
    result.reserve(m_points.size());
    for (auto const& entry : m_points) result.push_back(entry.point);
    return result;
}

bool StartPositions::prepareReset(PlayLayer* play, int startId) {
    if (!play || play != m_owner || play != PlayLayer::get() || play->m_isPlatformer ||
        !play->m_checkpointArray) return false;
    auto selected = std::find_if(m_points.begin(), m_points.end(), [startId](auto const& entry) {
        return entry.point.id == startId;
    });
    if (selected == m_points.end()) return false;
    selectStart(play, selected->object);
    m_changed = true;
    return true;
}

void StartPositions::restore(PlayLayer* play) {
    if (!m_changed || !play || play != m_owner || play != PlayLayer::get() || !play->m_checkpointArray) return;
    selectStart(play, m_original.data());
    m_changed = false;
}

}
