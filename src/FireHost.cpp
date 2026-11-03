// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Chris DuPont / Xtreme Sounds

#include "plugin.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <cstdio>
#include <vector>

struct FireHost : Module {
    static constexpr int MAX_VOICES = 16;
    static constexpr int SLOT_COUNT = 20;
    static constexpr int BUTTON_COUNT = 25;

    enum ParamId { NUM_PARAMS };
    enum InputId { NUM_INPUTS };
    enum OutputId {
        PITCH_OUTPUT,
        GATE_OUTPUT,
        VELOCITY_OUTPUT,
        ASSIGN_OUTPUT_1,
        ASSIGN_OUTPUT_LAST = ASSIGN_OUTPUT_1 + SLOT_COUNT - 1,
        NUM_OUTPUTS = ASSIGN_OUTPUT_1 + SLOT_COUNT
    };
    enum LightId { CONNECT_LIGHT, MIDI_LIGHT, NUM_LIGHTS };

    // Raw Fire controls, matched to DrivenByMoss FireControlSurface.
    enum SourceId {
        SRC_NONE = 0,
        SRC_VOLUME,
        SRC_PAN,
        SRC_FILTER,
        SRC_RESONANCE,
        SRC_SELECT_ENCODER,
        SRC_TOUCH_VOLUME,
        SRC_TOUCH_PAN,
        SRC_TOUCH_FILTER,
        SRC_TOUCH_RESONANCE,
        SRC_SELECT_PUSH,
        SRC_BANK,
        SRC_PATTERN_UP,
        SRC_PATTERN_DOWN,
        SRC_BROWSER,
        SRC_GRID_LEFT,
        SRC_GRID_RIGHT,
        SRC_SOLO_1,
        SRC_SOLO_2,
        SRC_SOLO_3,
        SRC_SOLO_4,
        SRC_STEP,
        SRC_NOTE,
        SRC_DRUM,
        SRC_PERFORM,
        SRC_SHIFT,
        SRC_ALT,
        SRC_PATTERN,
        SRC_PLAY,
        SRC_STOP,
        SRC_RECORD,
        SRC_COUNT
    };

    enum ButtonMode { BUTTON_MOMENTARY = 0, BUTTON_TOGGLE, BUTTON_TRIGGER };
    enum PadVelocityMode { VELOCITY_SOFT = 0, VELOCITY_MEDIUM, VELOCITY_FIRM, VELOCITY_RAW };
    enum PanelTheme { PANEL_LIGHT = 0, PANEL_SILVER, PANEL_SMOKE, PANEL_DARK };
    enum PadColor {
        COLOR_RED = 0, COLOR_ORANGE, COLOR_AMBER, COLOR_YELLOW,
        COLOR_LIME, COLOR_GREEN, COLOR_MINT, COLOR_CYAN,
        COLOR_SKY, COLOR_BLUE, COLOR_INDIGO, COLOR_PURPLE,
        COLOR_MAGENTA, COLOR_PINK, COLOR_WHITE, COLOR_ICE,
        COLOR_COUNT
    };
    enum ScaleType {
        SCALE_MAJOR = 0, SCALE_NATURAL_MINOR, SCALE_DORIAN, SCALE_PHRYGIAN,
        SCALE_LYDIAN, SCALE_MIXOLYDIAN, SCALE_LOCRIAN, SCALE_HARMONIC_MINOR,
        SCALE_MELODIC_MINOR, SCALE_MAJOR_PENTATONIC, SCALE_MINOR_PENTATONIC,
        SCALE_BLUES, SCALE_WHOLE_TONE, SCALE_DIMINISHED_HW, SCALE_DIMINISHED_WH,
        SCALE_CHROMATIC, SCALE_COUNT
    };

    // Fire's view layer. NOTE intentionally has two Moss-style layouts: PLAY and PIANO.
    enum PadMode {
        PAD_PLAY = 0,
        PAD_PIANO,
        PAD_DRUM64
    };

    midi::InputQueue midiInput;
    midi::Output midiOutput;
    dsp::ClockDivider reconnectDivider;
    dsp::PulseGenerator midiPulse;

    bool connectionWasActive = false;
    bool fireVerified = false;
    bool hardwareInitialized = false;
    bool feedbackDirty = false;
    float midiActivityBrightness = 0.f;
    int lastInputDriverId = -9999;
    int lastInputDeviceId = -9999;

    // A voice is tied to a physical Fire pad, not to the raw MIDI note address.
    std::array<int, MAX_VOICES> voiceRawPads{};
    std::array<int, MAX_VOICES> voiceMidiNotes{};
    std::array<float, MAX_VOICES> voiceVelocity{};
    std::array<bool, MAX_VOICES> voiceGate{};
    int nextStealVoice = 0;

    // Five Fire endless relative encoders accumulated to the useful 0..127 range.
    // Touch messages are tracked separately and are never used as the encoder value.
    std::array<int, 5> encoderValues{{64, 64, 64, 64, 64}};
    std::array<bool, BUTTON_COUNT> buttonStates{};
    std::array<uint32_t, BUTTON_COUNT> buttonPressCounters{};

    PadVelocityMode padVelocityMode = VELOCITY_SOFT;
    PanelTheme panelTheme = PANEL_LIGHT;
    PadColor padColor = COLOR_BLUE;
    bool rootNoteWhite = false;
    int scaleRoot = 0; // C=0 ... B=11
    ScaleType scaleType = SCALE_MAJOR;

    // Akai Fire OLED state. The normal view shows root/scale. Touching or moving
    // an assigned encoder temporarily shows its current modulation amount.
    dsp::ClockDivider displayDivider;
    float displayOverrideSeconds = 0.f;
    int displaySource = SRC_NONE;
    int displayPercent = 0;
    bool displayDirty = true;

    // Moss-derived controller state simplified for standalone Rack.
    PadMode padMode = PAD_PLAY;
    int playOctave = 0;   // Moss Scales octave range: -4..+4
    int pianoOctave = 0;  // Moss piano octave range: -3..+3
    int drumOffset = 36;  // 64-note drum grid starts at MIDI 36
    int encoderBank = 0;  // Mirrors the Fire BANK concept (four banks)

    std::array<int, SLOT_COUNT> slotSources{};
    std::array<int, SLOT_COUNT> slotButtonModes{};
    std::array<bool, SLOT_COUNT> slotToggleStates{};
    std::array<uint32_t, SLOT_COUNT> slotSeenPressCounters{};
    std::array<dsp::PulseGenerator, SLOT_COUNT> slotPulses{};

    static constexpr std::array<int, BUTTON_COUNT> BUTTON_NOTES{{
        16, 17, 18, 19,
        25, 26,
        31, 32, 33, 34, 35,
        36, 37, 38, 39,
        44, 45, 46, 47, 48, 49, 50, 51, 52, 53
    }};

    static constexpr std::array<int, SLOT_COUNT> DEFAULT_SLOT_SOURCES{{
        SRC_VOLUME, SRC_PAN, SRC_FILTER, SRC_RESONANCE,
        SRC_SELECT_ENCODER, SRC_SELECT_PUSH, SRC_PLAY, SRC_STOP,
        SRC_SHIFT, SRC_ALT, SRC_PATTERN_UP, SRC_PATTERN_DOWN,
        SRC_BROWSER, SRC_GRID_LEFT, SRC_GRID_RIGHT, SRC_STEP,
        SRC_NOTE, SRC_DRUM, SRC_PERFORM, SRC_RECORD
    }};

    static constexpr std::array<const char*, SRC_COUNT> SOURCE_NAMES{{
        "Unassigned",
        "Volume encoder", "Pan encoder", "Filter encoder", "Resonance encoder", "Select encoder",
        "Volume knob touch", "Pan knob touch", "Filter knob touch", "Resonance knob touch",
        "Select push", "Bank", "Pattern up", "Pattern down", "Browser", "Grid left", "Grid right",
        "Solo 1", "Solo 2", "Solo 3", "Solo 4", "Step", "Note", "Drum", "Perform", "Shift", "Alt",
        "Pattern / Song", "Play", "Stop", "Record"
    }};

    static constexpr std::array<const char*, SRC_COUNT> SOURCE_SHORT_NAMES{{
        "---", "VOL", "PAN", "FILT", "RES", "SEL", "VOL TCH", "PAN TCH", "FILT TCH", "RES TCH",
        "S-PUSH", "BANK", "PAT+", "PAT-", "BROWSE", "G-L", "G-R", "SOLO 1", "SOLO 2", "SOLO 3",
        "SOLO 4", "STEP", "NOTE", "DRUM", "PERF", "SHIFT", "ALT", "PATTERN", "PLAY", "STOP", "REC"
    }};

    static const char* panelThemeKey(int theme) {
        static const char* keys[] = {"light", "silver", "smoke", "dark"};
        return keys[clamp(theme, (int) PANEL_LIGHT, (int) PANEL_DARK)];
    }
    static const char* panelThemeName(int theme) {
        static const char* names[] = {"Light", "Silver", "Smoke", "Dark"};
        return names[clamp(theme, (int) PANEL_LIGHT, (int) PANEL_DARK)];
    }
    static const char* velocityModeName(int mode) {
        static const char* names[] = {"Soft", "Medium", "Firm", "Raw"};
        return names[clamp(mode, (int) VELOCITY_SOFT, (int) VELOCITY_RAW)];
    }
    static const char* padColorName(int color) {
        static const char* names[] = {
            "Red", "Orange", "Amber", "Yellow", "Lime", "Green", "Mint", "Cyan",
            "Sky", "Blue", "Indigo", "Purple", "Magenta", "Pink", "White", "Ice"
        };
        return names[clamp(color, 0, (int) COLOR_COUNT - 1)];
    }
    static const char* rootName(int root) {
        static const char* names[] = {"C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"};
        return names[clamp(root, 0, 11)];
    }
    static const char* rootDisplayName(int root) {
        static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return names[clamp(root, 0, 11)];
    }
    static const char* scaleName(int scale) {
        static const char* names[] = {
            "Major", "Natural Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
            "Harmonic Minor", "Melodic Minor", "Major Pentatonic", "Minor Pentatonic", "Blues",
            "Whole Tone", "Diminished H-W", "Diminished W-H", "Chromatic"
        };
        return names[clamp(scale, 0, (int) SCALE_COUNT - 1)];
    }
    static const char* scaleDisplayName(int scale) {
        static const char* names[] = {
            "MAJOR", "NAT MIN", "DORIAN", "PHRYG", "LYDIAN", "MIXO", "LOCRIAN",
            "HARM MIN", "MEL MIN", "MAJ PENT", "MIN PENT", "BLUES",
            "WHOLE", "DIM H-W", "DIM W-H", "CHROM"
        };
        return names[clamp(scale, 0, (int) SCALE_COUNT - 1)];
    }
    static void padColorRgb(int color, uint8_t& r, uint8_t& g, uint8_t& b) {
        static const uint8_t rgb[COLOR_COUNT][3] = {
            {127, 0, 0}, {127, 28, 0}, {127, 62, 0}, {127, 110, 0},
            {72, 127, 0}, {0, 127, 0}, {0, 127, 62}, {0, 127, 127},
            {0, 72, 127}, {0, 28, 127}, {32, 0, 127}, {72, 0, 127},
            {127, 0, 127}, {127, 0, 60}, {127, 127, 127}, {72, 110, 127}
        };
        const int c = clamp(color, 0, (int) COLOR_COUNT - 1);
        r = rgb[c][0]; g = rgb[c][1]; b = rgb[c][2];
    }
    static int scaleLength(int scale) {
        static const int lengths[SCALE_COUNT] = {7,7,7,7,7,7,7,7,7,5,5,6,6,8,8,12};
        return lengths[clamp(scale, 0, (int) SCALE_COUNT - 1)];
    }
    static int scaleInterval(int scale, int degree) {
        static const int intervals[SCALE_COUNT][12] = {
            {0,2,4,5,7,9,11}, {0,2,3,5,7,8,10}, {0,2,3,5,7,9,10}, {0,1,3,5,7,8,10},
            {0,2,4,6,7,9,11}, {0,2,4,5,7,9,10}, {0,1,3,5,6,8,10}, {0,2,3,5,7,8,11},
            {0,2,3,5,7,9,11}, {0,2,4,7,9}, {0,3,5,7,10}, {0,3,5,6,7,10},
            {0,2,4,6,8,10}, {0,1,3,4,6,7,9,10}, {0,2,3,5,6,8,9,11}, {0,1,2,3,4,5,6,7,8,9,10,11}
        };
        const int s = clamp(scale, 0, (int) SCALE_COUNT - 1);
        const int len = scaleLength(s);
        return intervals[s][clamp(degree, 0, len - 1)];
    }
    static bool isKnobTouchSource(int source) {
        return source >= SRC_TOUCH_VOLUME && source <= SRC_TOUCH_RESONANCE;
    }
    static bool isAssignableButtonSource(int source) {
        return source >= SRC_SELECT_PUSH && source < SRC_COUNT;
    }

    FireHost() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configOutput(PITCH_OUTPUT, "Fire pads pitch (polyphonic 1V/oct)");
        configOutput(GATE_OUTPUT, "Fire pads gate (polyphonic)");
        configOutput(VELOCITY_OUTPUT, "Fire pads velocity (polyphonic 0-10V)");
        for (int i = 0; i < SLOT_COUNT; ++i)
            configOutput(ASSIGN_OUTPUT_1 + i, string::f("Assignable Fire control %d", i + 1));

        voiceRawPads.fill(-1);
        voiceMidiNotes.fill(-1);
        setDefaultAssignments();
        slotButtonModes.fill(BUTTON_MOMENTARY);
        midiInput.channel = -1;
        midiOutput.channel = -1;
        reconnectDivider.setDivision(22050);
        displayDivider.setDivision(2205);
    }

    void setDefaultAssignments() {
        slotSources = DEFAULT_SLOT_SOURCES;
    }

    static std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::tolower(c); });
        return s;
    }

    static bool isGenericUsbMidiName(const std::string& deviceName) {
        const std::string n = lower(deviceName);
        return n.find("usb audio device") != std::string::npos || n.find("usb midi device") != std::string::npos;
    }

    // Mirrors the useful part of Moss FireControllerDefinition discovery.
    // Windows-numbered variants still contain "FL STUDIO FIRE", so substring matching catches them.
    static int fireDeviceScore(const std::string& deviceName) {
        const std::string n = lower(deviceName);
        if (n.find("fl studio fire") != std::string::npos)
            return 100;
        if (n.find("akai fire") != std::string::npos)
            return 95;
        if (n == "fire" || n == "fire midi" || n.find(" fire") != std::string::npos)
            return 80;
        return 0;
    }

    void disconnectOutput() {
        // Blank the currently connected Fire before releasing the MIDI output.
        // VCV can tear down MIDI ports before module destruction during shutdown,
        // so relying only on the destructor can leave the controller lit.
        if (midiOutput.getDeviceId() >= 0)
            shutdownHardware();
        midiOutput.setDeviceId(-1);
        hardwareInitialized = false;
    }

    bool pairOutputToInput() {
        disconnectOutput();
        if (midiInput.getDeviceId() < 0 || midiInput.getDriverId() < 0)
            return false;
        midi::Driver* driver = midi::getDriver(midiInput.getDriverId());
        if (!driver)
            return false;

        const std::string inputName = driver->getInputDeviceName(midiInput.getDeviceId());
        const std::string inLower = lower(inputName);
        int best = -1;
        int bestScore = 0;
        std::vector<int> genericOutputs;

        for (int deviceId : driver->getOutputDeviceIds()) {
            const std::string outputName = driver->getOutputDeviceName(deviceId);
            const std::string outLower = lower(outputName);
            int score = 0;
            if (outLower == inLower)
                score = 1000;
            else if (inLower.find("fl studio fire") != std::string::npos && outLower.find("fl studio fire") != std::string::npos)
                score = 950;
            else if (inLower.find("akai fire") != std::string::npos && outLower.find("akai fire") != std::string::npos)
                score = 900;
            else if (inLower.find("fire") != std::string::npos && outLower.find("fire") != std::string::npos)
                score = 850;
            if (isGenericUsbMidiName(outputName))
                genericOutputs.push_back(deviceId);
            if (score > bestScore) {
                bestScore = score;
                best = deviceId;
            }
        }

        // Generic "USB Audio Device" is ambiguous. Only pair it automatically if unique.
        if (best < 0 && isGenericUsbMidiName(inputName) && genericOutputs.size() == 1)
            best = genericOutputs[0];
        if (best < 0)
            return false;

        midiOutput.setDriverId(midiInput.getDriverId());
        midiOutput.setDeviceId(best);
        midiOutput.setChannel(-1);
        return midiOutput.getDeviceId() >= 0;
    }

    bool autoConnect(bool forceRescan = false) {
        if (forceRescan && midiInput.getDeviceId() >= 0) {
            midiInput.setDeviceId(-1);
            disconnectOutput();
            clearPerformanceState();
            fireVerified = false;
        }
        if (midiInput.getDeviceId() >= 0)
            return true;

        int bestDriver = -1;
        int bestDevice = -1;
        int bestScore = 0;
        struct Candidate { int driverId; int deviceId; };
        std::vector<Candidate> genericInputs;

        for (int driverId : midi::getDriverIds()) {
            midi::Driver* driver = midi::getDriver(driverId);
            if (!driver)
                continue;
            for (int deviceId : driver->getInputDeviceIds()) {
                const std::string name = driver->getInputDeviceName(deviceId);
                const int score = fireDeviceScore(name);
                if (score > bestScore) {
                    bestScore = score;
                    bestDriver = driverId;
                    bestDevice = deviceId;
                }
                if (isGenericUsbMidiName(name))
                    genericInputs.push_back({driverId, deviceId});
            }
        }

        // Only take a generic USB MIDI name automatically if it is unambiguous.
        if (bestDevice < 0 && genericInputs.size() == 1) {
            bestDriver = genericInputs[0].driverId;
            bestDevice = genericInputs[0].deviceId;
        }
        if (bestDriver < 0 || bestDevice < 0)
            return false;

        midiInput.setDriverId(bestDriver);
        midiInput.setDeviceId(bestDevice);
        midiInput.setChannel(-1);
        return midiInput.getDeviceId() >= 0;
    }

    bool isConnected() {
        return midiInput.getDeviceId() >= 0;
    }

    std::string currentInputName() {
        midi::Driver* driver = midi::getDriver(midiInput.getDriverId());
        return (driver && midiInput.getDeviceId() >= 0) ? driver->getInputDeviceName(midiInput.getDeviceId()) : "Not connected";
    }

    std::string currentOutputName() {
        midi::Driver* driver = midi::getDriver(midiOutput.getDriverId());
        return (driver && midiOutput.getDeviceId() >= 0) ? driver->getOutputDeviceName(midiOutput.getDeviceId()) : "Not connected";
    }

    void sendCC(int cc, int value) {
        if (midiOutput.getDeviceId() < 0)
            return;
        midi::Message m;
        m.setStatus(0xB);
        m.setChannel(0);
        m.setNote((uint8_t) clamp(cc, 0, 127));
        m.setValue((uint8_t) clamp(value, 0, 127));
        midiOutput.sendMessage(m);
    }

    // Moss FirePadGrid physical translation, expressed as 0..63 matrix positions.
    // Matrix row 0 is the physical top row, row 3 is the bottom row.
    static int rawPadToMatrixIndex(int rawNote) {
        if (rawNote >= 102 && rawNote <= 117)
            return rawNote - 102;
        if (rawNote >= 86 && rawNote <= 101)
            return 16 + rawNote - 86;
        if (rawNote >= 70 && rawNote <= 85)
            return 32 + rawNote - 70;
        if (rawNote >= 54 && rawNote <= 69)
            return 48 + rawNote - 54;
        return -1;
    }

    int playNoteForIndex(int index) const {
        const int row = index / 16;
        const int col = index % 16;
        // DrivenByMoss-style Play grid: scale degrees increase left-to-right and
        // each row is shifted by three scale degrees (FOURTH_UP layout).
        const int scaleStep = row * 3 + col;
        const int len = scaleLength((int) scaleType);
        const int octave = scaleStep / len;
        const int degree = scaleStep % len;
        const int note = 36 + playOctave * 12 + scaleRoot + octave * 12 + scaleInterval((int) scaleType, degree);
        return (note >= 0 && note <= 127) ? note : -1;
    }

    int pianoNoteForIndex(int index) const {
        static const int piano[2][7] = {
            {0, 2, 4, 5, 7, 9, 11},
            {-1, 1, 3, -1, 6, 8, 10}
        };
        const int row = index / 16;
        const int col = index % 16;
        const int semi = piano[row % 2][col % 7];
        if (semi < 0)
            return -1;
        int baseOctave = 3 + pianoOctave;
        if (row >= 2)
            baseOctave += 2;
        const int note = semi + (baseOctave + col / 7) * 12;
        return (note >= 0 && note <= 127) ? note : -1;
    }

    // Exact AbstractDrum64View 16x4 pad ordering used by Moss.
    // Each physical 4x4 block is chromatic internally, with the four blocks laid left-to-right.
    int drum64NoteForIndex(int matrixIndex) const {
        const int row = matrixIndex / 16;
        const int col = matrixIndex % 16;
        const int blockX = col / 4;
        const int withinX = col % 4;
        const int note = drumOffset + blockX * 16 + withinX * 4 + row;
        return (note >= 0 && note <= 127) ? note : -1;
    }

    int padMidiNote(int matrixIndex) const {
        if (matrixIndex < 0 || matrixIndex >= 64)
            return -1;
        switch (padMode) {
            case PAD_PIANO:
                return pianoNoteForIndex(matrixIndex);
            case PAD_DRUM64:
                return drum64NoteForIndex(matrixIndex);
            case PAD_PLAY:
            default:
                return playNoteForIndex(matrixIndex);
        }
    }

    // Fire OLED protocol: 128x64 monochrome bitmap encoded into Akai's
    // 1175-byte remapped payload (SysEx command 0x0E). The glyph renderer is
    // intentionally tiny and self-contained so the plugin has no font/runtime dependency.
    static uint8_t glyphRow(char c, int row) {
        // 5x7 uppercase font; each return value uses the low five bits.
        static const uint8_t digits[10][7] = {
            {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},
            {31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
        };
        static const uint8_t letters[26][7] = {
            {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
            {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
            {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
            {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
            {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
        };
        if (row < 0 || row >= 7) return 0;
        if (c >= '0' && c <= '9') return digits[c - '0'][row];
        if (c >= 'A' && c <= 'Z') return letters[c - 'A'][row];
        switch (c) {
            case '#': { static const uint8_t g[7]={10,31,10,10,31,10,0}; return g[row]; }
            case '%': { static const uint8_t g[7]={17,2,4,8,16,17,0}; return g[row]; }
            case '-': return row == 3 ? 14 : 0;
            case '/': { static const uint8_t g[7]={1,2,2,4,8,8,16}; return g[row]; }
            case '.': return row == 6 ? 4 : 0;
            case ':': return (row == 2 || row == 5) ? 4 : 0;
            default: return 0;
        }
    }

    static void drawOledText(std::array<uint8_t, 128 * 64>& pixels, const std::string& text, int y, float desiredScale = 1.f) {
        if (text.empty()) return;
        const float baseWidth = (float) ((int) text.size() * 6 - 1);
        const float scale = std::min(desiredScale, 118.f / std::max(1.f, baseWidth));
        const int textW = (int) std::ceil(baseWidth * scale);
        float x0 = std::max(0.f, (128.f - textW) * 0.5f);
        for (char raw : text) {
            char c = (char) std::toupper((unsigned char) raw);
            for (int row = 0; row < 7; ++row) {
                const uint8_t bits = glyphRow(c, row);
                for (int col = 0; col < 5; ++col) {
                    if ((bits & (1u << (4 - col))) == 0) continue;
                    const int x1 = (int) std::floor(x0 + col * scale);
                    const int x2 = (int) std::ceil(x0 + (col + 1) * scale);
                    const int y1 = (int) std::floor(y + row * scale);
                    const int y2 = (int) std::ceil(y + (row + 1) * scale);
                    for (int py = y1; py < y2; ++py) for (int x = x1; x < x2; ++x)
                        if (x >= 0 && x < 128 && py >= 0 && py < 64) pixels[py * 128 + x] = 1;
                }
            }
            x0 += 6.f * scale;
        }
    }

    static void plotFireOledPixel(std::array<uint8_t, 1175>& bitmap, int x, int y, bool on) {
        static const int bitMutate[8][7] = {
            {13,19,25,31,37,43,49},{0,20,26,32,38,44,50},{1,7,27,33,39,45,51},{2,8,14,34,40,46,52},
            {3,9,15,21,41,47,53},{4,10,16,22,28,48,54},{5,11,17,23,29,35,55},{6,12,18,24,30,36,42}
        };
        if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
        int packedX = x + 128 * (y / 8);
        const int remap = bitMutate[y % 8][packedX % 7];
        const int index = 4 + (packedX / 7) * 8 + remap / 7;
        const uint8_t mask = (uint8_t) (1u << (remap % 7));
        if (index >= 0 && index < (int) bitmap.size()) {
            if (on) bitmap[index] |= mask; else bitmap[index] &= (uint8_t) ~mask;
        }
    }

    void sendOledLines(const std::string& top, const std::string& bottom) {
        if (midiOutput.getDeviceId() < 0) return;
        std::array<uint8_t, 128 * 64> pixels{};
        drawOledText(pixels, top, 5, 3.0625f);
        drawOledText(pixels, bottom, 33, 3.0625f);

        std::array<uint8_t, 1175> bitmap{};
        bitmap[0] = 0; bitmap[1] = 7; bitmap[2] = 0; bitmap[3] = 127;
        for (int y = 0; y < 56; ++y)
            for (int x = 0; x < 120; ++x)
                if (pixels[(y + 4) * 128 + (x + 4)])
                    plotFireOledPixel(bitmap, x + 4, y + 4, true);

        midi::Message m;
        m.bytes.clear();
        m.bytes.reserve(1183);
        m.bytes.push_back(0xF0); m.bytes.push_back(0x47); m.bytes.push_back(0x7F); m.bytes.push_back(0x43); m.bytes.push_back(0x0E);
        m.bytes.push_back((uint8_t) (bitmap.size() >> 7));
        m.bytes.push_back((uint8_t) (bitmap.size() & 0x7F));
        m.bytes.insert(m.bytes.end(), bitmap.begin(), bitmap.end());
        m.bytes.push_back(0xF7);
        midiOutput.sendMessage(m);
    }

    bool sourceIsAssigned(int source) const {
        if (source <= SRC_NONE || source >= SRC_COUNT) return false;
        for (int slot = 0; slot < SLOT_COUNT; ++slot)
            if (slotSources[slot] == source) return true;
        return false;
    }

    void showSourceOnDisplay(int source, int percent) {
        if (!sourceIsAssigned(source)) return;
        displaySource = source;
        displayPercent = clamp(percent, 0, 100);
        displayOverrideSeconds = 1.25f;
        displayDirty = true;
    }

    void sendOledProgress(const std::string& label, int percent) {
        if (midiOutput.getDeviceId() < 0) return;
        std::array<uint8_t, 128 * 64> pixels{};
        // Keep the proven text size, but leave a visible gap before the meter.
        // The Fire OLED transport exposes source rows 4..59, so keep the bar
        // comfortably inside that drawable window.
        drawOledText(pixels, label, 5, 3.0625f);
        drawOledText(pixels, string::f("%d%%", clamp(percent, 0, 100)), 27, 3.0625f);

        const int x = 9, y = 52, w = 110, h = 8;
        for (int px = x; px < x + w; ++px) {
            pixels[y * 128 + px] = 1;
            pixels[(y + h - 1) * 128 + px] = 1;
        }
        for (int py = y; py < y + h; ++py) {
            pixels[py * 128 + x] = 1;
            pixels[py * 128 + x + w - 1] = 1;
        }
        const int fill = (int) std::lround((w - 4) * clamp(percent, 0, 100) / 100.f);
        for (int py = y + 2; py < y + h - 2; ++py)
            for (int px = x + 2; px < x + 2 + fill; ++px)
                pixels[py * 128 + px] = 1;

        std::array<uint8_t, 1175> bitmap{};
        bitmap[0] = 0; bitmap[1] = 7; bitmap[2] = 0; bitmap[3] = 127;
        for (int py = 0; py < 56; ++py)
            for (int px = 0; px < 120; ++px)
                if (pixels[(py + 4) * 128 + (px + 4)])
                    plotFireOledPixel(bitmap, px + 4, py + 4, true);

        midi::Message m;
        m.bytes.clear();
        m.bytes.reserve(1183);
        m.bytes.push_back(0xF0); m.bytes.push_back(0x47); m.bytes.push_back(0x7F); m.bytes.push_back(0x43); m.bytes.push_back(0x0E);
        m.bytes.push_back((uint8_t) (bitmap.size() >> 7));
        m.bytes.push_back((uint8_t) (bitmap.size() & 0x7F));
        m.bytes.insert(m.bytes.end(), bitmap.begin(), bitmap.end());
        m.bytes.push_back(0xF7);
        midiOutput.sendMessage(m);
    }

    void sendCurrentDisplay() {
        if (displayOverrideSeconds > 0.f && displaySource > SRC_NONE && displaySource < SRC_COUNT)
            sendOledProgress(sourceShortName(displaySource), displayPercent);
        else
            sendOledLines(rootDisplayName(scaleRoot), scaleDisplayName((int) scaleType));
        displayDirty = false;
    }

    void sendPadLayoutFeedback() {
        if (midiOutput.getDeviceId() < 0)
            return;
        midi::Message m;
        m.bytes.clear();
        m.bytes.reserve(264);
        m.bytes.push_back(0xF0);
        m.bytes.push_back(0x47);
        m.bytes.push_back(0x7F);
        m.bytes.push_back(0x43);
        m.bytes.push_back(0x65);
        // 64 records * 4 bytes = 256 = 2*128 + 0.
        m.bytes.push_back(0x02);
        m.bytes.push_back(0x00);

        for (int raw = 54; raw <= 117; ++raw) {
            const int ledIndex = raw - 54;
            const int matrixIndex = rawPadToMatrixIndex(raw);
            const int note = padMidiNote(matrixIndex);
            uint8_t r = 0, g = 0, b = 0;
            if (note >= 0) {
                uint8_t baseR = 0, baseG = 0, baseB = 0;
                padColorRgb((int) padColor, baseR, baseG, baseB);
                float brightness = 0.42f;
                switch (padMode) {
                    case PAD_PLAY: {
                        const bool isRoot = ((((note - scaleRoot) % 12) + 12) % 12 == 0);
                        if (isRoot && rootNoteWhite) {
                            baseR = baseG = baseB = 127;
                            brightness = 1.f;
                        } else {
                            // Root notes are full-brightness; other scale degrees use the selected hue dimmed.
                            brightness = isRoot ? 1.f : 0.42f;
                        }
                        break;
                    }
                    case PAD_PIANO:
                        // Keep the piano-key contrast while tinting both key classes with the selected hue.
                        if ((note % 12) == 1 || (note % 12) == 3 || (note % 12) == 6 || (note % 12) == 8 || (note % 12) == 10)
                            brightness = 0.22f;
                        else
                            brightness = 0.62f;
                        break;
                    case PAD_DRUM64:
                        brightness = 0.62f;
                        break;
                }
                r = (uint8_t) clamp((int) std::lround(baseR * brightness), 0, 127);
                g = (uint8_t) clamp((int) std::lround(baseG * brightness), 0, 127);
                b = (uint8_t) clamp((int) std::lround(baseB * brightness), 0, 127);
            }
            m.bytes.push_back((uint8_t) ledIndex);
            m.bytes.push_back(r);
            m.bytes.push_back(g);
            m.bytes.push_back(b);
        }
        m.bytes.push_back(0xF7);
        midiOutput.sendMessage(m);
    }

    void sendModeFeedback() {
        // Fire button LEDs are controlled by CCs matching the mode button numbers.
        // STEP/PERFORM remain normal assignable Fire buttons in standalone Rack because
        // their Moss functions are DAW clip/sequencer views. NOTE and DRUM select the two
        // musical pad engines that have a direct VCV equivalent.
        sendCC(44, 0);
        sendCC(45, (padMode == PAD_PLAY || padMode == PAD_PIANO) ? 2 : 0);
        sendCC(46, padMode == PAD_DRUM64 ? 2 : 0);
        sendCC(47, 0);
    }

    void refreshHardwareFeedback() {
        if (midiOutput.getDeviceId() < 0)
            return;
        sendModeFeedback();
        sendPadLayoutFeedback();
        sendCurrentDisplay();
        feedbackDirty = false;
    }

    void shutdownHardware() {
        if (midiOutput.getDeviceId() < 0)
            return;

        // Blank the OLED.
        sendOledLines("", "");

        // Turn off all 64 RGB pads using the Fire pad-color SysEx packet.
        midi::Message padsOff;
        padsOff.bytes.clear();
        padsOff.bytes.reserve(264);
        padsOff.bytes.push_back(0xF0);
        padsOff.bytes.push_back(0x47);
        padsOff.bytes.push_back(0x7F);
        padsOff.bytes.push_back(0x43);
        padsOff.bytes.push_back(0x65);
        padsOff.bytes.push_back(0x02);
        padsOff.bytes.push_back(0x00);
        for (int ledIndex = 0; ledIndex < 64; ++ledIndex) {
            padsOff.bytes.push_back((uint8_t) ledIndex);
            padsOff.bytes.push_back(0);
            padsOff.bytes.push_back(0);
            padsOff.bytes.push_back(0);
        }
        padsOff.bytes.push_back(0xF7);
        midiOutput.sendMessage(padsOff);

        // Turn off every Fire button LED we know about.
        for (int cc : BUTTON_NOTES)
            sendCC(cc, 0);

        feedbackDirty = false;
        displayDirty = false;
        hardwareInitialized = false;
        fireVerified = false;
    }

    ~FireHost() override {
        shutdownHardware();
    }

    void initializeHardware() {
        if (midiOutput.getDeviceId() < 0 || hardwareInitialized)
            return;
        // No Fire "unlock" is needed. The controller layer is the protocol decoder,
        // mode state machine, physical-pad translation and feedback below.
        hardwareInitialized = true;
        feedbackDirty = true;
    }

    void syncMidiPorts() {
        const int driverId = midiInput.getDriverId();
        const int deviceId = midiInput.getDeviceId();
        if (driverId == lastInputDriverId && deviceId == lastInputDeviceId)
            return;

        lastInputDriverId = driverId;
        lastInputDeviceId = deviceId;
        clearPerformanceState();
        fireVerified = false;
        hardwareInitialized = false;
        feedbackDirty = false;

        if (deviceId < 0) {
            disconnectOutput();
            return;
        }

        midiInput.setChannel(-1);
        pairOutputToInput();

        midi::Driver* driver = midi::getDriver(driverId);
        if (driver && fireDeviceScore(driver->getInputDeviceName(deviceId)) > 0)
            initializeHardware();
    }

    static bool isEncoderSource(int source) {
        return source >= SRC_VOLUME && source <= SRC_SELECT_ENCODER;
    }
    static bool isButtonSource(int source) {
        return source >= SRC_TOUCH_VOLUME && source < SRC_COUNT;
    }
    static int sourceToEncoderIndex(int source) {
        return isEncoderSource(source) ? source - SRC_VOLUME : -1;
    }
    static int sourceToButtonIndex(int source) {
        return isButtonSource(source) ? source - SRC_TOUCH_VOLUME : -1;
    }
    static const char* sourceName(int source) {
        return (source >= 0 && source < SRC_COUNT) ? SOURCE_NAMES[source] : SOURCE_NAMES[SRC_NONE];
    }
    static const char* sourceShortName(int source) {
        return (source >= 0 && source < SRC_COUNT) ? SOURCE_SHORT_NAMES[source] : SOURCE_SHORT_NAMES[SRC_NONE];
    }

    int findButton(int note) const {
        for (size_t i = 0; i < BUTTON_NOTES.size(); ++i)
            if (BUTTON_NOTES[i] == note)
                return (int) i;
        return -1;
    }

    static int relativeDelta(uint8_t value) {
        if (value == 0)
            return 0;
        return value < 64 ? (int) value : (int) value - 128;
    }

    bool messageLooksLikeFire(const midi::Message& msg) const {
        if (msg.bytes.size() < 3)
            return false;
        const uint8_t status = msg.getStatus();
        const int number = msg.getNote();
        if (status == 0xB)
            return (number >= 16 && number <= 19) || number == 118;
        if (status == 0x8 || status == 0x9)
            return rawPadToMatrixIndex(number) >= 0 || findButton(number) >= 0;
        return false;
    }

    void clearVoices() {
        voiceRawPads.fill(-1);
        voiceMidiNotes.fill(-1);
        voiceVelocity.fill(0.f);
        voiceGate.fill(false);
        nextStealVoice = 0;
    }

    void clearPerformanceState() {
        clearVoices();
        buttonStates.fill(false);
        buttonPressCounters.fill(0);
        slotSeenPressCounters.fill(0);
        for (auto& p : slotPulses)
            p.reset();
    }

    void setPadMode(PadMode mode) {
        if (padMode == mode)
            return;
        clearVoices();
        padMode = mode;
        feedbackDirty = true;
        displayDirty = true;
    }

    void shiftCurrentPadRange(int direction) {
        if (direction == 0)
            return;
        clearVoices();
        switch (padMode) {
            case PAD_PLAY:
                playOctave = clamp(playOctave + direction, -4, 4);
                break;
            case PAD_PIANO:
                pianoOctave = clamp(pianoOctave + direction, -3, 3);
                break;
            case PAD_DRUM64:
                // Moss Drum64 range is -2..+1 blocks of 16 notes around MIDI 36.
                drumOffset = clamp(drumOffset + direction * 16, 4, 52);
                break;
        }
        feedbackDirty = true;
        displayDirty = true;
    }

    int findVoiceForRawPad(int rawPad) const {
        for (int i = 0; i < MAX_VOICES; ++i)
            if (voiceGate[i] && voiceRawPads[i] == rawPad)
                return i;
        return -1;
    }

    int allocateVoice(int rawPad, int midiNote) {
        const int existing = findVoiceForRawPad(rawPad);
        if (existing >= 0)
            return existing;
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (!voiceGate[i]) {
                voiceRawPads[i] = rawPad;
                voiceMidiNotes[i] = midiNote;
                return i;
            }
        }
        const int voice = nextStealVoice;
        nextStealVoice = (nextStealVoice + 1) % MAX_VOICES;
        voiceGate[voice] = false;
        voiceVelocity[voice] = 0.f;
        voiceRawPads[voice] = rawPad;
        voiceMidiNotes[voice] = midiNote;
        return voice;
    }

    void releaseVoiceForRawPad(int rawPad) {
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voiceRawPads[i] == rawPad) {
                // MIDI note-off should only drop the gate. Keep pitch and velocity latched
                // until this voice slot is reused so an envelope release tail does not
                // jump to 0V/C and sound like a second/retriggered note.
                voiceGate[i] = false;
                voiceRawPads[i] = -1;
            }
        }
    }

    float applyPadVelocity(int rawVelocity) const {
        const float x = clamp(rawVelocity / 127.f, 0.f, 1.f);
        // The Fire's raw piezo response is naturally firm. These four host-side response
        // curves make the useful velocity range accessible without altering note timing.
        float y = x;
        switch (padVelocityMode) {
            case VELOCITY_SOFT:   y = std::pow(x, 0.45f); break;
            case VELOCITY_MEDIUM: y = std::pow(x, 0.65f); break;
            case VELOCITY_FIRM:   y = std::pow(x, 0.85f); break;
            case VELOCITY_RAW:    y = x; break;
        }
        return clamp(y, 0.f, 1.f);
    }

    void handleModeButtonRising(int note) {
        switch (note) {
            case 45:
                if (padMode == PAD_PLAY)
                    setPadMode(PAD_PIANO);
                else
                    setPadMode(PAD_PLAY);
                break;
            case 46:
                setPadMode(PAD_DRUM64);
                break;
            case 26:
                encoderBank = (encoderBank + 1) & 3;
                break;
            default:
                break;
        }
    }

    void handleMessage(const midi::Message& msg) {
        if (msg.bytes.size() < 3)
            return;

        const uint8_t status = msg.getStatus();
        const int number = msg.getNote();
        const int value = msg.getValue();
        midiPulse.trigger(0.03f);

        if (!fireVerified && messageLooksLikeFire(msg)) {
            fireVerified = true;
            initializeHardware();
        }

        if (status == 0xB) {
            int encoder = -1;
            if (number >= 16 && number <= 19)
                encoder = number - 16;
            else if (number == 118)
                encoder = 4;

            if (encoder >= 0) {
                const int delta = relativeDelta((uint8_t) value);
                // Fire sends endless relative encoder deltas. Accumulate those deltas to a stable
                // 0..127 value, exactly like an attenuator position exposed to Rack.
                encoderValues[encoder] = clamp(encoderValues[encoder] + delta, 0, 127);
                if (delta != 0) {
                    const int source = SRC_VOLUME + encoder;
                    const int pct = (int) std::lround(encoderValues[encoder] * 100.f / 127.f);
                    showSourceOnDisplay(source, pct);
                }
                // Select encoder is a musical root selector whenever it is not assigned
                // as a modulation source. If assigned, preserve modulation behavior only.
                if (encoder == 4 && delta != 0 && !sourceIsAssigned(SRC_SELECT_ENCODER)) {
                    scaleRoot = (scaleRoot + (delta > 0 ? 1 : -1) + 12) % 12;
                    feedbackDirty = true;
                    displayDirty = true;
                }
            }
            return;
        }

        const bool noteOn = (status == 0x9 && value > 0);
        const bool noteOff = (status == 0x8) || (status == 0x9 && value == 0);
        if (!noteOn && !noteOff)
            return;

        const int matrixIndex = rawPadToMatrixIndex(number);
        if (matrixIndex >= 0) {
            if (noteOn) {
                const int musicalNote = padMidiNote(matrixIndex);
                if (musicalNote >= 0) {
                    const int voice = allocateVoice(number, musicalNote);
                    voiceGate[voice] = true;
                    voiceVelocity[voice] = applyPadVelocity(value);
                }
            }
            else {
                releaseVoiceForRawPad(number);
            }
            return;
        }

        const int button = findButton(number);
        if (button >= 0) {
            const bool wasPressed = buttonStates[button];
            buttonStates[button] = noteOn;
            const int source = SRC_TOUCH_VOLUME + button;

            // The first four Fire note buttons are the encoder touch sensors. When an
            // encoder itself is assigned, touching it should reveal the encoder's current
            // modulation amount rather than requiring the separate touch source to be assigned.
            if (button >= 0 && button < 4 && sourceIsAssigned(SRC_VOLUME + button)) {
                const int encoderSource = SRC_VOLUME + button;
                const int pct = (int) std::lround(encoderValues[button] * 100.f / 127.f);
                showSourceOnDisplay(encoderSource, pct);
            }
            else if (sourceIsAssigned(source)) {
                showSourceOnDisplay(source, noteOn ? 100 : 0);
            }

            if (noteOn && !wasPressed) {
                ++buttonPressCounters[button];

                // Grid Left / Grid Right become previous / next scale when those
                // controls are not assigned as modulation sources. Assigned controls
                // remain modulation-only and do not alter the musical scale.
                if (source == SRC_GRID_LEFT && !sourceIsAssigned(SRC_GRID_LEFT)) {
                    scaleType = (ScaleType) (((int) scaleType - 1 + (int) SCALE_COUNT) % (int) SCALE_COUNT);
                    feedbackDirty = true;
                    displayDirty = true;
                }
                else if (source == SRC_GRID_RIGHT && !sourceIsAssigned(SRC_GRID_RIGHT)) {
                    scaleType = (ScaleType) (((int) scaleType + 1) % (int) SCALE_COUNT);
                    feedbackDirty = true;
                    displayDirty = true;
                }
                else {
                    handleModeButtonRising(number);
                }
            }
        }
    }

    int getOutputChannelCount() const {
        int channels = 1;
        // Keep allocated/latched voice channels present after note-off. Collapsing the
        // poly channel count on release can make downstream poly voices reassign while
        // their envelopes are still releasing. Slots are reused normally on the next press.
        for (int i = 0; i < MAX_VOICES; ++i)
            if (voiceMidiNotes[i] >= 0)
                channels = i + 1;
        return channels;
    }

    float processAssignableSlot(int slot, float sampleTime) {
        const int source = slotSources[slot];
        const int encoder = sourceToEncoderIndex(source);
        if (encoder >= 0)
            return (encoderValues[encoder] / 127.f) * 10.f;

        const int button = sourceToButtonIndex(source);
        if (button < 0)
            return 0.f;

        const bool pressed = buttonStates[button];
        const uint32_t serial = buttonPressCounters[button];
        const uint32_t newPresses = serial - slotSeenPressCounters[slot];
        if (newPresses > 0) {
            slotSeenPressCounters[slot] = serial;
            if (slotButtonModes[slot] == BUTTON_TOGGLE && (newPresses & 1u))
                slotToggleStates[slot] = !slotToggleStates[slot];
            else if (slotButtonModes[slot] == BUTTON_TRIGGER)
                slotPulses[slot].trigger(0.01f);
        }

        switch (slotButtonModes[slot]) {
            case BUTTON_TOGGLE:
                return slotToggleStates[slot] ? 10.f : 0.f;
            case BUTTON_TRIGGER:
                return slotPulses[slot].process(sampleTime) ? 10.f : 0.f;
            case BUTTON_MOMENTARY:
            default:
                return pressed ? 10.f : 0.f;
        }
    }

    void process(const ProcessArgs& args) override {
        if (!isConnected() && reconnectDivider.process())
            autoConnect();
        syncMidiPorts();

        const bool connectedNow = isConnected();
        if (!connectedNow && connectionWasActive)
            clearPerformanceState();
        connectionWasActive = connectedNow;

        midi::Message msg;
        while (midiInput.tryPop(&msg, args.frame))
            handleMessage(msg);

        if (displayOverrideSeconds > 0.f) {
            displayOverrideSeconds = std::max(0.f, displayOverrideSeconds - args.sampleTime);
            if (displayOverrideSeconds == 0.f) {
                displaySource = SRC_NONE;
                displayPercent = 0;
                displayDirty = true;
            }
        }
        if (feedbackDirty && fireVerified && midiOutput.getDeviceId() >= 0)
            refreshHardwareFeedback();
        else if (displayDirty && fireVerified && midiOutput.getDeviceId() >= 0 && displayDivider.process())
            sendCurrentDisplay();

        const int channels = getOutputChannelCount();
        outputs[PITCH_OUTPUT].setChannels(channels);
        outputs[GATE_OUTPUT].setChannels(channels);
        outputs[VELOCITY_OUTPUT].setChannels(channels);
        for (int i = 0; i < MAX_VOICES; ++i) {
            const float pitch = (voiceMidiNotes[i] >= 0) ? ((voiceMidiNotes[i] - 60) / 12.f) : 0.f;
            outputs[PITCH_OUTPUT].setVoltage(pitch, i);
            outputs[GATE_OUTPUT].setVoltage(voiceGate[i] ? 10.f : 0.f, i);
            outputs[VELOCITY_OUTPUT].setVoltage(voiceVelocity[i] * 10.f, i);
        }

        for (int slot = 0; slot < SLOT_COUNT; ++slot)
            outputs[ASSIGN_OUTPUT_1 + slot].setVoltage(processAssignableSlot(slot, args.sampleTime));

        lights[CONNECT_LIGHT].setBrightness(fireVerified ? 1.f : 0.f);
        midiActivityBrightness = midiPulse.process(args.sampleTime) ? 1.f : 0.f;
        lights[MIDI_LIGHT].setBrightness(midiActivityBrightness);
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        clearPerformanceState();
        encoderValues = {{64, 64, 64, 64, 64}};
        setDefaultAssignments();
        slotButtonModes.fill(BUTTON_MOMENTARY);
        slotToggleStates.fill(false);
        slotSeenPressCounters.fill(0);
        padMode = PAD_PLAY;
        playOctave = 0;
        pianoOctave = 0;
        drumOffset = 36;
        encoderBank = 0;
        padVelocityMode = VELOCITY_SOFT;
        padColor = COLOR_BLUE;
        rootNoteWhite = false;
        scaleRoot = 0;
        scaleType = SCALE_MAJOR;
        connectionWasActive = isConnected();
        fireVerified = false;
        hardwareInitialized = false;
        feedbackDirty = true;
        displayOverrideSeconds = 0.f;
        displaySource = SRC_NONE;
        displayPercent = 0;
        displayDirty = true;
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "midi", midiInput.toJson());
        json_object_set_new(rootJ, "midiOutput", midiOutput.toJson());
        json_object_set_new(rootJ, "padMode", json_integer((int) padMode));
        json_object_set_new(rootJ, "playOctave", json_integer(playOctave));
        json_object_set_new(rootJ, "pianoOctave", json_integer(pianoOctave));
        json_object_set_new(rootJ, "drumOffset", json_integer(drumOffset));
        json_object_set_new(rootJ, "encoderBank", json_integer(encoderBank));
        json_object_set_new(rootJ, "padVelocityMode", json_integer((int) padVelocityMode));
        json_object_set_new(rootJ, "padColor", json_integer((int) padColor));
        json_object_set_new(rootJ, "rootNoteWhite", json_boolean(rootNoteWhite));
        json_object_set_new(rootJ, "scaleRoot", json_integer(scaleRoot));
        json_object_set_new(rootJ, "scaleType", json_integer((int) scaleType));
        json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

        json_t* encJ = json_array();
        for (int v : encoderValues)
            json_array_append_new(encJ, json_integer(v));
        json_object_set_new(rootJ, "encoders", encJ);

        json_t* slotsJ = json_array();
        json_t* modesJ = json_array();
        json_t* togglesJ = json_array();
        for (int i = 0; i < SLOT_COUNT; ++i) {
            json_array_append_new(slotsJ, json_integer(slotSources[i]));
            json_array_append_new(modesJ, json_integer(slotButtonModes[i]));
            json_array_append_new(togglesJ, json_boolean(slotToggleStates[i]));
        }
        json_object_set_new(rootJ, "slotSources", slotsJ);
        json_object_set_new(rootJ, "slotButtonModes", modesJ);
        json_object_set_new(rootJ, "slotToggleStates", togglesJ);
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        if (json_t* midiJ = json_object_get(rootJ, "midi"))
            midiInput.fromJson(midiJ);
        if (json_t* midiOutJ = json_object_get(rootJ, "midiOutput"))
            midiOutput.fromJson(midiOutJ);
        lastInputDriverId = -9999;
        lastInputDeviceId = -9999;

        if (json_t* j = json_object_get(rootJ, "padMode"))
            padMode = (PadMode) clamp((int) json_integer_value(j), (int) PAD_PLAY, (int) PAD_DRUM64);
        if (json_t* j = json_object_get(rootJ, "playOctave"))
            playOctave = clamp((int) json_integer_value(j), -4, 4);
        if (json_t* j = json_object_get(rootJ, "pianoOctave"))
            pianoOctave = clamp((int) json_integer_value(j), -3, 3);
        if (json_t* j = json_object_get(rootJ, "drumOffset"))
            drumOffset = clamp((int) json_integer_value(j), 4, 52);
        if (json_t* j = json_object_get(rootJ, "encoderBank"))
            encoderBank = clamp((int) json_integer_value(j), 0, 3);
        if (json_t* j = json_object_get(rootJ, "padVelocityMode"))
            padVelocityMode = (PadVelocityMode) clamp((int) json_integer_value(j), (int) VELOCITY_SOFT, (int) VELOCITY_RAW);
        if (json_t* j = json_object_get(rootJ, "padColor"))
            padColor = (PadColor) clamp((int) json_integer_value(j), 0, (int) COLOR_COUNT - 1);
        if (json_t* j = json_object_get(rootJ, "rootNoteWhite"))
            rootNoteWhite = json_boolean_value(j);
        if (json_t* j = json_object_get(rootJ, "scaleRoot"))
            scaleRoot = clamp((int) json_integer_value(j), 0, 11);
        if (json_t* j = json_object_get(rootJ, "scaleType"))
            scaleType = (ScaleType) clamp((int) json_integer_value(j), 0, (int) SCALE_COUNT - 1);
        if (json_t* j = json_object_get(rootJ, "panelTheme"))
            panelTheme = (PanelTheme) clamp((int) json_integer_value(j), (int) PANEL_LIGHT, (int) PANEL_DARK);

        if (json_t* encJ = json_object_get(rootJ, "encoders")) {
            for (size_t i = 0; i < encoderValues.size(); ++i) {
                if (json_t* valueJ = json_array_get(encJ, i)) {
                    const double stored = json_number_value(valueJ);
                    // Backward compatibility with 2.2.1 patches that stored normalized 0..1.
                    encoderValues[i] = clamp((int) std::lround(stored <= 1.0 ? stored * 127.0 : stored), 0, 127);
                }
            }
        }
        if (json_t* slotsJ = json_object_get(rootJ, "slotSources")) {
            for (int i = 0; i < SLOT_COUNT; ++i)
                if (json_t* valueJ = json_array_get(slotsJ, i))
                    slotSources[i] = clamp((int) json_integer_value(valueJ), 0, SRC_COUNT - 1);
        }
        if (json_t* modesJ = json_object_get(rootJ, "slotButtonModes")) {
            for (int i = 0; i < SLOT_COUNT; ++i)
                if (json_t* valueJ = json_array_get(modesJ, i))
                    slotButtonModes[i] = clamp((int) json_integer_value(valueJ), (int) BUTTON_MOMENTARY, (int) BUTTON_TRIGGER);
        }
        if (json_t* togglesJ = json_object_get(rootJ, "slotToggleStates")) {
            for (int i = 0; i < SLOT_COUNT; ++i)
                if (json_t* valueJ = json_array_get(togglesJ, i))
                    slotToggleStates[i] = json_boolean_value(valueJ);
        }
        feedbackDirty = true;
    }
};

constexpr std::array<int, FireHost::BUTTON_COUNT> FireHost::BUTTON_NOTES;
constexpr std::array<int, FireHost::SLOT_COUNT> FireHost::DEFAULT_SLOT_SOURCES;
constexpr std::array<const char*, FireHost::SRC_COUNT> FireHost::SOURCE_NAMES;
constexpr std::array<const char*, FireHost::SRC_COUNT> FireHost::SOURCE_SHORT_NAMES;

// -----------------------------------------------------------------------------
// Canonical GUI. Panel art contains only static background/text/dividers/branding.
// Every interactive control is a real Rack widget positioned in 630x1772 panel
// coordinates measured from the approved reference image.
// -----------------------------------------------------------------------------
static Vec panelPoint(float x, float y) {
    return Vec(x * (9.f * RACK_GRID_WIDTH) / 630.f, y * RACK_GRID_HEIGHT / 1772.f);
}
static Vec panelSize(float w, float h) {
    return Vec(w * (9.f * RACK_GRID_WIDTH) / 630.f, h * RACK_GRID_HEIGHT / 1772.f);
}

static void drawImageFile(const widget::Widget::DrawArgs& args, const std::string& path, Vec size) {
    if (!APP || !APP->window)
        return;
    std::shared_ptr<window::Image> image = APP->window->loadImage(asset::plugin(pluginInstance, path));
    if (!image || image->handle < 0)
        return;
    nvgBeginPath(args.vg);
    nvgRect(args.vg, 0.f, 0.f, size.x, size.y);
    NVGpaint paint = nvgImagePattern(args.vg, 0.f, 0.f, size.x, size.y, 0.f, image->handle, 1.f);
    nvgFillPaint(args.vg, paint);
    nvgFill(args.vg);
}

static const char* fireThemeKey(FireHost* module) {
    return module ? FireHost::panelThemeKey(module->panelTheme) : "light";
}

struct FireCanonicalPanel : TransparentWidget {
    FireHost* module = nullptr;
    void draw(const widget::Widget::DrawArgs& args) override {
        drawImageFile(args, string::f("res/FireHost/FireHost-panel-%s.png", fireThemeKey(module)), box.size);
    }
};

struct FireDecorativeImage : TransparentWidget {
    std::string path;
    void draw(const widget::Widget::DrawArgs& args) override {
        drawImageFile(args, path, box.size);
    }
};

struct FireFindButton : OpaqueWidget {
    FireHost* module = nullptr;
    void draw(const widget::Widget::DrawArgs& args) override {
        drawImageFile(args, string::f("res/FireHost/ui/find-%s.png", fireThemeKey(module)), box.size);
    }
    void onButton(const event::Button& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) {
                module->autoConnect(true);
                module->syncMidiPorts();
            }
            e.consume(this);
        }
    }
};

struct FireMidiHotspot : OpaqueWidget {
    FireHost* module = nullptr;
    void draw(const widget::Widget::DrawArgs&) override {}
    void onButton(const event::Button& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && module) {
            ui::Menu* menu = createMenu();
            menu->addChild(createMenuLabel("Akai Fire connection"));
            menu->addChild(createMenuLabel(string::f("Input: %s", module->currentInputName().c_str())));
            menu->addChild(createMenuLabel(string::f("Output: %s", module->currentOutputName().c_str())));
            menu->addChild(new MenuSeparator);
            menu->addChild(createMenuItem("Find / reconnect Akai Fire", "", [=]() {
                module->autoConnect(true);
                module->syncMidiPorts();
            }));
            e.consume(this);
        }
    }
};

struct FireStatusLed : TransparentWidget {
    FireHost* module = nullptr;
    bool linkLed = false;
    void draw(const widget::Widget::DrawArgs& args) override {
        drawImageFile(args, string::f("res/FireHost/ui/led-off-%s.png", fireThemeKey(module)), box.size);
        if (!module)
            return;
        float brightness = linkLed ? (module->fireVerified ? 1.f : 0.f) : module->midiActivityBrightness;
        if (brightness <= 0.001f)
            return;
        const float r = std::min(box.size.x, box.size.y) * 0.24f;
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, r);
        nvgFillColor(args.vg, linkLed ? nvgRGBAf(0.15f, 1.f, 0.25f, 0.95f * brightness)
                                      : nvgRGBAf(0.05f, 0.68f, 1.f, 0.95f * brightness));
        nvgFill(args.vg);
    }
};

static void appendSourceAssignmentMenu(FireHost* module, int slot, Menu* menu) {
    if (!module || slot < 0 || slot >= FireHost::SLOT_COUNT)
        return;
    menu->addChild(createMenuLabel(string::f("Assignable output %d", slot + 1)));
    menu->addChild(createMenuItem("Unassigned", module->slotSources[slot] == FireHost::SRC_NONE ? CHECKMARK_STRING : "", [=]() {
        module->slotSources[slot] = FireHost::SRC_NONE;
        module->slotToggleStates[slot] = false;
        module->slotSeenPressCounters[slot] = 0;
    }));

    menu->addChild(new MenuSeparator);
    menu->addChild(createMenuLabel("Continuous encoders (0-127 -> 0-10V)"));
    for (int source = FireHost::SRC_VOLUME; source <= FireHost::SRC_SELECT_ENCODER; ++source) {
        menu->addChild(createMenuItem(FireHost::sourceName(source), module->slotSources[slot] == source ? CHECKMARK_STRING : "", [=]() {
            module->slotSources[slot] = source;
            module->slotToggleStates[slot] = false;
            module->slotSeenPressCounters[slot] = 0;
        }));
    }

    menu->addChild(new MenuSeparator);
    menu->addChild(createMenuLabel("Buttons"));
    for (int source = FireHost::SRC_SELECT_PUSH; source < FireHost::SRC_COUNT; ++source) {
        menu->addChild(createMenuItem(FireHost::sourceName(source), module->slotSources[slot] == source ? CHECKMARK_STRING : "", [=]() {
            module->slotSources[slot] = source;
            module->slotToggleStates[slot] = false;
            const int button = FireHost::sourceToButtonIndex(source);
            module->slotSeenPressCounters[slot] = button >= 0 ? module->buttonPressCounters[button] : 0;
        }));
    }

    if (FireHost::isAssignableButtonSource(module->slotSources[slot])) {
        menu->addChild(new MenuSeparator);
        menu->addChild(createSubmenuItem("Button behavior", "", [=](Menu* child) {
            static const char* names[] = {"Momentary gate", "Toggle", "10 ms trigger"};
            for (int mode = FireHost::BUTTON_MOMENTARY; mode <= FireHost::BUTTON_TRIGGER; ++mode) {
                child->addChild(createMenuItem(names[mode], module->slotButtonModes[slot] == mode ? CHECKMARK_STRING : "", [=]() {
                    module->slotButtonModes[slot] = mode;
                    module->slotToggleStates[slot] = false;
                    const int button = FireHost::sourceToButtonIndex(module->slotSources[slot]);
                    module->slotSeenPressCounters[slot] = button >= 0 ? module->buttonPressCounters[button] : 0;
                    module->slotPulses[slot].reset();
                }));
            }
        }));
    }
}

struct FireImagePort : app::PortWidget {
    FireHost* fireModule = nullptr;
    int slot = -1;
    FireImagePort() {
        type = engine::Port::OUTPUT;
    }
    void draw(const widget::Widget::DrawArgs& args) override {
        drawImageFile(args, "res/FireHost/ui/jack-shared.png", box.size);
    }
    void appendContextMenu(Menu* menu) override {
        if (slot >= 0)
            appendSourceAssignmentMenu(fireModule, slot, menu);
    }
};

struct FireDynamicSlotLabel : OpaqueWidget {
    FireHost* module = nullptr;
    int slot = 0;

    int displayedSource() const {
        if (module && slot >= 0 && slot < FireHost::SLOT_COUNT)
            return module->slotSources[slot];
        return (slot >= 0 && slot < FireHost::SLOT_COUNT) ? FireHost::DEFAULT_SLOT_SOURCES[slot] : FireHost::SRC_NONE;
    }

    void draw(const widget::Widget::DrawArgs& args) override {
        const int source = clamp(displayedSource(), 0, FireHost::SRC_COUNT - 1);

        // One unified monochrome label display is shared by all four panel themes.
        // Only the text is dynamic, so assigning a different Fire control does not
        // require a separate PNG for every source/theme combination.
        drawImageFile(args, "res/FireHost/ui/label-screen.png", box.size);

        if (!APP || !APP->window)
            return;
        std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (!font)
            return;

        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, box.size.y * 0.52f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGBA(218, 229, 220, 255));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.52f, FireHost::sourceShortName(source), nullptr);
    }

    void onButton(const event::Button& e) override {
        if ((e.button == GLFW_MOUSE_BUTTON_LEFT || e.button == GLFW_MOUSE_BUTTON_RIGHT) && e.action == GLFW_PRESS && module) {
            ui::Menu* menu = createMenu();
            appendSourceAssignmentMenu(module, slot, menu);
            e.consume(this);
        }
    }
};

static FireImagePort* createFireOutput(Vec center, Vec size, FireHost* module, int outputId, int slot = -1) {
    FireImagePort* port = new FireImagePort;
    port->module = module;
    port->fireModule = module;
    port->portId = outputId;
    port->slot = slot;
    port->box.size = size;
    port->box.pos = center.minus(size.div(2.f));
    return port;
}

struct FireHostWidget : ModuleWidget {
    FireHostWidget(FireHost* module) {
        setModule(module);
        box.size = Vec(9.f * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        FireCanonicalPanel* panel = new FireCanonicalPanel;
        panel->module = module;
        panel->box.pos = Vec(0.f, 0.f);
        panel->box.size = box.size;
        addChild(panel);

        // Decorative screws are separate widgets, not flattened into panel art.
        const Vec screwCenters[4] = {panelPoint(39.f,35.f), panelPoint(591.f,35.f), panelPoint(39.f,1732.f), panelPoint(591.f,1732.f)};
        for (int i = 0; i < 4; ++i) {
            FireDecorativeImage* s = new FireDecorativeImage;
            s->path = "res/FireHost/ui/screw-shared.png";
            s->box.size = panelSize(78.f,78.f);
            s->box.pos = screwCenters[i].minus(s->box.size.div(2.f));
            addChild(s);
        }

        // FIND is a real clickable widget, using the exact canonical crop.
        FireFindButton* find = new FireFindButton;
        find->module = module;
        find->box.pos = panelPoint(84.f,216.f);
        find->box.size = panelSize(183.f,110.f);
        addChild(find);

        // MIDI DEVICE title acts as explicit IN/OUT selector menu.
        FireMidiHotspot* midi = new FireMidiHotspot;
        midi->module = module;
        midi->box.pos = panelPoint(84.f, 167.f);
        midi->box.size = panelSize(464.f, 58.f);
        addChild(midi);

        const Vec ledCenters[2] = {panelPoint(372.5f,295.f), panelPoint(512.5f,295.f)};
        for (int i = 0; i < 2; ++i) {
            FireStatusLed* led = new FireStatusLed;
            led->module = module;
            led->linkLed = (i == 0);
            led->box.size = panelSize(62.f,62.f);
            led->box.pos = ledCenters[i].minus(led->box.size.div(2.f));
            addChild(led);
        }

        const Vec jackSize = panelSize(112.f,112.f);
        const Vec padCenters[3] = {panelPoint(124.f,526.f), panelPoint(315.f,526.f), panelPoint(507.f,526.f)};
        const int padIds[3] = {FireHost::PITCH_OUTPUT, FireHost::GATE_OUTPUT, FireHost::VELOCITY_OUTPUT};
        for (int i = 0; i < 3; ++i)
            addOutput(createFireOutput(padCenters[i], jackSize, module, padIds[i]));

        const float xs[4] = {99.f,243.f,387.f,531.f};
        const float jackYs[5] = {823.f,1002.f,1180.f,1356.f,1535.f};
        const float labelYs[5] = {744.f,922.f,1099.f,1276.f,1454.f};
        const Vec labelSize = panelSize(128.f,58.f);

        int slot = 0;
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 4; ++col) {
                FireDynamicSlotLabel* label = new FireDynamicSlotLabel;
                label->module = module;
                label->slot = slot;
                label->box.size = labelSize;
                label->box.pos = panelPoint(xs[col], labelYs[row]).minus(labelSize.div(2.f));
                addChild(label);

                FireImagePort* port = createFireOutput(panelPoint(xs[col], jackYs[row]), jackSize, module, FireHost::ASSIGN_OUTPUT_1 + slot, slot);
                addOutput(port);
                ++slot;
            }
        }
    }

    void appendContextMenu(Menu* menu) override {
        FireHost* module = getModule<FireHost>();
        if (!module)
            return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createSubmenuItem("Panel theme", FireHost::panelThemeName(module->panelTheme), [=](Menu* child) {
            for (int theme = FireHost::PANEL_LIGHT; theme <= FireHost::PANEL_DARK; ++theme) {
                child->addChild(createMenuItem(FireHost::panelThemeName(theme), module->panelTheme == theme ? CHECKMARK_STRING : "", [=]() {
                    module->panelTheme = (FireHost::PanelTheme) theme;
                }));
            }
        }));

        menu->addChild(createSubmenuItem("Pad velocity response", FireHost::velocityModeName(module->padVelocityMode), [=](Menu* child) {
            for (int mode = FireHost::VELOCITY_SOFT; mode <= FireHost::VELOCITY_RAW; ++mode) {
                child->addChild(createMenuItem(FireHost::velocityModeName(mode), module->padVelocityMode == mode ? CHECKMARK_STRING : "", [=]() {
                    module->padVelocityMode = (FireHost::PadVelocityMode) mode;
                }));
            }
        }));
        menu->addChild(createSubmenuItem("Pad color", FireHost::padColorName(module->padColor), [=](Menu* child) {
            for (int color = 0; color < FireHost::COLOR_COUNT; ++color) {
                child->addChild(createMenuItem(FireHost::padColorName(color), module->padColor == color ? CHECKMARK_STRING : "", [=]() {
                    module->padColor = (FireHost::PadColor) color;
                    module->feedbackDirty = true;
                }));
            }
        }));
        menu->addChild(createMenuItem("Root notes white", module->rootNoteWhite ? CHECKMARK_STRING : "", [=]() {
            module->rootNoteWhite = !module->rootNoteWhite;
            module->feedbackDirty = true;
        }));

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Play scale (Moss-style)"));
        menu->addChild(createSubmenuItem("Root note", FireHost::rootName(module->scaleRoot), [=](Menu* child) {
            for (int root = 0; root < 12; ++root) {
                child->addChild(createMenuItem(FireHost::rootName(root), module->scaleRoot == root ? CHECKMARK_STRING : "", [=]() {
                    module->clearVoices();
                    module->scaleRoot = root;
                    module->displayOverrideSeconds = 0.f;
                    module->displaySource = FireHost::SRC_NONE;
                    module->displayPercent = 0;
                    module->displayDirty = true;
                    module->feedbackDirty = true;
                }));
            }
        }));
        menu->addChild(createSubmenuItem("Scale", FireHost::scaleName(module->scaleType), [=](Menu* child) {
            for (int scale = 0; scale < FireHost::SCALE_COUNT; ++scale) {
                child->addChild(createMenuItem(FireHost::scaleName(scale), module->scaleType == scale ? CHECKMARK_STRING : "", [=]() {
                    module->clearVoices();
                    module->scaleType = (FireHost::ScaleType) scale;
                    module->displayOverrideSeconds = 0.f;
                    module->displaySource = FireHost::SRC_NONE;
                    module->displayPercent = 0;
                    module->displayDirty = true;
                    module->feedbackDirty = true;
                }));
            }
        }));

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Akai Fire"));
        menu->addChild(createMenuLabel(string::f("Input: %s", module->currentInputName().c_str())));
        menu->addChild(createMenuLabel(string::f("Output: %s", module->currentOutputName().c_str())));
        menu->addChild(createMenuItem("Find / reconnect Akai Fire", "", [=]() {
            module->autoConnect(true);
            module->syncMidiPorts();
        }));
        menu->addChild(createSubmenuItem("Maintenance", "", [=](Menu* child) {
            child->addChild(createMenuItem("Refresh Fire LEDs / pads", "", [=]() {
                module->hardwareInitialized = false;
                if (module->midiOutput.getDeviceId() < 0)
                    module->pairOutputToInput();
                module->initializeHardware();
            }));
        }));

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Assignable outputs"));
        menu->addChild(createMenuItem("Restore default assignments", "", [=]() {
            module->setDefaultAssignments();
            module->slotButtonModes.fill(FireHost::BUTTON_MOMENTARY);
            module->slotToggleStates.fill(false);
            module->slotSeenPressCounters.fill(0);
        }));
        menu->addChild(createMenuItem("Clear all assignments", "", [=]() {
            module->slotSources.fill(FireHost::SRC_NONE);
            module->slotButtonModes.fill(FireHost::BUTTON_MOMENTARY);
            module->slotToggleStates.fill(false);
            module->slotSeenPressCounters.fill(0);
        }));
    }
};

Model* modelFireHost = createModel<FireHost, FireHostWidget>("FireHost");
