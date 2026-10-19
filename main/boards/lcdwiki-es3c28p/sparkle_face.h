#ifndef SPARKLE_FACE_H
#define SPARKLE_FACE_H

#include <lvgl.h>

#include <cstdint>

// Expressive robot face for the Sparkle Bot UI. It draws the eyes, mouth and
// blush inside a page's dark face screen and morphs smoothly between
// expressions: every shape parameter is interpolated, so a change of mood is
// an animation rather than a cut. Blinking, gaze, talking, whole-face motion
// and particles (hearts, tears, "z"...) are procedural and advanced by
// Update(), which the display calls once per frame for the visible face only.
//
// Geometry is authored for the 94x76 idle face screen; `scale` (256 = 1.0)
// resizes it for smaller faces.
class SparkleFace {
public:
    enum Expression : uint8_t {
        kNeutral,
        kHappy,
        kLaugh,
        kLove,
        kWink,
        kSurprised,
        kSad,
        kCrying,
        kAngry,
        kSleepy,
        kDizzy,
        kShy,
        kStarry,
        kThinking,
        kConfused,
        kKissy,
        kSmug,
        kPetted,
        kExpressionCount
    };

    struct Config {
        lv_obj_t* screen = nullptr;    // dark face screen: eyes and mouth
        lv_obj_t* head = nullptr;      // blush sits on the head, below the screen
        lv_obj_t* motion = nullptr;    // object translated for bob / bounce / shake
        lv_obj_t* fx_layer = nullptr;  // full-page layer for particles
        int32_t scale = 256;
        int32_t breathe_px = 4;     // idle bob amplitude
        int32_t breathe_ms = 3200;  // idle bob period
    };

    explicit SparkleFace(const Config& config);

    // The resting expression (server emotion or neutral)
    void SetBase(Expression expression);
    Expression base() const { return base_; }
    // A reaction shown for duration_ms, then back to the base expression
    void Play(Expression expression, uint32_t duration_ms);
    // Hold an expression until Release() (petting, sleeping)
    void Hold(Expression expression);
    void Release(uint32_t linger_ms = 0);
    Expression current() const { return target_expr_; }

    // Gaze target in page coordinates, or idle wandering when cleared
    void LookAt(int32_t x, int32_t y);
    void ClearLook();
    void SetWander(bool wander) { wander_ = wander; }
    void SetTalking(bool talking) { talking_ = talking; }
    // Squish, as feedback for a tap
    void Boop();
    // Particle burst around the head (tap feedback on any page)
    void Burst(int count);

    void Update(uint32_t now_ms);

    // Mapping from the server's emotion names ("happy", "loving"...)
    static Expression FromEmotion(const char* emotion);

    struct EyeParams {
        int16_t w, h, dx, dy;
        int16_t lid_top, lid_tilt, lid_bottom;
        int16_t glyph_mix;
    };
    struct Params {
        EyeParams eye[2];
        uint32_t eye_color;
        int16_t smile, frown, arc_size;
        int16_t mouth_w, mouth_h, mouth_dx;
        int16_t blush;
        int16_t look_dx, look_dy;
    };

private:
    enum Glyph : uint8_t { kGlyphNone, kGlyphHeart, kGlyphStar, kGlyphSpiral };
    enum Motion : uint8_t {
        kMotionBreathe,
        kMotionBounce,
        kMotionShake,
        kMotionWobble,
        kMotionHop,
        kMotionSway,
        kMotionStill
    };
    enum Particle : uint8_t {
        kParticleNone,
        kParticleHeart,
        kParticleStar,
        kParticleTear,
        kParticleSweat,
        kParticleZ,
        kParticleQuestion,
        kParticleExclaim
    };
    struct Behavior {
        Glyph glyph;
        Motion motion;
        bool blink;
        bool ring_mouth;
        Particle pop;   // once, when the expression starts
        Particle emit;  // repeatedly while it lasts
        uint16_t emit_ms;
    };

    struct Eye {
        lv_obj_t* glow;
        lv_obj_t* box;  // eye bounds: clips the lids so they never spill onto the head
        lv_obj_t* body;
        lv_obj_t* shine;
        lv_obj_t* glyph;
        lv_obj_t* lid_top;
        lv_obj_t* lid_bottom;
    };
    struct Sprite {
        lv_obj_t* obj = nullptr;
        bool label = false;
        uint32_t start = 0;
        uint16_t life = 0;
        float x = 0, y = 0, vx = 0, vy = 0, ay = 0, sway = 0;
        int16_t scale0 = 256, scale1 = 256;
        bool active = false;
    };
    static constexpr int kImageSprites = 10;
    static constexpr int kLabelSprites = 3;

    static const Params kParams[kExpressionCount];
    static const Behavior kBehaviors[kExpressionCount];

    Config config_;
    Eye eyes_[2] = {};
    lv_obj_t* smile_ = nullptr;
    lv_obj_t* frown_ = nullptr;
    lv_obj_t* mouth_ = nullptr;
    lv_obj_t* cheeks_[2] = {};
    Sprite sprites_[kImageSprites + kLabelSprites];

    Expression base_ = kNeutral;
    Expression target_expr_ = kNeutral;
    Params from_{};
    Params to_{};
    Params now_{};
    Glyph glyph_ = kGlyphNone;
    uint32_t morph_start_ = 0;
    uint32_t morph_ms_ = 1;
    uint32_t temp_until_ = 0;  // 0: showing the base or a held expression
    bool held_ = false;
    bool started_ = false;
    bool pop_pending_ = false;
    uint32_t last_emit_ = 0;

    // Procedural state
    uint32_t last_ms_ = 0;
    uint32_t next_blink_ = 0;
    uint32_t blink_start_ = 0;
    bool double_blink_ = false;
    bool looking_ = false;
    bool wander_ = true;
    float look_x_ = 0, look_y_ = 0;  // current, in face pixels
    float gaze_x_ = 0, gaze_y_ = 0;  // target
    uint32_t next_wander_ = 0;
    bool talking_ = false;
    float talk_mix_ = 0;
    float talk_level_ = 0, talk_target_ = 0;
    uint32_t next_syllable_ = 0;
    uint32_t boop_start_ = 0;
    uint32_t motion_start_ = 0;
    uint32_t rand_ = 0x9E3779B9;

    // Last values pushed to LVGL, so unchanged frames cost nothing
    struct Cache {
        int32_t v[24];
    };
    Cache eye_cache_[2] = {};
    Cache mouth_cache_ = {};
    int32_t motion_cache_[2] = {INT32_MIN, INT32_MIN};
    int32_t blush_cache_ = -1;

    int32_t S(int32_t v) const { return (v * config_.scale + (v >= 0 ? 128 : -128)) / 256; }
    uint32_t Random(uint32_t range);
    void StartMorph(Expression expression, uint32_t now_ms, uint32_t duration_ms);
    void ApplyEye(int index, const EyeParams& eye, int32_t openness);
    void ApplyMouth(uint32_t now_ms);
    void ApplyMotion(uint32_t now_ms);
    void UpdateBlink(uint32_t now_ms, int32_t& openness);
    void UpdateGaze(uint32_t now_ms);
    void UpdateTalk(uint32_t now_ms);
    void Spawn(Particle particle, uint32_t now_ms);
    void UpdateSprites(uint32_t now_ms);
    bool FaceOrigin(int32_t& x, int32_t& y) const;
};

#endif  // SPARKLE_FACE_H
