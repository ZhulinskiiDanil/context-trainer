#pragma once
#include "Training.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <algorithm>

namespace context::ui {
using namespace geode::prelude;
inline constexpr ccColor3B Text{241, 238, 247};
inline constexpr ccColor3B Muted{180, 177, 193};
inline constexpr ccColor3B Accent{195, 172, 246};
inline constexpr ccColor3B Success{146, 210, 164};
inline constexpr ccColor3B Current{238, 208, 119};
inline constexpr ccColor3B Danger{236, 150, 149};
inline constexpr ccColor3B Surface{56, 53, 64};

inline CCLabelBMFont* text(CCNode* parent, std::string const& value, float x, float y,
    float width, float height = 11.f, ccColor3B color = Text, bool left = true,
    char const* font = "chatFont.fnt") {
    auto label = CCLabelBMFont::create(value.c_str(), font);
    label->setAnchorPoint({left ? 0.f : .5f, .5f});
    label->setPosition({x, y});
    label->setColor(color);
    float scale = height / std::max(1.f, label->getContentHeight());
    if (label->getContentWidth() > 0) scale = std::min(scale, width / label->getContentWidth());
    label->setScale(scale);
    parent->addChild(label);
    return label;
}

inline CCScale9Sprite* panel(CCNode* parent, float x, float y, float width, float height,
    char const* asset = "range-default-bg.png"_spr) {
    auto node = CCScale9Sprite::create(asset);
    node->setAnchorPoint({0, 0});
    node->setPosition({x, y});
    node->setContentSize({width, height});
    parent->addChild(node);
    return node;
}

inline CCMenu* menu(CCNode* parent) {
    auto node = CCMenu::create();
    node->setPosition({0, 0});
    parent->addChild(node, 5);
    return node;
}

enum class Tone { Normal, Primary, Secondary, Selected, Destructive };

inline CCSprite* buttonFace(float width, float height, Tone tone) {
    char const* asset = "GJ_button_04.png";
    ccColor3B tint = tone == Tone::Primary ? ccColor3B{124, 184, 153} :
        tone == Tone::Selected ? ccColor3B{157, 143, 192} :
        tone == Tone::Destructive ? ccColor3B{178, 105, 109} :
        tone == Tone::Secondary ? ccColor3B{166, 166, 177} : ccColor3B{185, 183, 200};
    auto face = CCSprite::create();
    face->setContentSize({width, height});
    face->setCascadeColorEnabled(true);
    face->setCascadeOpacityEnabled(true);
    auto background = CCScale9Sprite::create(asset);
    // Half-size borders keep GD's rounded corners intact on compact controls.
    background->setContentSize({width * 2, height * 2});
    background->setScale(.5f);
    background->setPosition({width / 2, height / 2});
    background->setColor(tint);
    face->addChild(background);
    return face;
}

inline CCMenuItemSpriteExtra* button(CCMenu* parent, std::string const& caption,
    float x, float y, float width, float height, CCObject* target, SEL_MenuHandler callback,
    int tag, Tone tone = Tone::Normal, char const* captionFont = "bigFont.fnt") {
    auto face = buttonFace(width, height, tone);
    bool marked = tone == Tone::Selected && width >= 70.f;
    float labelLeft = marked ? 25.f : 7.f;
    text(face, caption, (labelLeft + width - 7.f) / 2, height / 2,
        width - labelLeft - 7.f, std::min(14.f, height * .5f),
        {255, 255, 255}, false, captionFont);
    if (marked) {
        auto check = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        check->setScale(std::min(13.f, height * .55f) / check->getContentHeight());
        check->setPosition({14.f, height / 2});
        face->addChild(check);
    }
    auto item = CCMenuItemSpriteExtra::create(face, target, callback);
    item->m_scaleMultiplier = 1.04f;
    item->setTag(tag);
    item->setPosition({x, y});
    parent->addChild(item);
    return item;
}

inline CCMenuItemSpriteExtra* iconButton(CCMenu* parent, char const* frame,
    float x, float y, float size, CCObject* target, SEL_MenuHandler callback,
    int tag, bool flipX = false, bool enabled = true) {
    auto face = buttonFace(size, size, Tone::Normal);
    auto icon = CCSprite::createWithSpriteFrameName(frame);
    icon->setScale((size - 9.f) / std::max(icon->getContentWidth(), icon->getContentHeight()));
    icon->setFlipX(flipX);
    icon->setPosition({size / 2, size / 2});
    face->addChild(icon);
    auto item = CCMenuItemSpriteExtra::create(face, target, callback);
    item->m_scaleMultiplier = 1.08f;
    item->setTag(tag);
    item->setPosition({x, y});
    item->setEnabled(enabled);
    if (!enabled) item->setOpacity(100);
    parent->addChild(item);
    return item;
}

inline void line(CCNode* parent, float x, float y, float width, ccColor3B color = Surface) {
    auto draw = CCDrawNode::create();
    draw->drawSegment({x, y}, {x + width, y}, .5f,
        {color.r / 255.f, color.g / 255.f, color.b / 255.f, 1});
    parent->addChild(draw);
}

inline void fill(CCNode* parent, float x, float y, float width, float height, ccColor3B color) {
    if (width <= 0 || height <= 0) return;
    auto draw = CCDrawNode::create();
    CCPoint points[]{{x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}};
    draw->drawPolygon(points, 4, {color.r / 255.f, color.g / 255.f, color.b / 255.f, 1}, 0, {0, 0, 0, 0});
    parent->addChild(draw);
}

inline void meter(CCNode* parent, float x, float y, float width, float height,
    double ratio, ccColor3B color) {
    fill(parent, x, y, width, height, Surface);
    fill(parent, x, y, width * static_cast<float>(std::clamp(ratio, 0., 1.)), height, color);
}

inline void interval(CCNode* parent, float x, float y, float width, double start, double end) {
    float a = static_cast<float>(std::clamp(start, 0., 100.) / 100.);
    float b = static_cast<float>(std::clamp(end, start, 100.) / 100.);
    fill(parent, x, y, width, 5, Surface);
    fill(parent, x + a * width, y, std::max(2.f, (b - a) * width), 5, Current);
    fill(parent, x + a * width, y - 2, 1, 9, Current);
    fill(parent, x + b * width - 1, y - 2, 1, 9, Current);
    text(parent, "0%", x, y - 10, 24, 8, Muted);
    text(parent, "100%", x + width - 28, y - 10, 28, 8, Muted);
}

inline void outcomeStrip(CCNode* parent, Window const& window, RunContext context,
    std::optional<bool> practice, Mode mode, float x, float y, float width, float height = 16.f) {
    std::vector<Outcome> results;
    for (auto it = window.history.rbegin(); it != window.history.rend() && results.size() < 6; ++it)
        if (it->context == context && matchesRecord(*it, practice, mode)) results.push_back(it->outcome);
    std::reverse(results.begin(), results.end());
    float step = (width + 3) / 6;
    int empty = 6 - static_cast<int>(results.size());
    for (int i = 0; i < 6; ++i) {
        float left = x + i * step;
        bool recorded = i >= empty;
        bool passed = recorded && results[i - empty] == Outcome::Success;
        ccColor3B color = !recorded ? Muted : passed ? Success : Danger;
        fill(parent, left, y, step - 3, height, Surface);
        if (recorded) fill(parent, left, y, step - 3, 1, color);
        text(parent, !recorded ? "-" : passed ? "Pass" : "Fail",
            left + (step - 3) / 2, y + height / 2 + .5f, step - 7,
            std::min(9.f, height * .65f), color, false);
    }
}

inline ScrollLayer* list(CCNode* parent, float x, float y, float width, float height) {
    auto scroll = ScrollLayer::create(CCSize{width, height});
    scroll->setStealingTouches(true);
    scroll->setPosition({x, y});
    scroll->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(4));
    parent->addChild(scroll);
    return scroll;
}

inline std::string percent(double value) { return fmt::format("{:.1f}%", value); }
inline std::string range(double start, double end) {
    return percent(start) + " - " + (end >= 100 ? "Finish" : percent(end));
}
} // namespace context::ui
