#pragma once

#include <Geode/Geode.hpp>
#include <vector>

namespace context {

struct StartPoint {
    int id = 0;
    double percent = 0;
};

class StartPositions {
public:
    bool discover(PlayLayer* play);
    std::vector<StartPoint> points() const;
    bool prepareReset(PlayLayer* play, int startId);
    void restore(PlayLayer* play);

private:
    struct NativePoint {
        StartPoint point;
        StartPosObject* object = nullptr;
    };

    PlayLayer* m_owner = nullptr;
    bool m_changed = false;
    geode::Ref<StartPosObject> m_original;
    std::vector<NativePoint> m_points;
};

}
