#pragma once
#include <array>
#include <string>
#include <vector>
#include <cctype>
#include <optional>
namespace tri {
struct Chord { int root=60,quality=0; };
inline constexpr const char* chordNames[]={"Major","Minor","7","Maj7","Min7","Dim","Sus2","Sus4"};
inline constexpr int chordIntervals[8][4]={{0,4,7,-1},{0,3,7,-1},{0,4,7,10},{0,4,7,11},{0,3,7,10},{0,3,6,-1},{0,2,7,-1},{0,5,7,-1}};
inline std::optional<Chord> parseChord(std::string text) {
    if(text.empty()) return std::nullopt;
    int roots[]={9,11,0,2,4,5,7};
    char first=std::toupper(static_cast<unsigned char>(text[0]));
    if(first<'A'||first>'G') return std::nullopt;
    int pitch=roots[first-'A'], i=1;
    if(i<(int)text.size()&&(text[i]=='#'||text[i]=='b')) pitch+=text[i++]=='#'?1:-1;
    std::string suffix=text.substr(i);
    int q=-1;
    if(suffix.empty()||suffix=="maj")q=0;
    if(suffix=="m"||suffix=="min")q=1;
    if(suffix=="7")q=2;
    if(suffix=="maj7"||suffix=="M7")q=3;
    if(suffix=="m7"||suffix=="min7")q=4;
    if(suffix=="dim")q=5;
    if(suffix=="sus2")q=6;
    if(suffix=="sus4")q=7;
    if(q<0)return std::nullopt;
    return Chord{60+(pitch+12)%12,q};
}
inline std::optional<std::vector<Chord>> parseProgression(const std::string& text) {
    std::vector<Chord> result;std::string token;
    auto append=[&](){if(token.empty())return true;auto chord=parseChord(token);token.clear();if(!chord)return false;result.push_back(*chord);return result.size()<=8;};
    for(unsigned char c:text) {
        if(c=='|'||c==','||std::isspace(c)){if(!append())return std::nullopt;}
        else token.push_back(c);
    }
    if(!append()||result.empty())return std::nullopt;
    return result;
}
}
