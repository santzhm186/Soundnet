// ABC -> MIDI clip conversion: musical semantics, diagnostics and the SMF
// round trip that puts a generated score into the piano roll.
#include "../src/AbcScore.hpp"
#include "../src/StandardMidiFile.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool value, const char* what) {
    if (!value) { std::printf("FAIL %s\n", what); ++failures; }
}
void near(double a, double b, const char* what) {
    if (std::fabs(a - b) > 1e-6) {
        std::printf("FAIL %s (%.6f != %.6f)\n", what, a, b);
        ++failures;
    }
}
bool parseOne(const std::string& text, tri::abc::Score& score) {
    std::string why;
    const bool ok = tri::abc::parse(text, score, why);
    if (!ok) std::printf("  reason: %s\n", why.c_str());
    return ok;
}
const std::string header =
    "X:1\nT:Demo\nM:4/4\nL:1/4\nQ:1/4=120\nK:C\n";
}  // namespace

int main() {
    // ---- plain melody ------------------------------------------------------
    {
        tri::abc::Score score;
        check(parseOne(header + "C D E F | G A B c |\n", score), "simple melody parses");
        check(score.notes.size() == 8, "eight notes");
        if (score.notes.size() == 8) {
            const int expected[8] = {60, 62, 64, 65, 67, 69, 71, 72};
            for (int i = 0; i < 8; ++i) check(score.notes[size_t(i)].pitch == expected[i], "diatonic pitch");
            near(score.notes[1].beat, 1.0, "quarter note spacing");
            near(score.lengthBeats, 8.0, "length in quarter notes");
        }
        near(score.bpm, 120.0, "tempo from Q:1/4=120");
        check(score.title == "Demo", "title captured");
        check(score.meter == "4/4", "meter captured");
    }
    // ---- key signature -----------------------------------------------------
    {
        tri::abc::Score score;
        check(parseOne("X:1\nM:4/4\nL:1/4\nK:D\nF C F C\n", score), "K:D parses");
        check(score.notes.size() == 4, "four notes");
        if (score.notes.size() == 4) {
            check(score.notes[0].pitch == 66, "F sharped by K:D");
            check(score.notes[1].pitch == 61, "C sharped by K:D");
        }
    }
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nK:Bb\nB E A D\n", score), "K:Bb parses");
        if (score.notes.size() == 4) {
            check(score.notes[0].pitch == 70, "B flat in K:Bb");
            check(score.notes[1].pitch == 63, "E flat in K:Bb");
        }
    }
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nK:Am\nC A\n", score), "K:Am parses");
        if (score.notes.size() == 2) {
            check(score.notes[0].pitch == 60, "C natural in A minor");
            check(score.notes[1].pitch == 69, "A in A minor");
        }
    }
    // ---- accidentals are bar scoped ---------------------------------------
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nK:C\n^F F | F _B B\n", score), "accidentals parse");
        check(score.notes.size() == 5, "five notes");
        if (score.notes.size() == 5) {
            check(score.notes[0].pitch == 66, "explicit sharp");
            check(score.notes[1].pitch == 66, "accidental persists inside the bar");
            check(score.notes[2].pitch == 65, "barline clears the accidental");
            check(score.notes[3].pitch == 70, "explicit flat");
            check(score.notes[4].pitch == 70, "flat persists inside the bar");
        }
    }
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nK:C\n=F ^^G __A\n", score), "natural and double accidentals parse");
        if (score.notes.size() == 3) {
            check(score.notes[0].pitch == 65, "= natural");
            check(score.notes[1].pitch == 69, "double sharp on G");
            check(score.notes[2].pitch == 67, "double flat on A");
        }
    }
    // ---- durations, rests, ties -------------------------------------------
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/8\nK:C\nC2 C/2 C3/2 C\n", score), "duration multipliers parse");
        if (score.notes.size() == 4) {
            near(score.notes[0].beats, 1.0, "2 units of 1/8 = 1 beat");
            near(score.notes[1].beats, 0.25, "/2 of a unit");
            near(score.notes[2].beats, 0.75, "3/2 of a unit");
            near(score.notes[3].beats, 0.5, "plain unit");
        }
    }
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nK:C\nC z C z2 C\n", score), "rests advance the cursor");
        if (score.notes.size() == 3) {
            near(score.notes[1].beat, 2.0, "rest of one beat");
            near(score.notes[2].beat, 5.0, "rest of two beats");
        }
    }
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nK:C\nC2-C2 D\n", score), "tie parses");
        check(score.notes.size() == 2, "tie merges two notes");
        if (score.notes.size() == 2) {
            near(score.notes[0].beats, 4.0, "tied duration is summed");
            near(score.notes[1].beat, 4.0, "next note follows the tie");
        }
    }
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nM:3/4\nK:C\nZ C\n", score), "whole bar rest parses");
        if (score.notes.size() == 1) near(score.notes[0].beat, 3.0, "Z is one bar of 3/4");
    }
    // ---- chords, tuplets, broken rhythm -----------------------------------
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nK:C\n[CEG]2 [FA]\n", score), "chords parse");
        check(score.notes.size() == 5, "chord expands into notes");
        if (score.notes.size() == 5) {
            check(score.notes[0].pitch == 60 && score.notes[1].pitch == 64 && score.notes[2].pitch == 67,
                  "chord pitches");
            near(score.notes[0].beats, 2.0, "chord duration");
            near(score.notes[3].beat, 2.0, "chord advances the cursor once");
        }
    }
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/8\nK:C\n(3CDE F\n", score), "tuplet parses");
        if (score.notes.size() == 4) {
            near(score.notes[0].beats, 1.0 / 3.0, "triplet eighth is a third of a beat");
            near(score.notes[3].beat, 1.0, "after three triplet notes");
        }
    }
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nK:C\nC>D E<F\n", score), "broken rhythm parses");
        if (score.notes.size() == 4) {
            near(score.notes[0].beats, 1.5, "dotted first note");
            near(score.notes[1].beats, 0.5, "halved second note");
            near(score.notes[1].beat, 1.5, "second note starts after the dotted note");
            near(score.notes[2].beats, 0.5, "reversed broken rhythm shortens first");
        }
    }
    // ---- octaves -----------------------------------------------------------
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nK:C\nC,, c c'\n", score), "octave marks parse");
        if (score.notes.size() == 3) {
            check(score.notes[0].pitch == 36, "two commas below middle C");
            check(score.notes[1].pitch == 72, "lower case is an octave up");
            check(score.notes[2].pitch == 84, "apostrophe is another octave");
        }
    }
    // ---- voices ------------------------------------------------------------
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nK:C\nV:1\nC D\nV:2\nE F\n", score), "two voices parse");
        check(score.notes.size() == 4, "both voices contribute");
        check(score.voiceCount == 2, "voice count");
        if (score.notes.size() == 4) {
            check(score.notes[0].channel == 0 && score.notes[1].channel == 0, "first voice on channel 1");
            check(score.notes[2].channel == 1 && score.notes[3].channel == 1, "second voice on channel 2");
            near(score.notes[2].beat, 0.0, "voices start together");
        }
    }
    // ---- tempo edge cases --------------------------------------------------
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nQ:1/2=60\nK:C\nC\n", score), "half-note tempo parses");
        near(score.bpm, 120.0, "60 half notes per minute is 120 quarter notes");
    }
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nQ:96\nK:C\nC\n", score), "bare tempo parses");
        near(score.bpm, 96.0, "bare Q: is quarter-note BPM");
    }
    // ---- inline fields -----------------------------------------------------
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nK:C\nF [K:D] F\n", score), "inline key change parses");
        if (score.notes.size() == 2) {
            check(score.notes[0].pitch == 65, "F natural under K:C");
            check(score.notes[1].pitch == 66, "F sharp after the inline change");
        }
    }
    // ---- tolerating decorations and annotations ---------------------------
    {
        tri::abc::Score score;
        check(parseOne("X:1\nL:1/4\nK:C\n\"Am\"C !trill!D {ef}E ~F\n", score), "decorations are skipped");
        check(score.notes.size() == 4, "decorations do not invent notes");
    }
    // ---- diagnostics -------------------------------------------------------
    {
        tri::abc::Score score;
        std::string why;
        check(!tri::abc::parse("X:1\nL:1/4\nC D\n", score, why), "missing K: is rejected");
        check(why.find("K:") != std::string::npos, "diagnostic mentions K:");
    }
    {
        tri::abc::Score score;
        std::string why;
        check(!tri::abc::parse(header + "z z z\n", score, why), "note-free score is rejected");
        check(!why.empty(), "empty score carries a reason");
    }
    {
        tri::abc::Score score;
        std::string why;
        check(!tri::abc::parse("", score, why), "empty text is rejected");
        check(!tri::abc::parse(std::string(tri::abc::maxTextBytes + 1, 'C'), score, why), "oversized text is rejected");
    }
    // ---- SMF round trip ----------------------------------------------------
    {
        tri::abc::Score score;
        tri::MidiClip clip;
        std::string why;
        check(tri::abc::parseClip(header + "C D E F | G A B c |\n", clip, score, why), "score converts to a clip");
        check(clip.events.size() == 16, "one on and one off event per note");
        check(clip.lengthBeats > 0, "clip has a length");
        std::vector<uint8_t> bytes = tri::smf::encodeClip(clip);
        tri::MidiClip reparsed = tri::smf::parse(bytes.data(), bytes.size());
        check(reparsed.events.size() == clip.events.size(), "clip survives the SMF round trip");
        near(reparsed.lengthBeats, clip.lengthBeats, "length survives the SMF round trip");
        size_t onsets = 0;
        for (const auto& event : reparsed.events) if ((event.status & 0xf0) == 0x90) ++onsets;
        check(onsets == score.notes.size(), "every note is written");
    }
    std::printf("AbcScoreTests %s\n", failures == 0 ? "passed" : "failed");
    return failures == 0 ? 0 : 1;
}
