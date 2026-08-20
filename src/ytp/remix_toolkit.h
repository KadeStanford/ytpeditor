#pragma once
#include "model/project.h"
#include <string>
#include <vector>

namespace ytp {
struct WordMatch final { Id mediaAssetId; TimeRange range; std::string text; double score{1}; friend bool operator==(const WordMatch&,const WordMatch&)=default; };
struct SentenceBuild final { Sequence sequence; std::vector<Id> itemIds; };
class RemixToolkit final {
public:
    static std::string phoneticCode(std::string_view text);
    static std::vector<WordMatch> searchWords(const Project& project,std::string_view query,bool phonetic,std::size_t limit=500);
    static SentenceBuild sentenceSequence(const Project& project,const std::vector<WordMatch>& words,std::string name,Rational padding,Rational crossfade,bool captions=true);
    static BeatGrid estimateBeatGrid(const std::vector<Rational>& onsets,int division=4);
    static std::vector<Rational> beatTimes(const BeatGrid& grid,Rational end);
    static void snapItemsToBeats(Sequence& sequence,const std::vector<Id>& ids);
    static std::vector<Id> cutItemsToBeats(Sequence& sequence,const std::vector<Id>& ids);
    static void addAudioReactiveKeys(Sequence& sequence,const std::vector<Id>& ids,std::string_view effectType,std::string_view parameter,double low,double high);
};
} // namespace ytp
