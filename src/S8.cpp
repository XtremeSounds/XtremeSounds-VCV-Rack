// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Chris DuPont / Xtreme Sounds

#include "plugin.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <limits>

namespace {

constexpr int kSteps = 8;
constexpr int kRoots = 12;

const std::array<const char*, kRoots> kRootNames = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

struct ScaleDef {
    const char* name;
    std::vector<int> notes;
};

const std::vector<ScaleDef> kScales = {
    {"Major",          {0, 2, 4, 5, 7, 9, 11}},
    {"Natural Minor",  {0, 2, 3, 5, 7, 8, 10}},
    {"Dorian",         {0, 2, 3, 5, 7, 9, 10}},
    {"Phrygian",       {0, 1, 3, 5, 7, 8, 10}},
    {"Lydian",         {0, 2, 4, 6, 7, 9, 11}},
    {"Mixolydian",     {0, 2, 4, 5, 7, 9, 10}},
    {"Locrian",        {0, 1, 3, 5, 6, 8, 10}},
    {"Harmonic Minor", {0, 2, 3, 5, 7, 8, 11}},
    {"Melodic Minor",  {0, 2, 3, 5, 7, 9, 11}},
    {"Major Pent",     {0, 2, 4, 7, 9}},
    {"Minor Pent",     {0, 3, 5, 7, 10}},
    {"Blues",          {0, 3, 5, 6, 7, 10}},
    {"Whole Tone",     {0, 2, 4, 6, 8, 10}},
    {"Dim W-H",        {0, 2, 3, 5, 6, 8, 9, 11}},
    {"Dim H-W",        {0, 1, 3, 4, 6, 7, 9, 10}},
    {"Chromatic",      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {"Major Triad",    {0, 4, 7}},
    {"Minor Triad",    {0, 3, 7}},
    {"Dim Triad",      {0, 3, 6}},
    {"Aug Triad",      {0, 4, 8}},
    {"Maj7",           {0, 4, 7, 11}},
    {"Min7",           {0, 3, 7, 10}},
    {"Dom7",           {0, 4, 7, 10}},
    {"Sus4",           {0, 5, 7}}
};

constexpr int SCALE_MAJOR = 0;
constexpr int SCALE_MINOR = 1;
constexpr int SCALE_MAJOR_TRIAD = 16;
constexpr int SCALE_MINOR_TRIAD = 17;
constexpr int SCALE_DIM_TRIAD = 18;

constexpr int SCALE_DORIAN = 2;
constexpr int SCALE_PHRYGIAN = 3;
constexpr int SCALE_LYDIAN = 4;
constexpr int SCALE_MIXOLYDIAN = 5;
constexpr int SCALE_LOCRIAN = 6;
constexpr int SCALE_HARMONIC_MINOR = 7;
constexpr int SCALE_MELODIC_MINOR = 8;
constexpr int SCALE_MAJOR_PENT = 9;
constexpr int SCALE_MINOR_PENT = 10;
constexpr int SCALE_BLUES = 11;

struct HarmonySuggestion {
    int root;
    int scale;
    float score;
    std::string reason;

    HarmonySuggestion(int r, int s, float sc, const std::string& why)
        : root(r), scale(s), score(sc), reason(why) {}
};

int clampInt(int value, int low, int high) {
    return std::max(low, std::min(value, high));
}

const char* harmonyModeName(int mode) {
    static const char* names[] = {"Safe / Diatonic", "Modern", "Jazz / Advanced", "Ambient / Modal", "Adventurous"};
    return names[clampInt(mode, 0, 4)];
}

int wrap12(int note) {
    note %= 12;
    return note < 0 ? note + 12 : note;
}


} // namespace

struct S8 : Module {
    enum ParamIds {
        OFFSET_PARAM,
        PATTERN_PARAM,
        LENGTH_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        CLOCK_INPUT,
        RESET_INPUT,
        PITCH_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        PITCH_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        STEP1_LIGHT,
        STEP2_LIGHT,
        STEP3_LIGHT,
        STEP4_LIGHT,
        STEP5_LIGHT,
        STEP6_LIGHT,
        STEP7_LIGHT,
        STEP8_LIGHT,
        NUM_LIGHTS
    };

    std::array<int, kSteps> presetRoots{};
    std::array<int, kSteps> presetScales{};
    int activeStep = 0;
    int panelTheme = 0;
    int harmonyMode = 1;
    bool autoFollowHome = false;

    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;

    S8() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configParam(OFFSET_PARAM, -12.f, 12.f, 0.f, "Pitch offset", " semitones");
        getParamQuantity(OFFSET_PARAM)->snapEnabled = true;

        std::vector<std::string> stepLabels;
        for (int i = 0; i < kSteps; ++i)
            stepLabels.emplace_back(string::f("Pattern %d", i + 1));
        configSwitch(PATTERN_PARAM, 0.f, 7.f, 0.f, "Root/scale pattern", stepLabels);

        std::vector<std::string> lengthLabels;
        for (int i = 1; i <= kSteps; ++i)
            lengthLabels.emplace_back(string::f("%d step%s", i, i == 1 ? "" : "s"));
        configSwitch(LENGTH_PARAM, 1.f, 8.f, 8.f, "Clock step limit", lengthLabels);

        configInput(CLOCK_INPUT, "Pattern sequencer clock");
        configInput(RESET_INPUT, "Pattern sequencer reset");
        configInput(PITCH_INPUT, "V/Oct pitch");
        configOutput(PITCH_OUTPUT, "Quantized V/Oct pitch");
        configBypass(PITCH_INPUT, PITCH_OUTPUT);

        resetFactoryState();
    }

    void resetFactoryState() {
        // Full-scale defaults: a useful I-V-vi-IV flavored bank while keeping S8 a scale quantizer.
        presetRoots = {0, 7, 9, 5, 0, 5, 2, 7};
        presetScales = {SCALE_MAJOR, SCALE_MAJOR, SCALE_MINOR, SCALE_MAJOR,
                        SCALE_MAJOR, SCALE_MAJOR, SCALE_MINOR, SCALE_MAJOR};
        activeStep = 0;
        harmonyMode = 1;
        autoFollowHome = false;
        panelTheme = 0;
        params[OFFSET_PARAM].setValue(0.f);
        params[PATTERN_PARAM].setValue(0.f);
        params[LENGTH_PARAM].setValue(8.f);
    }

    void onReset() override {
        resetFactoryState();
    }

    void selectStep(int step, bool syncKnob = true) {
        activeStep = clampInt(step, 0, kSteps - 1);
        if (syncKnob)
            params[PATTERN_PARAM].setValue((float) activeStep);
    }

    static int circleDistance(int a, int b) {
        // Circle of fifths distance in 0..6 steps.
        int best = 6;
        int pc = a;
        for (int i = 0; i <= 6; ++i) {
            if (pc == b)
                best = std::min(best, i);
            pc = wrap12(pc + 7);
        }
        pc = a;
        for (int i = 0; i <= 6; ++i) {
            if (pc == b)
                best = std::min(best, i);
            pc = wrap12(pc - 7);
        }
        return best;
    }

    static std::array<bool, 12> pitchSet(int root, int scale) {
        std::array<bool, 12> pcs{};
        const auto& def = kScales[clampInt(scale, 0, (int) kScales.size() - 1)];
        for (int n : def.notes)
            pcs[wrap12(root + n)] = true;
        return pcs;
    }

    static int commonToneCount(int rootA, int scaleA, int rootB, int scaleB) {
        const auto a = pitchSet(rootA, scaleA);
        const auto b = pitchSet(rootB, scaleB);
        int common = 0;
        for (int i = 0; i < 12; ++i)
            common += (a[i] && b[i]) ? 1 : 0;
        return common;
    }

    bool homeIsMinor() const {
        const int s = presetScales[0];
        return s == SCALE_MINOR || s == SCALE_DORIAN || s == SCALE_PHRYGIAN ||
               s == SCALE_HARMONIC_MINOR || s == SCALE_MELODIC_MINOR ||
               s == SCALE_MINOR_PENT || s == SCALE_BLUES || s == SCALE_MINOR_TRIAD || s == 21;
    }

    std::vector<int> candidateScales() const {
        switch (harmonyMode) {
            case 0: return {SCALE_MAJOR, SCALE_MINOR, SCALE_DORIAN, SCALE_LYDIAN, SCALE_MIXOLYDIAN};
            case 1: return {SCALE_MAJOR, SCALE_MINOR, SCALE_DORIAN, SCALE_LYDIAN, SCALE_MIXOLYDIAN,
                            SCALE_MAJOR_PENT, SCALE_MINOR_PENT, SCALE_BLUES};
            case 2: return {SCALE_MAJOR, SCALE_MINOR, SCALE_DORIAN, SCALE_PHRYGIAN, SCALE_LYDIAN,
                            SCALE_MIXOLYDIAN, SCALE_HARMONIC_MINOR, SCALE_MELODIC_MINOR};
            case 3: return {SCALE_MAJOR, SCALE_MINOR, SCALE_DORIAN, SCALE_PHRYGIAN, SCALE_LYDIAN,
                            SCALE_MIXOLYDIAN, SCALE_MAJOR_PENT, SCALE_MINOR_PENT};
            default: return {SCALE_MAJOR, SCALE_MINOR, SCALE_DORIAN, SCALE_PHRYGIAN, SCALE_LYDIAN,
                             SCALE_MIXOLYDIAN, SCALE_HARMONIC_MINOR, SCALE_MELODIC_MINOR,
                             SCALE_MAJOR_PENT, SCALE_MINOR_PENT, SCALE_BLUES};
        }
    }

    float functionalScore(int homeRoot, bool minorHome, int candidateRoot, int candidateScale, std::string& reason) const {
        const int rel = wrap12(candidateRoot - homeRoot);
        float score = 0.f;
        auto add = [&](int interval, float points, const char* why) {
            if (rel == interval && points > score) {
                score = points;
                reason = why;
            }
        };

        if (!minorHome) {
            add(0, 22.f, "tonic / parallel color");
            add(7, 28.f, "dominant relationship");
            add(5, 25.f, "subdominant relationship");
            add(9, 24.f, "relative-minor area");
            add(2, 20.f, "supertonic / predominant");
            add(4, 15.f, "mediant relationship");
            add(10, harmonyMode >= 1 ? 20.f : 8.f, "borrowed bVII color");
            add(8, harmonyMode >= 1 ? 18.f : 5.f, "borrowed bVI color");
            add(3, harmonyMode >= 4 ? 19.f : 4.f, "chromatic-mediant color");
        }
        else {
            add(0, 24.f, "tonic / parallel color");
            add(7, 29.f, "dominant relationship");
            add(5, 24.f, "subdominant relationship");
            add(3, 25.f, "relative-major area");
            add(8, 22.f, "bVI relationship");
            add(10, 22.f, "bVII relationship");
            add(2, 15.f, "supertonic color");
            add(4, harmonyMode >= 4 ? 18.f : 4.f, "chromatic-mediant color");
        }

        // Scale-color preferences per mode.
        if (harmonyMode == 0) {
            if (candidateScale == SCALE_MAJOR || candidateScale == SCALE_MINOR)
                score += 8.f;
        }
        else if (harmonyMode == 1) {
            if (candidateScale == SCALE_DORIAN || candidateScale == SCALE_MIXOLYDIAN ||
                candidateScale == SCALE_MAJOR_PENT || candidateScale == SCALE_MINOR_PENT)
                score += 7.f;
        }
        else if (harmonyMode == 2) {
            if (candidateScale == SCALE_HARMONIC_MINOR || candidateScale == SCALE_MELODIC_MINOR ||
                candidateScale == SCALE_DORIAN || candidateScale == SCALE_MIXOLYDIAN)
                score += 9.f;
        }
        else if (harmonyMode == 3) {
            if (candidateScale == SCALE_DORIAN || candidateScale == SCALE_LYDIAN ||
                candidateScale == SCALE_MIXOLYDIAN || candidateScale == SCALE_MAJOR_PENT ||
                candidateScale == SCALE_MINOR_PENT)
                score += 10.f;
        }
        else {
            if (candidateScale == SCALE_PHRYGIAN || candidateScale == SCALE_HARMONIC_MINOR ||
                candidateScale == SCALE_MELODIC_MINOR || candidateScale == SCALE_BLUES)
                score += 8.f;
        }
        return score;
    }

    std::vector<HarmonySuggestion> recommendationsForSlot(int slot, int limit = 6) const {
        std::vector<HarmonySuggestion> out;
        slot = clampInt(slot, 1, kSteps - 1);

        const int homeRoot = presetRoots[0];
        const int homeScale = presetScales[0];
        const bool minorHome = homeIsMinor();
        const int prevRoot = presetRoots[slot - 1];
        const int prevScale = presetScales[slot - 1];
        const int prevPrevRoot = slot > 1 ? presetRoots[slot - 2] : homeRoot;

        for (int root = 0; root < kRoots; ++root) {
            for (int scale : candidateScales()) {
                if (root == prevRoot && scale == prevScale)
                    continue;

                std::string reason;
                float score = functionalScore(homeRoot, minorHome, root, scale, reason);

                const int commonPrev = commonToneCount(prevRoot, prevScale, root, scale);
                const int commonHome = commonToneCount(homeRoot, homeScale, root, scale);
                score += std::min(commonPrev, 6) * 4.2f;
                score += std::min(commonHome, 6) * 2.2f;

                const int circlePrev = circleDistance(prevRoot, root);
                score += (6 - circlePrev) * 2.1f;
                if (circlePrev <= 1 && reason.empty())
                    reason = "smooth circle-of-fifths move";

                // Prefer motion over bouncing straight back to the root used two slots ago.
                if (root == prevPrevRoot)
                    score -= 5.f;

                // Strong classical resolutions and predominant -> dominant motion.
                const int prevToCandidate = wrap12(root - prevRoot);
                const int prevRelHome = wrap12(prevRoot - homeRoot);
                const int candidateRelHome = wrap12(root - homeRoot);
                if (prevRelHome == 7 && candidateRelHome == 0) {
                    score += 18.f;
                    reason = "dominant -> tonic resolution";
                }
                if ((prevRelHome == 2 || prevRelHome == 5) && candidateRelHome == 7) {
                    score += 14.f;
                    reason = "predominant -> dominant motion";
                }
                if (prevToCandidate == 7 || prevToCandidate == 5)
                    score += 5.f;

                // Slot 8 is evaluated as a turnaround into slot 1 because the sequencer loops.
                if (slot == kSteps - 1) {
                    const int toHome = wrap12(homeRoot - root);
                    if (toHome == 5 || toHome == 7) {
                        score += 14.f;
                        reason = "strong loop turnaround to home";
                    }
                    score += commonToneCount(root, scale, homeRoot, homeScale) * 1.8f;
                }

                // Penalize remote changes unless the user explicitly asks for them.
                if (harmonyMode < 4 && circlePrev >= 4)
                    score -= 9.f;
                if (harmonyMode == 0 && commonPrev < 4)
                    score -= 10.f;

                // In adventurous mode, reward a controlled amount of harmonic distance.
                if (harmonyMode == 4 && circlePrev >= 2 && circlePrev <= 4)
                    score += 7.f;

                if (reason.empty())
                    reason = commonPrev >= 5 ? "common-tone voice leading" : "contrasting harmonic color";

                out.emplace_back(root, scale, score, reason);
            }
        }

        std::sort(out.begin(), out.end(), [](const HarmonySuggestion& a, const HarmonySuggestion& b) {
            if (std::fabs(a.score - b.score) > 0.001f)
                return a.score > b.score;
            if (a.root != b.root)
                return a.root < b.root;
            return a.scale < b.scale;
        });

        // Remove duplicate pitch collections so the menu recommends genuinely different quantizer sets.
        std::vector<HarmonySuggestion> unique;
        for (const auto& s : out) {
            const auto pcs = pitchSet(s.root, s.scale);
            bool duplicate = false;
            for (const auto& kept : unique) {
                if (pcs == pitchSet(kept.root, kept.scale)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                unique.push_back(s);
            if ((int) unique.size() >= limit)
                break;
        }
        return unique;
    }

    void applyBestRecommendation(int slot) {
        if (slot <= 0 || slot >= kSteps)
            return;
        const auto recs = recommendationsForSlot(slot, 1);
        if (!recs.empty()) {
            presetRoots[slot] = recs[0].root;
            presetScales[slot] = recs[0].scale;
        }
    }

    void intelligentAutoFill() {
        // Slot 1 is the user's HOME scale. Every next slot is generated from all choices before it.
        for (int slot = 1; slot < kSteps; ++slot) {
            const auto recs = recommendationsForSlot(slot, 4);
            if (recs.empty())
                continue;
            // Weighted choice among the strongest options keeps repeated fills musical but not identical.
            const uint32_t r = random::u32() % 100u;
            int pick = 0;
            if (recs.size() > 1 && r >= 58u) pick = 1;
            if (recs.size() > 2 && r >= 82u) pick = 2;
            if (recs.size() > 3 && r >= 95u) pick = 3;
            presetRoots[slot] = recs[pick].root;
            presetScales[slot] = recs[pick].scale;
        }
    }

    float quantize(float volts) {
        const int root = presetRoots[activeStep];
        const int scaleIndex = presetScales[activeStep];
        const auto& notes = kScales[scaleIndex].notes;
        const float semitone = volts * 12.f;
        const int octave = (int) std::floor(semitone / 12.f);

        float best = 0.f;
        float bestDistance = 1e9f;
        for (int octaveOffset = -1; octaveOffset <= 1; ++octaveOffset) {
            const int octaveBase = (octave + octaveOffset) * 12;
            for (int interval : notes) {
                const float candidate = (float) (octaveBase + root + interval);
                const float distance = std::fabs(candidate - semitone);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = candidate;
                }
            }
        }

        const float offsetSemitones = std::round(params[OFFSET_PARAM].getValue());
        return (best + offsetSemitones) / 12.f;
    }

    void process(const ProcessArgs& args) override {
        const int sequenceLength = clampInt((int) std::round(params[LENGTH_PARAM].getValue()), 1, kSteps);

        // STEP LIMIT applies only to clocked sequencing. RESET always returns to
        // Pattern 1. If CLOCK and RESET arrive on the same sample, RESET wins.
        const bool resetEdge = resetTrigger.process(inputs[RESET_INPUT].getVoltage());
        const bool clockEdge = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage());
        if (resetEdge) {
            selectStep(0, true);
        }
        else if (clockEdge) {
            const int nextStep = (activeStep < sequenceLength - 1) ? activeStep + 1 : 0;
            selectStep(nextStep, true);
        }

        const int knobStep = clampInt((int) std::round(params[PATTERN_PARAM].getValue()), 0, kSteps - 1);
        if (knobStep != activeStep)
            selectStep(knobStep, false);

        for (int i = 0; i < kSteps; ++i)
            lights[STEP1_LIGHT + i].setBrightness(i == activeStep ? 1.f : 0.05f);

        const int channels = inputs[PITCH_INPUT].getChannels();
        if (channels <= 0) {
            outputs[PITCH_OUTPUT].setChannels(1);
            outputs[PITCH_OUTPUT].setVoltage(0.f, 0);
            return;
        }

        outputs[PITCH_OUTPUT].setChannels(channels);
        for (int c = 0; c < channels; ++c) {
            const float in = inputs[PITCH_INPUT].getPolyVoltage(c);
            outputs[PITCH_OUTPUT].setVoltage(quantize(in), c);
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_t* rootsJ = json_array();
        json_t* scalesJ = json_array();
        for (int i = 0; i < kSteps; ++i) {
            json_array_append_new(rootsJ, json_integer(presetRoots[i]));
            json_array_append_new(scalesJ, json_integer(presetScales[i]));
        }
        json_object_set_new(rootJ, "presetRoots", rootsJ);
        json_object_set_new(rootJ, "presetScales", scalesJ);
        json_object_set_new(rootJ, "activeStep", json_integer(activeStep));
        json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
        json_object_set_new(rootJ, "harmonyMode", json_integer(harmonyMode));
        json_object_set_new(rootJ, "autoFollowHome", json_boolean(autoFollowHome));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* rootsJ = json_object_get(rootJ, "presetRoots");
        json_t* scalesJ = json_object_get(rootJ, "presetScales");
        if (json_is_array(rootsJ) && json_array_size(rootsJ) >= kSteps) {
            for (int i = 0; i < kSteps; ++i)
                presetRoots[i] = clampInt((int) json_integer_value(json_array_get(rootsJ, i)), 0, 11);
        }
        if (json_is_array(scalesJ) && json_array_size(scalesJ) >= kSteps) {
            for (int i = 0; i < kSteps; ++i)
                presetScales[i] = clampInt((int) json_integer_value(json_array_get(scalesJ, i)), 0, (int) kScales.size() - 1);
        }
        json_t* stepJ = json_object_get(rootJ, "activeStep");
        if (stepJ)
            activeStep = clampInt((int) json_integer_value(stepJ), 0, kSteps - 1);
        json_t* themeJ = json_object_get(rootJ, "panelTheme");
        if (themeJ)
            panelTheme = clampInt((int) json_integer_value(themeJ), 0, 3);
        json_t* harmonyJ = json_object_get(rootJ, "harmonyMode");
        if (harmonyJ)
            harmonyMode = clampInt((int) json_integer_value(harmonyJ), 0, 4);
        json_t* autoFollowJ = json_object_get(rootJ, "autoFollowHome");
        if (autoFollowJ)
            autoFollowHome = json_is_true(autoFollowJ);
        params[PATTERN_PARAM].setValue((float) activeStep);
    }
};


static void drawPluginImage(const widget::Widget::DrawArgs& args, const std::string& path, float x, float y, float w, float h, float alpha = 1.f) {
    std::shared_ptr<window::Image> image = APP->window->loadImage(asset::plugin(pluginInstance, path));
    if (!image || image->handle <= 0)
        return;
    nvgBeginPath(args.vg);
    nvgRect(args.vg, x, y, w, h);
    NVGpaint paint = nvgImagePattern(args.vg, x, y, w, h, 0.f, image->handle, alpha);
    nvgFillPaint(args.vg, paint);
    nvgFill(args.vg);
}

struct S8Panel : Widget {
    S8* module = nullptr;
    explicit S8Panel(S8* m) : module(m) {}

    void draw(const widget::Widget::DrawArgs& args) override {
        // Theme panels contain STATIC ART ONLY: substrate, permanent text,
        // dividers, display frame, tick marks and branding. They contain no
        // knob, jack, screw, LED or pointer pixels. The four files are generated
        // from one canonical geometry mask, so theme switching can only change
        // approved panel/text colors. All interactive hardware is drawn by the
        // shared widgets below and is therefore pixel-identical across themes.
        const int theme = module ? clampInt(module->panelTheme, 0, 3) : 0;
        static const char* panelFiles[4] = {
            "res/S8/S8Panel-Light.png",
            "res/S8/S8Panel-Dark.png",
            "res/S8/S8Panel-Smoke.png",
            "res/S8/S8Panel-Silver.png"
        };
        drawPluginImage(args, panelFiles[theme], 0.f, 0.f, box.size.x, box.size.y);
    }
};

struct S8Display : Widget {
    S8* module = nullptr;
    explicit S8Display(S8* m) : module(m) {}

    void draw(const widget::Widget::DrawArgs& args) override {
        const int active = module ? clampInt(module->activeStep, 0, kSteps - 1) : 0;
        const int root = module ? clampInt(module->presetRoots[active], 0, kRoots - 1) : 0;
        const int scale = module ? clampInt(module->presetScales[active], 0, (int) kScales.size() - 1) : 0;
        const int theme = module ? clampInt(module->panelTheme, 0, 3) : 0;
        const bool dark = (theme == 1 || theme == 2);
        const char* suffix = dark ? "dark" : "light";

        // Pre-rendered text assets avoid runtime font substitution.
        drawPluginImage(args, string::f("res/S8/display/root_%d_%s.png", root, suffix),
                        30.f, 41.f, 30.f, 12.f);
        drawPluginImage(args, string::f("res/S8/display/scale_%d_%s.png", scale, suffix),
                        15.f, 52.3f, 60.f, 10.f);
    }
};

struct S8StepLed : Widget {
    S8* module = nullptr;
    int index = 0;

    S8StepLed(S8* m, int i) : module(m), index(i) {
        box.size = Vec(5.f, 5.f);
    }

    void draw(const widget::Widget::DrawArgs& args) override {
        const int active = module ? clampInt(module->activeStep, 0, kSteps - 1) : 0;
        const int length = module ? clampInt((int) std::round(module->params[S8::LENGTH_PARAM].getValue()), 1, kSteps) : 8;

        NVGcolor color;
        float radius;
        if (index == active) {
            color = nvgRGB(0, 157, 224);
            radius = 2.35f;
        }
        else if (index < length) {
            color = nvgRGB(113, 191, 224);
            radius = 1.72f;
        }
        else {
            color = nvgRGB(188, 190, 190);
            radius = 1.72f;
        }

        const Vec c = box.size.div(2.f);
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, c.x, c.y, radius);
        nvgFillColor(args.vg, color);
        nvgFill(args.vg);
    }
};

struct S8Screw : Widget {
    S8Screw() {
        box.size = Vec(17.f, 17.f);
    }

    void draw(const widget::Widget::DrawArgs& args) override {
        drawPluginImage(args, "res/S8/S8Screw.png", 0.f, 0.f, box.size.x, box.size.y);
    }
};

struct ThemeItem : MenuItem {
    S8* module = nullptr;
    int theme = 0;
    void onAction(const event::Action& e) override {
        if (module)
            module->panelTheme = theme;
    }
    void step() override {
        rightText = CHECKMARK(module && module->panelTheme == theme);
        MenuItem::step();
    }
};


struct SetRootItem : MenuItem {
    S8* module = nullptr;
    int slot = 0;
    int value = 0;
    void onAction(const event::Action& e) override {
        if (!module)
            return;
        const bool changed = module->presetRoots[slot] != value;
        module->presetRoots[slot] = value;
        if (changed && slot == 0 && module->autoFollowHome)
            module->intelligentAutoFill();
    }
    void step() override {
        rightText = CHECKMARK(module && module->presetRoots[slot] == value);
        MenuItem::step();
    }
};

struct SetScaleItem : MenuItem {
    S8* module = nullptr;
    int slot = 0;
    int value = 0;
    void onAction(const event::Action& e) override {
        if (!module)
            return;
        const bool changed = module->presetScales[slot] != value;
        module->presetScales[slot] = value;
        if (changed && slot == 0 && module->autoFollowHome)
            module->intelligentAutoFill();
    }
    void step() override {
        rightText = CHECKMARK(module && module->presetScales[slot] == value);
        MenuItem::step();
    }
};

struct RootSubmenuItem : MenuItem {
    S8* module = nullptr;
    int slot = 0;
    Menu* createChildMenu() override {
        Menu* menu = new Menu;
        for (int i = 0; i < kRoots; ++i) {
            auto* item = new SetRootItem;
            item->text = kRootNames[i];
            item->module = module;
            item->slot = slot;
            item->value = i;
            menu->addChild(item);
        }
        return menu;
    }
};

struct ScaleGroupItem : MenuItem {
    S8* module = nullptr;
    int slot = 0;
    std::vector<int> values;

    Menu* createChildMenu() override {
        Menu* menu = new Menu;
        for (int value : values) {
            auto* item = new SetScaleItem;
            item->text = kScales[value].name;
            item->module = module;
            item->slot = slot;
            item->value = value;
            menu->addChild(item);
        }
        return menu;
    }
};

struct ScaleSubmenuItem : MenuItem {
    S8* module = nullptr;
    int slot = 0;

    Menu* createChildMenu() override {
        Menu* menu = new Menu;

        auto addGroup = [&](const char* name, const std::vector<int>& values) {
            auto* group = new ScaleGroupItem;
            group->text = name;
            group->rightText = RIGHT_ARROW;
            group->module = module;
            group->slot = slot;
            group->values = values;
            menu->addChild(group);
        };

        addGroup("Diatonic / modes", {0, 1, 2, 3, 4, 5, 6});
        addGroup("Minor variants", {7, 8});
        addGroup("Pentatonic / blues", {9, 10, 11});
        addGroup("Symmetric / chromatic", {12, 13, 14, 15});
        addGroup("Chord pitch sets", {16, 17, 18, 19, 20, 21, 22, 23});
        return menu;
    }
};

struct SuggestionApplyItem : MenuItem {
    S8* module = nullptr;
    int slot = 1;
    int root = 0;
    int scale = SCALE_MAJOR;
    void onAction(const event::Action& e) override {
        if (module) {
            module->presetRoots[slot] = root;
            module->presetScales[slot] = scale;
            // Move the active pattern to the slot just accepted. On the next right-click,
            // the top-level suggestion automatically advances to Pattern 3, 4, 5, etc.
            module->selectStep(slot, true);
        }
    }
};

struct SuggestionSubmenuItem : MenuItem {
    S8* module = nullptr;
    int slot = 1;
    Menu* createChildMenu() override {
        Menu* menu = new Menu;
        if (!module)
            return menu;
        const auto recs = module->recommendationsForSlot(slot, 6);
        if (recs.empty()) {
            menu->addChild(createMenuLabel("No recommendations"));
            return menu;
        }
        static const char* quality[] = {"Excellent", "Strong", "Strong", "Good", "Good", "Color"};
        for (size_t i = 0; i < recs.size(); ++i) {
            const auto& r = recs[i];
            auto* item = new SuggestionApplyItem;
            item->text = string::f("%d. %s / %s", (int) i + 1, kRootNames[r.root], kScales[r.scale].name);
            item->rightText = quality[std::min<size_t>(i, 5)];
            item->module = module;
            item->slot = slot;
            item->root = r.root;
            item->scale = r.scale;
            menu->addChild(item);
        }
        return menu;
    }
};

struct BestSuggestionItem : MenuItem {
    S8* module = nullptr;
    int slot = 1;
    void onAction(const event::Action& e) override {
        if (module) {
            module->applyBestRecommendation(slot);
            module->selectStep(slot, true);
        }
    }
};

struct PresetSubmenuItem : MenuItem {
    S8* module = nullptr;
    int slot = 0;
    Menu* createChildMenu() override {
        Menu* menu = new Menu;

        auto* root = new RootSubmenuItem;
        root->text = "Root";
        root->rightText = module ? kRootNames[module->presetRoots[slot]] : RIGHT_ARROW;
        root->module = module;
        root->slot = slot;
        menu->addChild(root);

        auto* scale = new ScaleSubmenuItem;
        scale->text = "Scale";
        scale->rightText = module ? kScales[module->presetScales[slot]].name : RIGHT_ARROW;
        scale->module = module;
        scale->slot = slot;
        menu->addChild(scale);

        if (slot > 0) {
            menu->addChild(new MenuSeparator);

            auto* best = new BestSuggestionItem;
            best->text = "Use top recommendation";
            best->module = module;
            best->slot = slot;
            menu->addChild(best);

            auto* suggestions = new SuggestionSubmenuItem;
            suggestions->text = "Choose recommendation";
            suggestions->rightText = RIGHT_ARROW;
            suggestions->module = module;
            suggestions->slot = slot;
            menu->addChild(suggestions);
        }
        return menu;
    }
};

struct IntelligentFillItem : MenuItem {
    S8* module = nullptr;
    void onAction(const event::Action& e) override {
        if (module)
            module->intelligentAutoFill();
    }
};

struct AutoFollowHomeItem : MenuItem {
    S8* module = nullptr;
    void onAction(const event::Action& e) override {
        if (!module)
            return;
        module->autoFollowHome = !module->autoFollowHome;
        if (module->autoFollowHome)
            module->intelligentAutoFill();
    }
    void step() override {
        rightText = CHECKMARK(module && module->autoFollowHome);
        MenuItem::step();
    }
};

struct HarmonyModeItem : MenuItem {
    S8* module = nullptr;
    int mode = 0;
    void onAction(const event::Action& e) override {
        if (!module)
            return;
        const bool changed = module->harmonyMode != mode;
        module->harmonyMode = mode;
        if (changed && module->autoFollowHome)
            module->intelligentAutoFill();
    }
    void step() override {
        rightText = CHECKMARK(module && module->harmonyMode == mode);
        MenuItem::step();
    }
};

struct PatternBankSubmenuItem : MenuItem {
    S8* module = nullptr;
    Menu* createChildMenu() override {
        Menu* menu = new Menu;
        for (int i = 0; i < kSteps; ++i) {
            auto* item = new PresetSubmenuItem;
            item->text = string::f("%d%s  %s / %s", i + 1, i == 0 ? " HOME" : "",
                                   kRootNames[module ? module->presetRoots[i] : 0],
                                   kScales[module ? module->presetScales[i] : 0].name);
            item->rightText = RIGHT_ARROW;
            item->module = module;
            item->slot = i;
            menu->addChild(item);
        }
        return menu;
    }
};

struct RecommendationBankSubmenuItem : MenuItem {
    S8* module = nullptr;
    Menu* createChildMenu() override {
        Menu* menu = new Menu;
        for (int i = 1; i < kSteps; ++i) {
            auto* item = new SuggestionSubmenuItem;
            item->text = string::f("Pattern %d from prior harmony", i + 1);
            item->rightText = RIGHT_ARROW;
            item->module = module;
            item->slot = i;
            menu->addChild(item);
        }
        return menu;
    }
};

struct HarmonyStyleSubmenuItem : MenuItem {
    S8* module = nullptr;
    Menu* createChildMenu() override {
        Menu* menu = new Menu;
        for (int mode = 0; mode < 5; ++mode) {
            auto* item = new HarmonyModeItem;
            item->text = harmonyModeName(mode);
            item->module = module;
            item->mode = mode;
            menu->addChild(item);
        }
        return menu;
    }
};

struct ProgressionSubmenuItem : MenuItem {
    S8* module = nullptr;
    Menu* createChildMenu() override {
        Menu* menu = new Menu;
        menu->addChild(createMenuLabel("Pattern 1 is the HOME scale"));

        auto* fill = new IntelligentFillItem;
        fill->text = "Intelligently fill patterns 2-8";
        fill->module = module;
        menu->addChild(fill);

        auto* autoFollow = new AutoFollowHomeItem;
        autoFollow->text = "Auto-follow HOME";
        autoFollow->module = module;
        menu->addChild(autoFollow);

        menu->addChild(new MenuSeparator);
        auto* style = new HarmonyStyleSubmenuItem;
        style->text = "Harmony style";
        style->rightText = module ? harmonyModeName(module->harmonyMode) : RIGHT_ARROW;
        style->module = module;
        menu->addChild(style);
        return menu;
    }
};

struct ThemeSubmenuItem : MenuItem {
    S8* module = nullptr;
    Menu* createChildMenu() override {
        Menu* menu = new Menu;
        const char* names[] = {"Light", "Dark", "Smoke", "Silver"};
        for (int i = 0; i < 4; ++i) {
            auto* item = new ThemeItem;
            item->text = names[i];
            item->module = module;
            item->theme = i;
            menu->addChild(item);
        }
        return menu;
    }
};


struct S8Knob : app::Knob {
    float fallbackNorm = 0.f;

    S8Knob() {
        minAngle = -2.35f;
        maxAngle = 2.35f;
        box.size = Vec(48.333f, 48.333f);
    }

    void draw(const widget::Widget::DrawArgs& args) override {
        float norm = fallbackNorm;
        if (engine::ParamQuantity* pq = getParamQuantity()) {
            const float lo = pq->getMinValue();
            const float hi = pq->getMaxValue();
            if (hi > lo)
                norm = std::max(0.f, std::min((pq->getValue() - lo) / (hi - lo), 1.f));
        }

        // ONE canonical hardware asset for all three knobs and every panel theme.
        drawPluginImage(args, "res/S8/S8Knob.png", 0.f, 0.f, box.size.x, box.size.y);

        // The pointer is its own canonical asset and is the only rotating visual.
        // It was extracted from the approved Pattern knob at minAngle.
        const float angle = minAngle + (maxAngle - minAngle) * norm;
        const float rotation = angle - minAngle;
        const Vec c = box.size.div(2.f);

        nvgSave(args.vg);
        nvgTranslate(args.vg, c.x, c.y);
        nvgRotate(args.vg, rotation);
        nvgTranslate(args.vg, -c.x, -c.y);
        drawPluginImage(args, "res/S8/S8KnobPointer.png", 0.f, 0.f, box.size.x, box.size.y);
        nvgRestore(args.vg);
    }
};

struct S8Port : app::PortWidget {
    S8Port() {
        box.size = Vec(31.f, 31.f);
    }

    void draw(const widget::Widget::DrawArgs& args) override {
        // ONE neutral/unpatched jack asset is shared by every jack and every theme. Cable color is supplied only
        // by Rack's cable layer at runtime; the port itself never changes color.
        drawPluginImage(args, "res/S8/S8Jack.png", 0.f, 0.f, box.size.x, box.size.y);
    }
};

struct S8Widget : ModuleWidget {
    S8Widget(S8* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/S8/S8.svg")));

        // Canonical geometry: the source reference is 270x1140, exactly 90x380
        // at Rack scale (6HP). Every coordinate below is source/3.
        auto* panel = new S8Panel(module);
        panel->box.size = box.size;
        addChild(panel);

        auto* display = new S8Display(module);
        display->box.size = box.size;
        addChild(display);

        static const float ledX[kSteps] = {
            13.0f, 22.0f, 31.333f, 40.667f,
            49.0f, 58.333f, 67.333f, 76.333f
        };
        for (int i = 0; i < kSteps; ++i) {
            auto* led = new S8StepLed(module, i);
            led->box.pos = Vec(ledX[i] - led->box.size.x * 0.5f,
                               68.333f - led->box.size.y * 0.5f);
            addChild(led);
        }

        auto addScrewAt = [this](float x, float y) {
            auto* screw = new S8Screw;
            screw->box.pos = Vec(x - screw->box.size.x * 0.5f,
                                 y - screw->box.size.y * 0.5f);
            addChild(screw);
        };
        addScrewAt(13.333f, 8.667f);
        addScrewAt(76.667f, 8.667f);
        addScrewAt(13.333f, 371.333f);
        addScrewAt(76.667f, 371.333f);

        auto* pattern = createParamCentered<S8Knob>(Vec(45.f, 107.667f), module, S8::PATTERN_PARAM);
        pattern->fallbackNorm = 0.f;
        addParam(pattern);

        auto* length = createParamCentered<S8Knob>(Vec(45.f, 171.f), module, S8::LENGTH_PARAM);
        length->fallbackNorm = 1.f;
        addParam(length);

        auto* offset = createParamCentered<S8Knob>(Vec(45.f, 230.333f), module, S8::OFFSET_PARAM);
        offset->fallbackNorm = 0.5f;
        addParam(offset);

        addInput(createInputCentered<S8Port>(Vec(24.333f, 282.667f), module, S8::CLOCK_INPUT));
        addInput(createInputCentered<S8Port>(Vec(65.f, 282.667f), module, S8::RESET_INPUT));
        addInput(createInputCentered<S8Port>(Vec(24.333f, 328.333f), module, S8::PITCH_INPUT));
        addOutput(createOutputCentered<S8Port>(Vec(65.333f, 328.333f), module, S8::PITCH_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        S8* module = dynamic_cast<S8*>(this->module);
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("S8 Quantizer"));

        if (module) {
            const int slot = clampInt(module->activeStep, 0, kSteps - 1);
            menu->addChild(createMenuLabel(string::f("Pattern %d%s: %s / %s",
                                                     slot + 1, slot == 0 ? " HOME" : "",
                                                     kRootNames[module->presetRoots[slot]],
                                                     kScales[module->presetScales[slot]].name)));

            auto* root = new RootSubmenuItem;
            root->text = "Root";
            root->rightText = kRootNames[module->presetRoots[slot]];
            root->module = module;
            root->slot = slot;
            menu->addChild(root);

            auto* scale = new ScaleSubmenuItem;
            scale->text = "Scale";
            scale->rightText = kScales[module->presetScales[slot]].name;
            scale->module = module;
            scale->slot = slot;
            menu->addChild(scale);

            if (slot < kSteps - 1) {
                auto* next = new SuggestionSubmenuItem;
                next->text = string::f("Suggest next: Pattern %d", slot + 2);
                next->rightText = RIGHT_ARROW;
                next->module = module;
                next->slot = slot + 1;
                menu->addChild(next);
            }
        }

        menu->addChild(new MenuSeparator);

        auto* patterns = new PatternBankSubmenuItem;
        patterns->text = "Pattern bank 1-8";
        patterns->rightText = RIGHT_ARROW;
        patterns->module = module;
        menu->addChild(patterns);

        auto* fill = new IntelligentFillItem;
        fill->text = "Intelligently fill patterns 2-8";
        fill->module = module;
        menu->addChild(fill);

        auto* autoFollow = new AutoFollowHomeItem;
        autoFollow->text = "Auto-follow HOME";
        autoFollow->module = module;
        menu->addChild(autoFollow);

        auto* style = new HarmonyStyleSubmenuItem;
        style->text = "Harmony style";
        style->rightText = module ? harmonyModeName(module->harmonyMode) : RIGHT_ARROW;
        style->module = module;
        menu->addChild(style);

        auto* theme = new ThemeSubmenuItem;
        theme->text = "Panel color";
        const char* names[] = {"Light", "Dark", "Smoke", "Silver"};
        theme->rightText = module ? names[clampInt(module->panelTheme, 0, 3)] : RIGHT_ARROW;
        theme->module = module;
        menu->addChild(theme);
    }
};

Model* modelS8 = createModel<S8, S8Widget>("S8");
