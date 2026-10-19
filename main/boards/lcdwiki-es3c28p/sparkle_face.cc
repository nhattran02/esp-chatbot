#include "sparkle_face.h"
#include "sparkle_assets.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace {

constexpr uint32_t kCyan = 0x5CE1E6;
constexpr uint32_t kPink = 0xFF6FB5;
constexpr uint32_t kPurple = 0xB79CFF;
constexpr uint32_t kYellow = 0xFFE27A;
constexpr uint32_t kAngryRed = 0xFF8FA3;
constexpr uint32_t kTearBlue = 0x7FC8FF;
constexpr uint32_t kSweatBlue = 0xA8ECFF;
constexpr uint32_t kFaceScreen = 0x0B0620;

// Idle face screen geometry (94x76)
constexpr int32_t kEyeCx[2] = {27, 66};
constexpr int32_t kEyeCy = 32;
constexpr int32_t kEyeW = 19;
constexpr int32_t kEyeH = 24;
constexpr int32_t kMouthX = 47;
constexpr int32_t kMouthY = 54;
constexpr int32_t kGlowW = 47;  // sparkle_glow_eye_tall: 19x24 eye + halo
constexpr int32_t kGlowH = 52;
constexpr int32_t kGlyphSize = 24;
constexpr float kPi = 3.14159265f;

constexpr uint32_t kMorphMs = 280;

// Eye presets: w, h, dx, dy, lid_top, lid_tilt (deg), lid_bottom, glyph_mix
constexpr SparkleFace::EyeParams kOpen = {19, 24, 0, 0, 0, 0, 0, 0};
constexpr SparkleFace::EyeParams kCrescent = {21, 22, 0, 1, 0, 0, 12, 0};
constexpr SparkleFace::EyeParams kClosed = {20, 3, 0, 5, 0, 0, 0, 0};
constexpr SparkleFace::EyeParams kGlyphEye = {22, 22, 0, 0, 0, 0, 0, 256};

lv_obj_t* Plain(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t* Fill(lv_obj_t* parent, uint32_t color) {
    lv_obj_t* obj = Plain(parent);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    return obj;
}

lv_obj_t* MaskImage(lv_obj_t* parent, const lv_image_dsc_t* src, uint32_t color) {
    lv_obj_t* img = lv_image_create(parent);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(img, src);
    lv_obj_set_style_image_recolor(img, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    return img;
}

lv_obj_t* MouthArc(lv_obj_t* parent, int32_t start, int32_t end, int32_t width) {
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(arc, start, end);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(kPink), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 0, LV_PART_INDICATOR);
    return arc;
}

inline void SetHidden(lv_obj_t* obj, bool hidden) {
    if (hidden != lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        if (hidden) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// Store v in slot and report whether it differs from the previous frame
inline bool Changed(int32_t& slot, int32_t v) {
    if (slot == v) {
        return false;
    }
    slot = v;
    return true;
}

inline int16_t Lerp(int16_t a, int16_t b, float t) {
    return static_cast<int16_t>(lroundf(a + (b - a) * t));
}

inline float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Settles with a small overshoot, which makes shapes feel springy
inline float EaseOutBack(float t) {
    constexpr float c1 = 1.3f;
    constexpr float c3 = c1 + 1;
    float u = t - 1;
    return 1 + c3 * u * u * u + c1 * u * u;
}

uint32_t MixColor(uint32_t a, uint32_t b, float t) {
    auto channel = [&](int shift) {
        int ca = (a >> shift) & 0xFF;
        int cb = (b >> shift) & 0xFF;
        return static_cast<uint32_t>(std::clamp<int>(lroundf(ca + (cb - ca) * t), 0, 255)) << shift;
    };
    return channel(16) | channel(8) | channel(0);
}

}  // namespace

// clang-format off
const SparkleFace::Params SparkleFace::kParams[kExpressionCount] = {
    //            eyes                                         color      smile frown arc  mouth w,h,dx  blush look
    /* Neutral */ {{kOpen, kOpen},                             kCyan,     255, 0,   14,  0, 0, 0,     180,  0, 0},
    /* Happy   */ {{kCrescent, kCrescent},                     kCyan,     255, 0,   18,  0, 0, 0,     230,  0, 0},
    /* Laugh   */ {{{21, 20, 0, 0, 0, 0, 12, 0},
                    {21, 20, 0, 0, 0, 0, 12, 0}},              kCyan,     0,   0,   18,  18, 11, 0,   230,  0, -1},
    /* Love    */ {{kGlyphEye, kGlyphEye},                     kPink,     255, 0,   16,  0, 0, 0,     255,  0, 0},
    /* Wink    */ {{kOpen, {21, 3, 0, 5, 0, 0, 0, 0}},         kCyan,     255, 0,   16,  0, 0, 0,     220,  1, 0},
    /* Surpris */ {{{23, 28, 0, -1, 0, 0, 0, 0},
                    {23, 28, 0, -1, 0, 0, 0, 0}},              kCyan,     0,   0,   14,  10, 12, 0,   60,   0, -1},
    /* Sad     */ {{{19, 20, 0, 2, 7, -18, 0, 0},
                    {19, 20, 0, 2, 7, -18, 0, 0}},             kCyan,     0,   255, 14,  0, 0, 0,     60,   0, 3},
    /* Crying  */ {{{19, 18, 0, 2, 9, -22, 0, 0},
                    {19, 18, 0, 2, 9, -22, 0, 0}},             kCyan,     0,   255, 16,  0, 0, 0,     110,  0, 3},
    /* Angry   */ {{{19, 20, 0, 1, 8, 22, 0, 0},
                    {19, 20, 0, 1, 8, 22, 0, 0}},              kAngryRed, 0,   255, 12,  0, 0, 0,     0,    0, 0},
    /* Sleepy  */ {{kClosed, kClosed},                         kCyan,     0,   0,   14,  6, 6, 0,     120,  0, 2},
    /* Dizzy   */ {{kGlyphEye, kGlyphEye},                     kCyan,     0,   0,   14,  10, 8, 0,    100,  0, 0},
    /* Shy     */ {{kCrescent, kCrescent},                     kCyan,     255, 0,   10,  0, 0, 0,     255,  -4, 3},
    /* Starry  */ {{kGlyphEye, kGlyphEye},                     kYellow,   255, 0,   18,  0, 0, 0,     210,  0, 0},
    /* Thinkin */ {{{19, 20, 0, 0, 4, 0, 0, 0},
                    {19, 20, 0, 0, 4, 0, 0, 0}},               kCyan,     0,   0,   14,  8, 3, 4,     80,   5, -4},
    /* Confuse */ {{kOpen, {15, 16, 0, 1, 3, 10, 0, 0}},       kCyan,     0,   0,   14,  10, 3, 3,    90,   2, -1},
    /* Kissy   */ {{kCrescent, kCrescent},                     kCyan,     0,   0,   14,  7, 7, 3,     255,  0, 0},
    /* Smug    */ {{{19, 24, 0, 0, 10, 0, 0, 0},
                    {19, 24, 0, 0, 10, 0, 0, 0}},              kCyan,     255, 0,   14,  0, 0, 4,     200,  2, 0},
    /* Petted  */ {{{21, 22, 0, 1, 0, 0, 13, 0},
                    {21, 22, 0, 1, 0, 0, 13, 0}},              kCyan,     255, 0,   18,  0, 0, 0,     255,  0, 0},
};

const SparkleFace::Behavior SparkleFace::kBehaviors[kExpressionCount] = {
    //            glyph         motion          blink  ring   pop                emit              ms
    /* Neutral */ {kGlyphNone,   kMotionBreathe, true,  false, kParticleNone,     kParticleNone,    0},
    /* Happy   */ {kGlyphNone,   kMotionHop,     false, false, kParticleNone,     kParticleNone,    0},
    /* Laugh   */ {kGlyphNone,   kMotionBounce,  false, false, kParticleNone,     kParticleNone,    0},
    /* Love    */ {kGlyphHeart,  kMotionBreathe, false, false, kParticleNone,     kParticleHeart,   520},
    /* Wink    */ {kGlyphNone,   kMotionHop,     false, false, kParticleStar,     kParticleNone,    0},
    /* Surpris */ {kGlyphNone,   kMotionHop,     true,  true,  kParticleExclaim,  kParticleNone,    0},
    /* Sad     */ {kGlyphNone,   kMotionBreathe, true,  false, kParticleNone,     kParticleTear,    1700},
    /* Crying  */ {kGlyphNone,   kMotionBreathe, false, false, kParticleNone,     kParticleTear,    420},
    /* Angry   */ {kGlyphNone,   kMotionShake,   true,  false, kParticleNone,     kParticleNone,    0},
    /* Sleepy  */ {kGlyphNone,   kMotionBreathe, false, true,  kParticleNone,     kParticleZ,       1300},
    /* Dizzy   */ {kGlyphSpiral, kMotionWobble,  false, true,  kParticleNone,     kParticleStar,    650},
    /* Shy     */ {kGlyphNone,   kMotionSway,    false, false, kParticleSweat,    kParticleNone,    0},
    /* Starry  */ {kGlyphStar,   kMotionBounce,  false, false, kParticleNone,     kParticleStar,    480},
    /* Thinkin */ {kGlyphNone,   kMotionStill,   true,  false, kParticleQuestion, kParticleNone,    0},
    /* Confuse */ {kGlyphNone,   kMotionSway,    true,  false, kParticleQuestion, kParticleNone,    0},
    /* Kissy   */ {kGlyphNone,   kMotionHop,     false, false, kParticleHeart,    kParticleNone,    0},
    /* Smug    */ {kGlyphNone,   kMotionBreathe, true,  false, kParticleNone,     kParticleNone,    0},
    /* Petted  */ {kGlyphNone,   kMotionSway,    false, false, kParticleNone,     kParticleHeart,   330},
};
// clang-format on

SparkleFace::SparkleFace(const Config& config) : config_(config) {
    lv_obj_t* screen = config_.screen;
    int32_t arc_width = std::max<int32_t>(2, S(3));

    for (auto& eye : eyes_) {
        eye.glow = MaskImage(screen, &sparkle_glow_eye_tall, kCyan);
        eye.box = Plain(screen);
        eye.body = Fill(eye.box, kCyan);
        eye.shine = Fill(eye.body, 0xFFFFFF);
        lv_obj_set_style_radius(eye.shine, S(3), 0);
        lv_obj_set_size(eye.shine, std::max<int32_t>(2, S(5)), std::max<int32_t>(2, S(6)));
        eye.lid_bottom = Fill(eye.box, kFaceScreen);
        eye.lid_top = Fill(eye.box, kFaceScreen);
        eye.glyph = MaskImage(screen, &sparkle_heart_24, kPink);
        lv_obj_set_style_radius(eye.lid_top, 0, 0);
        lv_obj_add_flag(eye.glyph, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(eye.lid_bottom, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(eye.lid_top, LV_OBJ_FLAG_HIDDEN);
    }
    smile_ = MouthArc(screen, 20, 160, arc_width);
    frown_ = MouthArc(screen, 200, 340, arc_width);
    mouth_ = Fill(screen, kPink);
    lv_obj_add_flag(mouth_, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 2; i++) {
        cheeks_[i] = Fill(config_.head, kPink);
        lv_obj_set_size(cheeks_[i], S(12), S(7));
        lv_obj_align(cheeks_[i], i == 0 ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_RIGHT,
                     i == 0 ? S(10) : -S(10), -S(28));
    }

    for (int i = 0; i < kImageSprites + kLabelSprites; i++) {
        Sprite& sprite = sprites_[i];
        sprite.label = i >= kImageSprites;
        if (sprite.label) {
            sprite.obj = lv_label_create(config_.fx_layer);
            lv_obj_set_style_text_font(
                sprite.obj, config_.scale > 200 ? &sparkle_font_title_20 : &sparkle_font_title_12,
                0);
        } else {
            sprite.obj = MaskImage(config_.fx_layer, &sparkle_heart_24, kPink);
        }
        lv_obj_remove_flag(sprite.obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(sprite.obj, LV_OBJ_FLAG_HIDDEN);
    }

    for (auto& cache : eye_cache_) {
        std::fill(std::begin(cache.v), std::end(cache.v), INT32_MIN);
    }
    std::fill(std::begin(mouth_cache_.v), std::end(mouth_cache_.v), INT32_MIN);

    from_ = to_ = now_ = kParams[kNeutral];
    morph_start_ = lv_tick_get();
    Update(morph_start_);
}

uint32_t SparkleFace::Random(uint32_t range) {
    rand_ ^= rand_ << 13;
    rand_ ^= rand_ >> 17;
    rand_ ^= rand_ << 5;
    return range == 0 ? 0 : rand_ % range;
}

SparkleFace::Expression SparkleFace::FromEmotion(const char* emotion) {
    static const struct {
        const char* name;
        Expression expression;
    } kMap[] = {
        {"neutral", kNeutral},
        {"happy", kHappy},
        {"laughing", kLaugh},
        {"funny", kLaugh},
        {"sad", kSad},
        {"angry", kAngry},
        {"crying", kCrying},
        {"loving", kLove},
        {"embarrassed", kShy},
        {"surprised", kSurprised},
        {"shocked", kSurprised},
        {"thinking", kThinking},
        {"winking", kWink},
        {"cool", kStarry},
        {"relaxed", kHappy},
        {"delicious", kLaugh},
        {"kissy", kKissy},
        {"confident", kSmug},
        {"sleepy", kSleepy},
        {"silly", kDizzy},
        {"confused", kConfused},
        // Alert icons
        {"warning", kSad},
        {"cancel", kSad},
        {"cloud_off", kSad},
    };
    if (emotion != nullptr) {
        for (const auto& item : kMap) {
            if (strcmp(item.name, emotion) == 0) {
                return item.expression;
            }
        }
    }
    return kNeutral;
}

// ---------------------------------------------------------------- expression control

void SparkleFace::StartMorph(Expression expression, uint32_t now_ms, uint32_t duration_ms) {
    const Behavior& next = kBehaviors[expression];
    if (expression == target_expr_ && started_) {
        return;
    }
    started_ = true;
    from_ = now_;
    to_ = kParams[expression];
    if (next.glyph != kGlyphNone && next.glyph != glyph_) {
        // A different glyph pops in from nothing instead of morphing
        for (auto& eye : from_.eye) {
            eye.glyph_mix = 0;
        }
        glyph_ = next.glyph;
    }
    if (kBehaviors[target_expr_].motion != next.motion) {
        motion_start_ = now_ms;
    }
    target_expr_ = expression;
    morph_start_ = now_ms;
    morph_ms_ = std::max<uint32_t>(1, duration_ms);
    pop_pending_ = true;
    last_emit_ = now_ms;
    // A blink hides the change of eye shape, like a cartoon cut
    if (next.blink && !looking_) {
        next_blink_ = now_ms;
    }
}

void SparkleFace::SetBase(Expression expression) {
    base_ = expression;
    if (!held_ && temp_until_ == 0) {
        StartMorph(expression, lv_tick_get(), kMorphMs);
    }
}

void SparkleFace::Play(Expression expression, uint32_t duration_ms) {
    uint32_t now = lv_tick_get();
    held_ = false;
    StartMorph(expression, now, kMorphMs);
    temp_until_ = now + std::max<uint32_t>(1, duration_ms);
}

void SparkleFace::Hold(Expression expression) {
    held_ = true;
    temp_until_ = 0;
    StartMorph(expression, lv_tick_get(), kMorphMs);
}

void SparkleFace::Release(uint32_t linger_ms) {
    if (!held_) {
        return;
    }
    held_ = false;
    uint32_t now = lv_tick_get();
    if (linger_ms > 0) {
        temp_until_ = now + linger_ms;
    } else {
        StartMorph(base_, now, kMorphMs);
    }
}

void SparkleFace::LookAt(int32_t x, int32_t y) {
    int32_t cx, cy;
    if (!FaceOrigin(cx, cy)) {
        return;
    }
    looking_ = true;
    // Face pixels per page pixel: eyes track a finger across the screen
    gaze_x_ = std::clamp((x - cx) / 14.0f, -6.0f, 6.0f);
    gaze_y_ = std::clamp((y - cy) / 16.0f, -4.0f, 4.0f);
}

void SparkleFace::ClearLook() {
    looking_ = false;
    gaze_x_ = gaze_y_ = 0;
    next_wander_ = lv_tick_get() + 900;
}

void SparkleFace::Boop() { boop_start_ = std::max<uint32_t>(1, lv_tick_get()); }

void SparkleFace::Burst(int count) {
    uint32_t now = lv_tick_get();
    for (int i = 0; i < count; i++) {
        Spawn(i % 2 == 0 ? kParticleHeart : kParticleStar, now);
    }
}

// ---------------------------------------------------------------- per frame

void SparkleFace::Update(uint32_t now_ms) {
    uint32_t dt = last_ms_ == 0 ? 16 : std::min<uint32_t>(now_ms - last_ms_, 100);
    last_ms_ = now_ms;
    (void)dt;

    if (temp_until_ != 0 && static_cast<int32_t>(now_ms - temp_until_) >= 0) {
        temp_until_ = 0;
        StartMorph(base_, now_ms, kMorphMs);
    }

    // Interpolate the shape parameters
    float t = Clamp01(static_cast<float>(now_ms - morph_start_) / morph_ms_);
    float e = EaseOutBack(t);
    for (int i = 0; i < 2; i++) {
        const EyeParams& a = from_.eye[i];
        const EyeParams& b = to_.eye[i];
        EyeParams& c = now_.eye[i];
        c.w = Lerp(a.w, b.w, e);
        c.h = Lerp(a.h, b.h, e);
        c.dx = Lerp(a.dx, b.dx, e);
        c.dy = Lerp(a.dy, b.dy, e);
        c.lid_top = std::max<int16_t>(0, Lerp(a.lid_top, b.lid_top, e));
        c.lid_tilt = Lerp(a.lid_tilt, b.lid_tilt, e);
        c.lid_bottom = std::max<int16_t>(0, Lerp(a.lid_bottom, b.lid_bottom, e));
        c.glyph_mix = std::clamp<int16_t>(Lerp(a.glyph_mix, b.glyph_mix, t), 0, 256);
    }
    now_.eye_color = MixColor(from_.eye_color, to_.eye_color, t);
    now_.smile = std::clamp<int16_t>(Lerp(from_.smile, to_.smile, t), 0, 255);
    now_.frown = std::clamp<int16_t>(Lerp(from_.frown, to_.frown, t), 0, 255);
    now_.arc_size = Lerp(from_.arc_size, to_.arc_size, e);
    now_.mouth_w = std::max<int16_t>(0, Lerp(from_.mouth_w, to_.mouth_w, e));
    now_.mouth_h = std::max<int16_t>(0, Lerp(from_.mouth_h, to_.mouth_h, e));
    now_.mouth_dx = Lerp(from_.mouth_dx, to_.mouth_dx, e);
    now_.blush = std::clamp<int16_t>(Lerp(from_.blush, to_.blush, t), 0, 255);
    now_.look_dx = Lerp(from_.look_dx, to_.look_dx, e);
    now_.look_dy = Lerp(from_.look_dy, to_.look_dy, e);
    if (t >= 1 && kBehaviors[target_expr_].glyph == kGlyphNone) {
        glyph_ = kGlyphNone;
    }

    int32_t openness = 256;
    UpdateBlink(now_ms, openness);
    UpdateGaze(now_ms);
    UpdateTalk(now_ms);
    for (int i = 0; i < 2; i++) {
        ApplyEye(i, now_.eye[i], openness);
    }
    ApplyMouth(now_ms);
    if (Changed(blush_cache_, now_.blush)) {
        for (auto cheek : cheeks_) {
            lv_obj_set_style_bg_opa(cheek, static_cast<lv_opa_t>(now_.blush), 0);
        }
    }
    ApplyMotion(now_ms);

    // Particles: a pop when the expression starts, then the emitter
    const Behavior& behavior = kBehaviors[target_expr_];
    if (pop_pending_ && now_ms - morph_start_ >= morph_ms_ / 2) {
        pop_pending_ = false;
        if (behavior.pop != kParticleNone) {
            Spawn(behavior.pop, now_ms);
        }
    }
    if (behavior.emit != kParticleNone && now_ms - last_emit_ >= behavior.emit_ms) {
        last_emit_ = now_ms;
        Spawn(behavior.emit, now_ms);
    }
    UpdateSprites(now_ms);
}

void SparkleFace::UpdateBlink(uint32_t now_ms, int32_t& openness) {
    const Behavior& behavior = kBehaviors[target_expr_];
    if (next_blink_ == 0) {
        next_blink_ = now_ms + 1500 + Random(2500);
    }
    if (blink_start_ == 0 && behavior.blink && static_cast<int32_t>(now_ms - next_blink_) >= 0) {
        blink_start_ = now_ms;
        double_blink_ = Random(100) < 18;
        next_blink_ = now_ms + 2400 + Random(3400);
    }
    if (blink_start_ == 0) {
        return;
    }
    uint32_t t = now_ms - blink_start_;
    if (double_blink_ && t >= 220) {
        t -= 220;
        if (t >= 170) {
            blink_start_ = 0;
            return;
        }
    } else if (!double_blink_ && t >= 170) {
        blink_start_ = 0;
        return;
    }
    if (t < 70) {
        openness = 256 - 236 * static_cast<int32_t>(t) / 70;
    } else if (t < 170) {
        openness = 20 + 236 * static_cast<int32_t>(t - 70) / 100;
    }
}

void SparkleFace::UpdateGaze(uint32_t now_ms) {
    if (!looking_ && wander_ && kBehaviors[target_expr_].blink) {
        if (static_cast<int32_t>(now_ms - next_wander_) >= 0) {
            next_wander_ = now_ms + 1300 + Random(2600);
            if (Random(100) < 40) {
                gaze_x_ = gaze_y_ = 0;
            } else {
                gaze_x_ = static_cast<float>(Random(11)) - 5;
                gaze_y_ = static_cast<float>(Random(7)) - 4;
            }
        }
    } else if (!looking_) {
        gaze_x_ = gaze_y_ = 0;
    }
    // Quick, critically damped glance
    look_x_ += (gaze_x_ - look_x_) * 0.28f;
    look_y_ += (gaze_y_ - look_y_) * 0.28f;
}

void SparkleFace::UpdateTalk(uint32_t now_ms) {
    talk_mix_ += ((talking_ ? 1.0f : 0.0f) - talk_mix_) * 0.2f;
    if (talk_mix_ < 0.01f) {
        talk_mix_ = 0;
        return;
    }
    if (static_cast<int32_t>(now_ms - next_syllable_) >= 0) {
        next_syllable_ = now_ms + 80 + Random(100);
        talk_target_ = Random(100) < 15 ? 0.05f : 0.25f + Random(75) / 100.0f;
    }
    talk_level_ += (talk_target_ - talk_level_) * 0.45f;
}

void SparkleFace::ApplyEye(int index, const EyeParams& eye, int32_t openness) {
    Eye& o = eyes_[index];
    int32_t* c = eye_cache_[index].v;
    float fx = kEyeCx[index] + eye.dx + look_x_ + now_.look_dx;
    float fy = kEyeCy + eye.dy + look_y_ + now_.look_dy;
    int32_t cx = lroundf(fx * config_.scale / 256);
    int32_t cy = lroundf(fy * config_.scale / 256);
    int32_t w = std::max<int32_t>(2, S(eye.w));
    int32_t full_h = std::max<int32_t>(2, S(eye.h));
    int32_t h = std::max<int32_t>(2, full_h * openness / 256);
    int32_t mix = glyph_ == kGlyphNone ? 0 : eye.glyph_mix;
    uint32_t color = now_.eye_color;

    // Body and its shine; the box tracks the body
    if (Changed(c[0], cx - w / 2) | Changed(c[1], cy - h / 2)) {
        lv_obj_set_pos(o.box, c[0], c[1]);
    }
    if (Changed(c[2], w) | Changed(c[3], h)) {
        lv_obj_set_size(o.box, w, h);
        lv_obj_set_size(o.body, w, h);
        lv_obj_set_pos(o.shine, w * 3 / 19, h * 4 / 24);
    }
    if (Changed(c[4], static_cast<int32_t>(color))) {
        lv_obj_set_style_bg_color(o.body, lv_color_hex(color), 0);
        lv_obj_set_style_image_recolor(o.glow, lv_color_hex(color), 0);
        lv_obj_set_style_image_recolor(o.glyph, lv_color_hex(color), 0);
    }
    int32_t body_opa = 255 - std::min<int32_t>(255, mix);
    if (Changed(c[5], body_opa)) {
        lv_obj_set_style_bg_opa(o.body, static_cast<lv_opa_t>(body_opa), 0);
        SetHidden(o.box, body_opa == 0);
    }
    bool shine = h * 10 >= full_h * 6 && full_h >= S(14) && eye.lid_bottom < 4 && mix < 128;
    SetHidden(o.shine, !shine);

    // Halo around the part of the eye the lids leave visible, faded for glyphs
    int32_t visible_top = std::clamp<int32_t>(S(eye.lid_top), 0, h);
    int32_t visible_bottom = std::clamp<int32_t>(h - S(eye.lid_bottom), visible_top, h);
    int32_t visible_h = std::max<int32_t>(2, visible_bottom - visible_top);
    int32_t glow_cy = cy - h / 2 + (visible_top + visible_bottom) / 2;
    int32_t sx = w * 256 / kEyeW;
    int32_t sy = visible_h * 256 / kEyeH;
    if (Changed(c[6], sx) | Changed(c[7], sy)) {
        lv_image_set_scale_x(o.glow, std::max<int32_t>(1, sx));
        lv_image_set_scale_y(o.glow, std::max<int32_t>(1, sy));
    }
    if (Changed(c[8], cx) | Changed(c[9], glow_cy)) {
        lv_obj_set_pos(o.glow, cx - kGlowW / 2, glow_cy - kGlowH / 2);
    }
    if (Changed(c[10], body_opa)) {
        lv_obj_set_style_image_opa(o.glow, static_cast<lv_opa_t>(body_opa), 0);
        SetHidden(o.glow, body_opa == 0);
    }

    // Glyph eyes (hearts, stars, spirals) pop in over the body
    bool glyph_visible = mix > 0;
    SetHidden(o.glyph, !glyph_visible);
    if (glyph_visible) {
        if (Changed(c[11], glyph_)) {
            const lv_image_dsc_t* src = glyph_ == kGlyphHeart  ? &sparkle_heart_24
                                        : glyph_ == kGlyphStar ? &sparkle_star_glow_16
                                                               : &sparkle_spiral_24;
            lv_image_set_src(o.glyph, src);
        }
        // Star mask carries its glow: the star itself is 16 of 24 px
        int32_t core = glyph_ == kGlyphStar ? 15 : 22;
        int32_t scale = w * 256 / core * (100 + 155 * mix / 256) / 255;
        if (Changed(c[20], scale)) {
            lv_image_set_scale(o.glyph, std::max<int32_t>(1, scale));
        }
        if (Changed(c[21], std::min<int32_t>(255, mix))) {
            lv_obj_set_style_image_opa(o.glyph, static_cast<lv_opa_t>(c[21]), 0);
        }
        lv_obj_set_pos(o.glyph, cx - kGlyphSize / 2, cy - kGlyphSize / 2);
        int32_t rotation =
            glyph_ == kGlyphSpiral ? static_cast<int32_t>(last_ms_ * 7 / 10 % 3600) : 0;
        if (Changed(c[22], rotation)) {
            lv_image_set_rotation(o.glyph, index == 0 ? rotation : (3600 - rotation) % 3600);
        }
    } else {
        c[11] = c[20] = c[21] = c[22] = INT32_MIN;
    }

    // Bottom lid: a screen-colored ellipse that turns the eye into a crescent
    int32_t lid_bottom = S(eye.lid_bottom);
    SetHidden(o.lid_bottom, lid_bottom <= 0 || mix >= 256);
    if (lid_bottom > 0) {
        int32_t lw = w + S(8);
        int32_t lh = full_h + S(4);
        if (Changed(c[12], (w - lw) / 2) | Changed(c[13], h - lid_bottom)) {
            lv_obj_set_pos(o.lid_bottom, c[12], c[13]);
        }
        if (Changed(c[14], lw) | Changed(c[15], lh)) {
            lv_obj_set_size(o.lid_bottom, lw, lh);
        }
    }

    // Top lid: a tilted cover for sad, angry, sleepy and smug eyes
    int32_t lid_top = S(eye.lid_top);
    SetHidden(o.lid_top, lid_top <= 0 || mix >= 256);
    if (lid_top > 0) {
        int32_t lw = w + S(12);
        int32_t lh = full_h + S(4);
        if (Changed(c[16], (w - lw) / 2) | Changed(c[17], lid_top - lh)) {
            lv_obj_set_pos(o.lid_top, c[16], c[17]);
        }
        if (Changed(c[18], lw * 10000 + lh)) {
            lv_obj_set_size(o.lid_top, lw, lh);
            lv_obj_set_style_transform_pivot_x(o.lid_top, lw / 2, 0);
            lv_obj_set_style_transform_pivot_y(o.lid_top, lh / 2, 0);
        }
        int32_t rotation = eye.lid_tilt * 10 * (index == 0 ? 1 : -1);
        if (Changed(c[19], rotation)) {
            lv_obj_set_style_transform_rotation(o.lid_top, rotation, 0);
        }
    }
}

void SparkleFace::ApplyMouth(uint32_t now_ms) {
    (void)now_ms;
    int32_t* c = mouth_cache_.v;
    float talk = talk_mix_;
    int32_t mx = S(kMouthX + now_.mouth_dx);

    // Smile / frown arcs fade out while talking
    int32_t smile = lroundf(now_.smile * (1 - talk));
    int32_t frown = lroundf(now_.frown * (1 - talk));
    int32_t size = std::max<int32_t>(4, S(now_.arc_size));
    if (Changed(c[0], smile)) {
        lv_obj_set_style_arc_opa(smile_, static_cast<lv_opa_t>(smile), LV_PART_MAIN);
        SetHidden(smile_, smile == 0);
    }
    if (Changed(c[1], frown)) {
        lv_obj_set_style_arc_opa(frown_, static_cast<lv_opa_t>(frown), LV_PART_MAIN);
        SetHidden(frown_, frown == 0);
    }
    if (Changed(c[2], size) | Changed(c[3], mx)) {
        lv_obj_set_size(smile_, size, size);
        lv_obj_set_pos(smile_, mx - size / 2, S(kMouthY + 2) - size);
        lv_obj_set_size(frown_, size, size);
        lv_obj_set_pos(frown_, mx - size / 2, S(kMouthY - 3));
    }

    // Open mouth: ring (surprise), filled (laugh) or talking
    float w = now_.mouth_w;
    float h = now_.mouth_h;
    if (talk > 0) {
        float talk_w = std::max(w, 13.0f);
        float talk_h = 3 + talk_level_ * 9;
        w += (talk_w - w) * talk;
        h += (talk_h - h) * talk;
    }
    int32_t mw = lroundf(w * config_.scale / 256);
    int32_t mh = lroundf(h * config_.scale / 256);
    bool visible = mw >= 2 && mh >= 2;
    SetHidden(mouth_, !visible);
    if (!visible) {
        return;
    }
    if (Changed(c[4], mw) | Changed(c[5], mh)) {
        lv_obj_set_size(mouth_, mw, mh);
    }
    if (Changed(c[6], mx - mw / 2) | Changed(c[7], S(kMouthY) - mh / 2)) {
        lv_obj_set_pos(mouth_, c[6], c[7]);
    }
    bool ring = kBehaviors[target_expr_].ring_mouth && talk < 0.5f;
    if (Changed(c[8], ring)) {
        lv_obj_set_style_bg_color(mouth_, lv_color_hex(ring ? kFaceScreen : kPink), 0);
        lv_obj_set_style_border_color(mouth_, lv_color_hex(kPink), 0);
        lv_obj_set_style_border_width(mouth_, ring ? std::max<int32_t>(2, S(2)) : 0, 0);
    }
}

void SparkleFace::ApplyMotion(uint32_t now_ms) {
    if (config_.motion == nullptr) {
        return;
    }
    float t = (now_ms - motion_start_) / 1000.0f;
    float k = config_.scale / 256.0f;
    float breathe = -config_.breathe_px * 0.5f *
                    (1 - cosf(2 * kPi * (now_ms % config_.breathe_ms) / config_.breathe_ms));
    float x = 0;
    float y = breathe;
    switch (kBehaviors[target_expr_].motion) {
        case kMotionBounce:
            y = -fabsf(sinf(2 * kPi * t / 0.84f)) * 5 * k;
            break;
        case kMotionShake:
            x = sinf(2 * kPi * t / 0.11f) * 2.5f * k * expf(-t / 0.7f);
            break;
        case kMotionWobble:
            x = cosf(2 * kPi * t / 1.1f) * 4 * k;
            y = sinf(2 * kPi * t / 1.1f) * 2.5f * k;
            break;
        case kMotionHop:
            if (t < 0.34f) {
                y = -7 * k * sinf(kPi * t / 0.34f);
            }
            break;
        case kMotionSway:
            x = sinf(2 * kPi * t / 2.4f) * 3 * k;
            break;
        case kMotionStill:
            y = 0;
            break;
        default:
            break;
    }
    // Boop: pressed in, then springs back with a little wobble
    if (boop_start_ != 0) {
        float b = (now_ms - boop_start_) / 1000.0f;
        if (b > 0.7f) {
            boop_start_ = 0;
        } else {
            y += 5 * k * expf(-b / 0.13f) * cosf(2 * kPi * b / 0.3f);
        }
    }
    int32_t ix = lroundf(x);
    int32_t iy = lroundf(y);
    if (Changed(motion_cache_[0], ix)) {
        lv_obj_set_style_translate_x(config_.motion, ix, 0);
    }
    if (Changed(motion_cache_[1], iy)) {
        lv_obj_set_style_translate_y(config_.motion, iy, 0);
    }
}

// ---------------------------------------------------------------- particles

bool SparkleFace::FaceOrigin(int32_t& x, int32_t& y) const {
    lv_area_t screen, layer;
    lv_obj_get_coords(config_.screen, &screen);
    lv_obj_get_coords(config_.fx_layer, &layer);
    if (lv_area_get_width(&screen) <= 1) {
        return false;
    }
    x = (screen.x1 + screen.x2) / 2 - layer.x1;
    y = (screen.y1 + screen.y2) / 2 - layer.y1;
    return true;
}

void SparkleFace::Spawn(Particle particle, uint32_t now_ms) {
    int32_t cx, cy;
    if (!FaceOrigin(cx, cy)) {
        return;
    }
    bool label =
        particle == kParticleZ || particle == kParticleQuestion || particle == kParticleExclaim;
    Sprite* sprite = nullptr;
    int first = label ? kImageSprites : 0;
    int last = label ? kImageSprites + kLabelSprites : kImageSprites;
    uint32_t oldest = UINT32_MAX;
    for (int i = first; i < last; i++) {
        if (!sprites_[i].active) {
            sprite = &sprites_[i];
            break;
        }
        // Pool full: recycle the oldest
        if (sprites_[i].start < oldest) {
            oldest = sprites_[i].start;
            sprite = &sprites_[i];
        }
    }
    if (sprite == nullptr) {
        return;
    }

    float k = config_.scale / 256.0f;
    float half_w = 47 * k;  // half the face screen
    float r = static_cast<float>(Random(1000)) / 1000.0f;
    Sprite& s = *sprite;
    s = Sprite{s.obj, s.label};
    s.active = true;
    s.start = now_ms;
    s.vx = s.vy = s.ay = s.sway = 0;
    s.scale0 = s.scale1 = 256;
    uint32_t color = kPink;
    const lv_image_dsc_t* src = &sparkle_heart_24;
    const char* text = nullptr;

    switch (particle) {
        case kParticleHeart:
            s.x = cx + (r - 0.5f) * half_w * 1.6f;
            s.y = cy - 44 * k;
            s.vx = (static_cast<float>(Random(25)) - 12) * k;
            s.vy = -40 * k;
            s.sway = 5 * k;
            s.life = 1500;
            s.scale0 = lroundf(100 * k);
            s.scale1 = lroundf(190 * k);
            break;
        case kParticleStar:
            src = &sparkle_star_16;
            color = Random(2) ? kYellow : kCyan;
            s.x = cx + (r - 0.5f) * half_w * 2.4f;
            s.y = cy - 30 * k;
            s.vx = (static_cast<float>(Random(41)) - 20) * k;
            s.vy = -46 * k;
            s.life = 1000;
            s.scale0 = lroundf(320 * k);
            s.scale1 = lroundf(120 * k);
            break;
        case kParticleTear:
            src = &sparkle_drop_10;
            color = kTearBlue;
            s.x = cx + (Random(2) ? 19 : -20) * k + look_x_ * k;
            s.y = cy + 4 * k;
            s.vy = 8 * k;
            s.ay = 170 * k;
            s.life = 900;
            s.scale0 = s.scale1 = lroundf(230 * k);
            break;
        case kParticleSweat:
            src = &sparkle_drop_10;
            color = kSweatBlue;
            s.x = cx + 44 * k;
            s.y = cy - 40 * k;
            s.vy = 10 * k;
            s.ay = 16 * k;
            s.life = 1700;
            s.scale0 = s.scale1 = lroundf(300 * k);
            break;
        case kParticleZ:
            text = Random(2) ? "Z" : "z";
            color = kPurple;
            s.x = cx + 34 * k;
            s.y = cy - 38 * k;
            s.vx = 14 * k;
            s.vy = -22 * k;
            s.sway = 3 * k;
            s.life = 2100;
            break;
        case kParticleQuestion:
            text = "?";
            color = kYellow;
            s.x = cx + 50 * k;
            s.y = cy - 50 * k;
            s.vy = -6 * k;
            s.life = 1500;
            break;
        case kParticleExclaim:
            text = "!";
            color = kPink;
            s.x = cx + 50 * k;
            s.y = cy - 50 * k;
            s.vy = -10 * k;
            s.life = 1100;
            break;
        default:
            s.active = false;
            return;
    }

    if (s.label) {
        lv_label_set_text_static(s.obj, text);
        lv_obj_set_style_text_color(s.obj, lv_color_hex(color), 0);
    } else {
        lv_image_set_src(s.obj, src);
        lv_obj_set_style_image_recolor(s.obj, lv_color_hex(color), 0);
        lv_image_set_scale(s.obj, std::max<int32_t>(1, s.scale0));
    }
    lv_obj_set_style_opa(s.obj, LV_OPA_TRANSP, 0);
    lv_obj_move_foreground(s.obj);
    lv_obj_remove_flag(s.obj, LV_OBJ_FLAG_HIDDEN);
}

void SparkleFace::UpdateSprites(uint32_t now_ms) {
    for (auto& s : sprites_) {
        if (!s.active) {
            continue;
        }
        uint32_t age = now_ms - s.start;
        if (age >= s.life) {
            s.active = false;
            lv_obj_add_flag(s.obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        float t = age / 1000.0f;
        float p = static_cast<float>(age) / s.life;
        float x = s.x + s.vx * t + s.sway * sinf(2 * kPi * t / 0.9f);
        float y = s.y + s.vy * t + 0.5f * s.ay * t * t;
        int32_t opa = p < 0.12f ? lroundf(255 * p / 0.12f)
                                : (p > 0.65f ? lroundf(255 * (1 - p) / 0.35f) : 255);
        lv_obj_set_style_opa(s.obj, static_cast<lv_opa_t>(std::clamp<int32_t>(opa, 0, 255)), 0);
        if (s.label) {
            lv_obj_set_pos(s.obj, lroundf(x) - 5, lroundf(y) - 10);
        } else {
            const auto* src = static_cast<const lv_image_dsc_t*>(lv_image_get_src(s.obj));
            lv_obj_set_pos(s.obj, lroundf(x) - src->header.w / 2, lroundf(y) - src->header.h / 2);
            if (s.scale0 != s.scale1) {
                lv_image_set_scale(s.obj, s.scale0 + lroundf((s.scale1 - s.scale0) * p));
            }
        }
    }
}
