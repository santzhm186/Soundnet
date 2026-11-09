#pragma once
// ABC notation -> MIDI clip.
//
// YuE2's symbolic planner emits ABC (audio.cpp writes it as `score.abc` next to
// the rendered audio). The score is the only part of a generation that a human
// can edit, so it has to become an ordinary MIDI clip: the piano roll, the scale
// quantiser, the arranger and MIDI export then work on it unchanged.
//
// The parser is pure text: no model, no dependency, deterministic. It follows
// the ABC 2.1 subset that generative models actually emit (single tune, K:
// terminated header, one or more V: voices, chords, ties, tuplets, broken
// rhythm, accidentals with bar scope). Anything it cannot represent is dropped
// with a warning instead of failing the whole score.
#include "../../platform/resources/MidiClip.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tri::abc {

constexpr size_t maxTextBytes = 1024 * 1024;
// smf::maxEvents is 32768 and every note costs two events, so leave headroom.
constexpr size_t maxNotes = 8000;
constexpr double maxLengthBeats = 65000.0;

struct Note {
    double beat = 0.0;     // quarter notes from the start of the clip
    double beats = 0.0;    // duration in quarter notes
    int pitch = 60;
    int channel = 0;
    int velocity = 100;
};

struct Score {
    std::vector<Note> notes;
    double bpm = 120.0;
    double lengthBeats = 0.0;
    std::string title, key, meter;
    int voiceCount = 1;
    std::vector<std::string> warnings;
};

namespace detail {

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
inline bool isNoteLetter(char c) { return (c >= 'A' && c <= 'G') || (c >= 'a' && c <= 'g'); }

inline std::string trim(const std::string& text) {
    size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

inline int letterPitchClass(char c) {
    switch (c >= 'a' ? char(c - 'a' + 'A') : c) {
        case 'C': return 0;
        case 'D': return 2;
        case 'E': return 4;
        case 'F': return 5;
        case 'G': return 7;
        case 'A': return 9;
        default: return 11;   // B
    }
}

// Position on the circle of fifths for a major key, indexed by pitch class.
inline int majorFifths(int pitchClass) {
    static const int table[12] = {0, 7, 2, -5, 4, -1, 6, 1, -4, 3, -3, 5};
    return table[((pitchClass % 12) + 12) % 12];
}

struct KeySignature {
    int accidental[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // +1 sharp, -1 flat
};

inline KeySignature keySignature(const std::string& value) {
    KeySignature signature;
    size_t index = 0;
    while (index < value.size() && !std::isalpha(static_cast<unsigned char>(value[index]))) ++index;
    if (index >= value.size()) return signature;
    if (value.compare(index, 4, "none") == 0) return signature;

    int pitchClass = letterPitchClass(value[index]);
    ++index;
    if (index < value.size() && value[index] == '#') {
        pitchClass = (pitchClass + 1) % 12;
        ++index;
    } else if (index < value.size() && value[index] == 'b') {
        pitchClass = (pitchClass + 11) % 12;
        ++index;
    }
    std::string mode;
    while (index < value.size() && std::isalpha(static_cast<unsigned char>(value[index])))
        mode += char(std::tolower(static_cast<unsigned char>(value[index++])));

    // A mode shares the signature of the major key a mode-step above its root.
    int offset = 0;
    if (mode == "m" || mode == "min" || mode == "minor" || mode == "aeo" || mode == "aeolian") offset = 3;
    else if (mode == "mix" || mode == "mixolydian") offset = 5;
    else if (mode == "dor" || mode == "dorian") offset = 10;
    else if (mode == "phr" || mode == "phrygian") offset = 8;
    else if (mode == "lyd" || mode == "lydian") offset = 7;
    else if (mode == "loc" || mode == "locrian") offset = 1;

    static const int sharps[7] = {5, 0, 7, 2, 9, 4, 11};   // F C G D A E B
    static const int flats[7] = {11, 4, 9, 2, 7, 0, 5};    // B E A D G C F
    const int fifths = majorFifths((pitchClass + offset) % 12);
    if (fifths > 0) for (int i = 0; i < fifths && i < 7; ++i) signature.accidental[sharps[i]] = 1;
    else for (int i = 0; i < -fifths && i < 7; ++i) signature.accidental[flats[i]] = -1;
    return signature;
}

// A single note head: accidentals, the letter, and octave marks. Duration is a
// separate token so chords can share it.
struct Head { int pitch = 60; bool explicitAccidental = false; int accidental = 0; };

class Reader {
public:
    Reader(const std::string& text, Score& score) : text_(text), score_(score) {}

    bool run(std::string& why) {
        size_t cursor = 0;
        int line = 0;
        const size_t lineCount = countLines();
        while (cursor <= text_.size() && line < lineCount + 1) {
            const size_t end = text_.find('\n', cursor);
            std::string current = text_.substr(cursor, end == std::string::npos ? std::string::npos : end - cursor);
            ++line;
            if (end == std::string::npos) cursor = text_.size() + 1;
            else cursor = end + 1;

            const size_t comment = current.find('%');
            if (comment != std::string::npos) current = current.substr(0, comment);
            if (!current.empty() && current.back() == '\r') current.pop_back();
            if (trim(current).empty()) continue;

            if (current.size() >= 2 && std::isalpha(static_cast<unsigned char>(current[0])) && current[1] == ':') {
                const char code = char(std::toupper(static_cast<unsigned char>(current[0])));
                // A second X: starts a second tune; this reader handles one.
                if (code == 'X' && bodySeen_) break;
                if (!field(code, trim(current.substr(2)), line, why)) return false;
                if (code == 'K') bodySeen_ = true;
                continue;
            }
            if (!bodySeen_) {
                why = where(line) + "K: 音调行之前出现音符，这不是有效的 ABC。";
                return false;
            }
            if (!body(current, line, why)) return false;
        }
        return finish(why);
    }

private:
    size_t countLines() const {
        size_t count = 0;
        for (char c : text_) if (c == '\n') ++count;
        return count + 1;
    }
    std::string where(int line) const { return "第 " + std::to_string(line) + " 行："; }

    bool field(char code, const std::string& value, int line, std::string& why) {
        switch (code) {
            case 'T':
                if (score_.title.empty()) score_.title = value;
                break;
            case 'M': meter(value); break;
            case 'L': unitLength(value); break;
            case 'Q': tempo(value); break;
            case 'K':
                key_ = keySignature(value);
                score_.key = value;
                barAccidentals_.clear();
                break;
            case 'V': voice(value); break;
            case 'W': break;
            default: break;
        }
        (void)line;
        (void)why;
        return true;
    }

    void meter(const std::string& value) {
        const std::string text = trim(value);
        if (text == "C") { meterNum_ = 4; meterDen_ = 4; return; }
        if (text == "C|") { meterNum_ = 2; meterDen_ = 2; return; }
        if (text.rfind("none", 0) == 0) { meterNum_ = 4; meterDen_ = 4; return; }
        const size_t slash = text.find('/');
        if (slash == std::string::npos) return;
        const int numerator = std::atoi(text.substr(0, slash).c_str());
        const int denominator = std::atoi(text.substr(slash + 1).c_str());
        if (numerator > 0 && numerator <= 64 && denominator > 0 && denominator <= 64) {
            meterNum_ = numerator;
            meterDen_ = denominator;
        }
    }

    void unitLength(const std::string& value) {
        const std::string text = trim(value);
        const size_t slash = text.find('/');
        if (slash == std::string::npos) return;
        const int numerator = std::atoi(text.substr(0, slash).c_str());
        const int denominator = std::atoi(text.substr(slash + 1).c_str());
        if (numerator > 0 && numerator <= 64 && denominator > 0 && denominator <= 128) {
            unitNum_ = numerator;
            unitDen_ = denominator;
        }
    }

    void tempo(const std::string& value) {
        const std::string text = trim(value);
        size_t index = 0;
        while (index < text.size() && !isDigit(text[index])) ++index;
        if (index >= text.size()) return;
        std::string unit;
        while (index < text.size() && (isDigit(text[index]) || text[index] == '/')) unit += text[index++];
        std::string number;
        if (index < text.size() && text[index] == '=') {
            ++index;
            while (index < text.size() && (isDigit(text[index]) || text[index] == '.')) number += text[index++];
        }
        if (number.empty()) {   // "Q:120" — 120 quarter notes per minute.
            const double plain = std::atof(unit.c_str());
            if (plain > 0) score_.bpm = plain;
            return;
        }
        // "Q:1/2=60" means 60 half notes per minute, i.e. 120 quarter notes.
        double fraction = 0.25;
        const size_t slash = unit.find('/');
        if (slash != std::string::npos) {
            const double numerator = std::atof(unit.substr(0, slash).c_str());
            const double denominator = std::atof(unit.substr(slash + 1).c_str());
            if (numerator > 0 && denominator > 0) fraction = numerator / denominator;
        } else if (!unit.empty()) {
            const double beats = std::atof(unit.c_str());
            if (beats > 0) fraction = 1.0 / beats;
        }
        const double bpm = std::atof(number.c_str()) * fraction * 4.0;
        if (bpm > 0) score_.bpm = bpm;
    }

    void voice(const std::string& value) {
        const std::string text = trim(value);
        size_t end = 0;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) && text[end] != '=') ++end;
        const std::string id = text.substr(0, end);
        const std::string name = id.empty() ? std::to_string(voices_.size() + 1) : id;
        auto it = voices_.find(name);
        if (it == voices_.end()) {
            // The first declared voice claims the implicit voice, so an
            // undeclared score and a `V:1`-only score behave identically.
            int index = 0;
            if (voices_.empty() && cursors_.size() == 1) {
                index = 0;
            } else {
                index = int(cursors_.size());
                cursors_.push_back(0.0);
            }
            voices_[name] = index;
            active_ = index;
            score_.voiceCount = int(cursors_.size());
        } else {
            active_ = it->second;
        }
    }

    double beatsPerUnit() const { return double(unitNum_) / double(unitDen_) * 4.0; }
    double barBeats() const { return double(meterNum_) * 4.0 / double(meterDen_); }

    bool body(const std::string& line, int lineNumber, std::string& why) {
        size_t index = 0;
        while (index < line.size()) {
            const char c = line[index];
            if (std::isspace(static_cast<unsigned char>(c)) || c == '\\') { ++index; continue; }
            if (c == '%') break;
            if (c == '"') { index = skipTo(line, index + 1, '"'); continue; }
            if (c == '{') { index = skipTo(line, index + 1, '}'); continue; }   // grace notes
            if (c == '!' || c == '+') { index = skipTo(line, index + 1, c); continue; }   // decorations
            if (c == '[') {
                if (index + 2 < line.size() && std::isalpha(static_cast<unsigned char>(line[index + 1])) &&
                    line[index + 2] == ':') {
                    const size_t close = line.find(']', index);
                    const std::string inner = line.substr(index + 1,
                        (close == std::string::npos ? line.size() : close) - index - 1);
                    field(char(std::toupper(static_cast<unsigned char>(inner[0]))), trim(inner.substr(2)),
                          lineNumber, why);
                    index = close == std::string::npos ? line.size() : close + 1;
                    continue;
                }
                if (index + 1 < line.size() && isDigit(line[index + 1])) {   // [1 [2 alternative endings
                    while (index < line.size() && (isDigit(line[index]) || line[index] == '[' ||
                                                   line[index] == '-' || line[index] == ',')) ++index;
                    resetBar();
                    continue;
                }
                chord(line, index);
                continue;            }
            if (c == '|' || c == ':' || c == ']') { ++index; resetBar(); continue; }
            if (c == '(') {
                if (index + 1 < line.size() && isDigit(line[index + 1])) tuplet(line, index);
                else ++index;
                continue;
            }
            if (c == ')' || c == '&' || c == '.' || c == '~' || c == 'u' || c == 'v') { ++index; continue; }
            if (c == '-') { tieFrom_ = lastGroup_.size() == 1 ? long(lastGroup_[0]) : -1; ++index; continue; }
            if (c == 'z' || c == 'x') { rest(line, index); continue; }
            if (c == 'Z') { ++index; cursors_[active_] += barBeats(); finishToken(); continue; }
            if (isNoteLetter(c) || c == '^' || c == '_' || c == '=') {
                single(line, index);
                continue;
            }
            ++index;   // annotation we do not model
        }
        return true;
    }

    static size_t skipTo(const std::string& line, size_t from, char close) {
        const size_t at = line.find(close, from);
        return at == std::string::npos ? line.size() : at + 1;
    }

    bool head(const std::string& line, size_t& index, Head& out) {
        int accidental = 0;
        bool explicitAccidental = false;
        while (index < line.size()) {
            const char c = line[index];
            if (c == '^') { accidental += 1; explicitAccidental = true; ++index; }
            else if (c == '_') { accidental -= 1; explicitAccidental = true; ++index; }
            else if (c == '=') { accidental = 0; explicitAccidental = true; ++index; }
            else break;
        }
        if (index >= line.size() || !isNoteLetter(line[index])) return false;
        const char letter = line[index++];
        int base = (letter >= 'a') ? 72 : 60;
        base += letterPitchClass(letter);
        while (index < line.size() && (line[index] == ',' || line[index] == '\'')) {
            base += line[index] == ',' ? -12 : 12;
            ++index;
        }
        const int pitchClass = ((base % 12) + 12) % 12;
        if (!explicitAccidental) {
            auto it = barAccidentals_.find(base);
            accidental = it != barAccidentals_.end() ? it->second : key_.accidental[pitchClass];
        } else {
            barAccidentals_[base] = accidental;
        }
        out.pitch = std::clamp(base + accidental, 0, 127);
        out.explicitAccidental = explicitAccidental;
        out.accidental = accidental;
        return true;
    }

    double durationMultiplier(const std::string& line, size_t& index) {
        double multiplier = 1.0;
        if (index < line.size() && isDigit(line[index])) {
            int count = 0;
            while (index < line.size() && isDigit(line[index])) count = count * 10 + (line[index++] - '0');
            if (count > 0) multiplier = double(count);
            if (index < line.size() && line[index] == '/') {
                ++index;
                if (index < line.size() && isDigit(line[index])) {
                    int divisor = 0;
                    while (index < line.size() && isDigit(line[index])) divisor = divisor * 10 + (line[index++] - '0');
                    if (divisor > 0) multiplier /= double(divisor);
                } else {
                    multiplier /= 2.0;
                }
            }
        } else if (index < line.size() && line[index] == '/') {
            int slashes = 0;
            while (index < line.size() && line[index] == '/') { ++index; ++slashes; }
            if (index < line.size() && isDigit(line[index])) {
                int divisor = 0;
                while (index < line.size() && isDigit(line[index])) divisor = divisor * 10 + (line[index++] - '0');
                if (divisor > 0) multiplier = 1.0 / double(divisor);
            } else {
                multiplier = 1.0 / double(1 << std::min(slashes, 10));
            }
        }
        return multiplier;
    }

    // `a>b` lengthens the first note and shortens the second.
    void broken(const std::string& line, size_t& index, double& previous, double& next) {
        previous = 1.0;
        next = 1.0;
        int longer = 0, shorter = 0;
        while (index < line.size() && (line[index] == '>' || line[index] == '<')) {
            if (line[index] == '>') ++longer;
            else ++shorter;
            ++index;
        }
        if (longer) {
            next = 1.0 / double(1 << std::min(longer, 8));
            previous = 2.0 - next;
        } else if (shorter) {
            previous = 1.0 / double(1 << std::min(shorter, 8));
            next = 2.0 - previous;
        }
    }

    double scaled(double units) {
        double beats = units * beatsPerUnit();
        if (tupletRemaining_ > 0) { beats *= tupletMultiplier_; --tupletRemaining_; }
        beats *= pendingNext_;
        pendingNext_ = 1.0;
        return beats;
    }

    void emit(double start, double beats, int pitch) {
        if (beats <= 0.0) return;
        Note note;
        note.beat = start;
        note.beats = beats;
        note.pitch = pitch;
        note.channel = active_ == 0 ? 0 : 1;   // first voice on channel 1, the rest merged
        score_.notes.push_back(note);
    }

    // A single unreadable token is skipped with a warning: one stray decoration
    // must not cost the user the whole generated score.
    void single(const std::string& line, size_t& index) {
        Head note;
        if (!head(line, index, note)) {
            ++skipped_;
            if (index < line.size()) ++index;
            return;
        }
        const double beats = scaled(durationMultiplier(line, index));
        double previous = 1.0, next = 1.0;
        broken(line, index, previous, next);
        const double duration = beats * previous;
        pendingNext_ = next;
        if (tieFrom_ >= 0 && size_t(tieFrom_) < score_.notes.size() &&
            score_.notes[size_t(tieFrom_)].pitch == note.pitch) {
            score_.notes[size_t(tieFrom_)].beats += duration;
            lastGroup_.assign(1, size_t(tieFrom_));
        } else {
            emit(cursors_[active_], duration, note.pitch);
            if (!score_.notes.empty()) lastGroup_.assign(1, score_.notes.size() - 1);
        }
        tieFrom_ = -1;
        cursors_[active_] += duration;
    }

    void chord(const std::string& line, size_t& index) {
        ++index;   // '['
        std::vector<int> pitches;
        while (index < line.size() && line[index] != ']') {
            if (std::isspace(static_cast<unsigned char>(line[index]))) { ++index; continue; }
            Head note;
            if (!head(line, index, note)) {
                ++skipped_;
                if (index < line.size()) ++index;
                continue;
            }
            pitches.push_back(note.pitch);
        }
        if (index < line.size()) ++index;   // ']'
        const double beats = scaled(durationMultiplier(line, index));
        double previous = 1.0, next = 1.0;
        broken(line, index, previous, next);
        const double duration = beats * previous;
        pendingNext_ = next;
        lastGroup_.clear();
        for (int pitch : pitches) {
            emit(cursors_[active_], duration, pitch);
            if (!score_.notes.empty()) lastGroup_.push_back(score_.notes.size() - 1);
        }
        tieFrom_ = -1;
        cursors_[active_] += duration;
    }

    void rest(const std::string& line, size_t& index) {
        ++index;   // 'z' or 'x'
        const double beats = scaled(durationMultiplier(line, index));
        double previous = 1.0, next = 1.0;
        broken(line, index, previous, next);
        cursors_[active_] += beats * previous;
        pendingNext_ = next;
        finishToken();
    }

    void tuplet(const std::string& line, size_t& index) {
        ++index;   // '('
        int count = 0;
        while (index < line.size() && isDigit(line[index])) count = count * 10 + (line[index++] - '0');
        int inTheTimeOf = 2;
        int played = count;
        if (index < line.size() && line[index] == ':') {
            ++index;
            int value = 0;
            while (index < line.size() && isDigit(line[index])) value = value * 10 + (line[index++] - '0');
            if (value > 0) inTheTimeOf = value;
            if (index < line.size() && line[index] == ':') {
                ++index;
                value = 0;
                while (index < line.size() && isDigit(line[index])) value = value * 10 + (line[index++] - '0');
                if (value > 0) played = value;
            }
        }
        if (count <= 0 || played <= 0) return;
        tupletRemaining_ = played;
        tupletMultiplier_ = double(inTheTimeOf) / double(played);
    }

    void finishToken() {
        lastGroup_.clear();
        tieFrom_ = -1;
    }
    void resetBar() {
        barAccidentals_.clear();
        finishToken();
    }

    bool finish(std::string& why) {
        if (!bodySeen_) {
            why = "缺少 K: 音调行（ABC 头部必须以 K: 结束）。";
            return false;
        }
        if (score_.notes.empty()) {
            why = "乐谱中没有音符。";
            return false;
        }
        if (score_.notes.size() > maxNotes) {
            why = "乐谱音符过多（上限 " + std::to_string(maxNotes) + "）。";
            return false;
        }
        double length = 0.0;
        for (const Note& note : score_.notes) length = std::max(length, note.beat + note.beats);
        for (double cursor : cursors_) length = std::max(length, cursor);
        if (length > maxLengthBeats) {
            why = "乐谱过长（上限 " + std::to_string(int(maxLengthBeats)) + " 拍）。";
            return false;
        }
        if (skipped_ > 0)
            score_.warnings.push_back("跳过了 " + std::to_string(skipped_) + " 个无法识别的记号。");
        score_.lengthBeats = length;
        score_.bpm = std::clamp(score_.bpm, 20.0, 400.0);
        score_.meter = std::to_string(meterNum_) + "/" + std::to_string(meterDen_);
        score_.voiceCount = int(std::max<size_t>(1, voices_.size()));
        return true;
    }

    const std::string& text_;
    Score& score_;
    KeySignature key_;
    std::map<int, int> barAccidentals_;   // pitch -> accidental, cleared at every barline
    std::map<std::string, int> voices_;
    std::vector<double> cursors_{0.0};
    int active_ = 0;
    int meterNum_ = 4, meterDen_ = 4;
    int unitNum_ = 1, unitDen_ = 8;
    int tupletRemaining_ = 0;
    double tupletMultiplier_ = 1.0;
    double pendingNext_ = 1.0;
    long tieFrom_ = -1;
    std::vector<size_t> lastGroup_;
    bool bodySeen_ = false;
    int skipped_ = 0;
};

} // namespace detail

inline MidiClip toClip(const Score& score) {
    MidiClip clip;
    clip.lengthBeats = std::max(0.0, score.lengthBeats);
    clip.events.reserve(score.notes.size() * 2);
    for (const Note& note : score.notes) {
        const int channel = std::clamp(note.channel, 0, 15);
        const int pitch = std::clamp(note.pitch, 0, 127);
        const int velocity = std::clamp(note.velocity, 1, 127);
        clip.events.push_back({std::max(0.0, note.beat), uint8_t(0x90 | channel), uint8_t(pitch), uint8_t(velocity)});
        clip.events.push_back({std::max(0.0, note.beat) + std::max(0.0, note.beats), uint8_t(0x80 | channel),
                               uint8_t(pitch), 0});
    }
    return clip;
}

// Parses ABC text into note events. On failure `why` carries a line-numbered
// reason; the caller shows it verbatim.
inline bool parse(const std::string& text, Score& score, std::string& why) {
    score = Score();
    if (text.empty()) { why = "乐谱文本为空。"; return false; }
    if (text.size() > maxTextBytes) { why = "乐谱文本过大（上限 1 MB）。"; return false; }
    detail::Reader reader(text, score);
    if (!reader.run(why)) return false;
    if (score.notes.size() > maxNotes) { why = "乐谱音符过多。"; return false; }
    return true;
}

inline bool parseClip(const std::string& text, MidiClip& clip, Score& score, std::string& why) {
    if (!parse(text, score, why)) return false;
    clip = toClip(score);
    if (clip.events.empty()) { why = "乐谱中没有音符。"; return false; }
    return true;
}

} // namespace tri::abc
