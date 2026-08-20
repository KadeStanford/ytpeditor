#include "ytp/remix_toolkit.h"
#include "timeline/timeline_editor.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace ytp { namespace {
std::vector<std::string> tokens(std::string_view text){std::vector<std::string> out;std::string token;for(unsigned char c:text){if(std::isalnum(c)||c=='\'')token.push_back(static_cast<char>(std::tolower(c)));else if(!token.empty()){out.push_back(token);token.clear();}}if(!token.empty())out.push_back(token);return out;}
Rational ratio(double value){return Rational{static_cast<std::int64_t>(std::llround(value*1'000'000)),1'000'000};}
}
std::string RemixToolkit::phoneticCode(std::string_view text){auto words=tokens(text);if(words.empty())return{};const auto&word=words.front();std::string result(1,static_cast<char>(std::toupper(static_cast<unsigned char>(word.front()))));const auto code=[](char c){c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));if(std::string_view("bfpv").find(c)!=std::string_view::npos)return'1';if(std::string_view("cgjkqsxz").find(c)!=std::string_view::npos)return'2';if(std::string_view("dt").find(c)!=std::string_view::npos)return'3';if(c=='l')return'4';if(c=='m'||c=='n')return'5';if(c=='r')return'6';return'0';};char previous=code(word.front());for(std::size_t i=1;i<word.size()&&result.size()<4;++i){const char current=code(word[i]);if(current!='0'&&current!=previous)result.push_back(current);previous=current;}while(result.size()<4)result.push_back('0');return result;}
std::vector<WordMatch> RemixToolkit::searchWords(const Project& project,std::string_view query,bool phonetic,std::size_t limit){std::vector<WordMatch> result;if(limit==0)return result;const auto sought=tokens(query);if(sought.empty())return result;const auto queryCode=phoneticCode(sought.front());for(const auto&media:project.mediaAssets())for(const auto&entry:media.transcript){const auto parts=tokens(entry.text);if(parts.empty())continue;const auto partDuration=entry.duration/Rational{static_cast<std::int64_t>(parts.size()),1};for(std::size_t i=0;i<parts.size();++i){const bool exact=parts[i].find(sought.front())!=std::string::npos;const bool sounds=phonetic&&phoneticCode(parts[i])==queryCode;if(!exact&&!sounds)continue;result.push_back({.mediaAssetId=media.id,.range=TimeRange{entry.start+partDuration*Rational{static_cast<std::int64_t>(i),1},partDuration},.text=parts[i],.score=exact?1.0:.65});if(result.size()>=limit)return result;}}return result;}
SentenceBuild RemixToolkit::sentenceSequence(const Project& project,
                                              const std::vector<WordMatch>& words,
                                              std::string name, Rational padding,
                                              Rational crossfade, bool captions) {
    if (words.empty()||padding<Rational{}||crossfade<Rational{}) throw std::invalid_argument("sentence settings are invalid");
    auto sequence = createDefaultSequence();
    sequence.name = name.empty() ? "Sentence" : std::move(name);
    for (auto& track : sequence.tracks) track.items.clear();
    Rational cursor;
    SentenceBuild build;
    for (const auto& word : words) {
        const auto* media = project.findMediaAsset(word.mediaAssetId);
        if (!media) throw std::invalid_argument("sentence word media missing");
        const auto start = std::max(Rational{}, word.range.start() - padding);
        const auto end = std::min(media->duration, word.range.end() + padding);
        if (end <= start) continue;
        const auto duration = end - start;
        std::vector<Track*> targets;
        if(media->width>0&&media->height>0){auto video=std::find_if(sequence.tracks.begin(),sequence.tracks.end(),[](const auto&track){return track.kind==TrackKind::Video&&track.name=="V1";});if(video!=sequence.tracks.end())targets.push_back(&*video);}
        if(media->audioSampleRate>0){auto audio=std::find_if(sequence.tracks.begin(),sequence.tracks.end(),[](const auto&track){return track.kind==TrackKind::Audio&&track.name=="A1";});if(audio!=sequence.tracks.end())targets.push_back(&*audio);}
        if(targets.empty())continue;
        const auto group = targets.size()>1?createId():Id{};
        const auto overlap=std::min(crossfade,duration/Rational{2,1});
        for (auto* trackPointer : targets) {
            auto&track=*trackPointer;
            TimelineItem item{
                .id = createId(),
                .libraryClipId = {},
                .mediaAssetId = media->id,
                .nestedSequenceId = {},
                .adjustmentClip = false,
                .trackId = track.id,
                .timelineStart = cursor,
                .sourceRange = TimeRange{start, duration},
                .duration = duration,
                .linkedGroupId = group,
                .groupId = {},
                .fadeIn = overlap,
                .fadeOut = overlap,
                .transform = {},
                .audio = {},
                .speed = 1.0,
                .pitchSemitones = 0.0,
                .preservePitch = true,
                .reverse = false,
                .freezeFrame = false,
                .freezeSourceTime = {},
                .effects = {},
                .masks = {},
                .captionEnabled = false,
                .captionText = {},
                .captionSize = 54.0,
                .captionColor = "white",
                .name = {}
            };
            if (track.kind == TrackKind::Video && captions) {
                item.captionEnabled = true;
                item.captionText = word.text;
            }
            track.items.push_back(item);
            build.itemIds.push_back(item.id);
        }
        cursor = cursor + duration - overlap;
    }
    build.sequence = std::move(sequence);
    return build;
}
BeatGrid RemixToolkit::estimateBeatGrid(const std::vector<Rational>& onsets,int division){if(onsets.size()<2)throw std::invalid_argument("at least two onsets required");auto sorted=onsets;std::sort(sorted.begin(),sorted.end());sorted.erase(std::unique(sorted.begin(),sorted.end()),sorted.end());std::vector<double> gaps;for(std::size_t i=1;i<sorted.size();++i){const auto gap=static_cast<double>((sorted[i]-sorted[i-1]).asLongDouble());if(gap>.12&&gap<3)gaps.push_back(gap);}if(gaps.empty())throw std::invalid_argument("onsets have no tempo");std::sort(gaps.begin(),gaps.end());double seconds=gaps[gaps.size()/2];double bpm=60.0/seconds;while(bpm<60)bpm*=2;while(bpm>200)bpm/=2;return {.enabled=true,.bpm=bpm,.offset=sorted.front(),.division=std::clamp(division,1,64)};}
std::vector<Rational> RemixToolkit::beatTimes(const BeatGrid& grid,Rational end){std::vector<Rational> result;if(!grid.enabled||!std::isfinite(grid.bpm)||grid.bpm<=0||grid.division<1||end<=Rational{})return result;const auto step=ratio(60.0/grid.bpm*4.0/grid.division);if(step<=Rational{})return result;for(auto time=grid.offset;time<end;time=time+step)if(time>=Rational{})result.push_back(time);return result;}
void RemixToolkit::snapItemsToBeats(Sequence& sequence,const std::vector<Id>& ids){const auto beats=beatTimes(sequence.beatGrid,sequence.duration()+Rational{60,1});if(beats.empty())throw std::invalid_argument("beat grid disabled");for(const auto&id:ids)if(auto*item=sequence.findItem(id)){const auto*track=sequence.findTrack(item->trackId);if(!track||track->locked)throw std::invalid_argument("a selected track is locked");const auto nearest=std::min_element(beats.begin(),beats.end(),[&](const auto&a,const auto&b){return std::abs(static_cast<double>((a-item->timelineStart).asLongDouble()))<std::abs(static_cast<double>((b-item->timelineStart).asLongDouble()));});item->timelineStart=*nearest;}}
std::vector<Id> RemixToolkit::cutItemsToBeats(Sequence& sequence,const std::vector<Id>& ids){auto selected=ids;const auto beats=beatTimes(sequence.beatGrid,sequence.duration());for(const auto&beat:beats){std::vector<Id> current=selected;for(const auto&id:current)if(const auto*item=sequence.findItem(id);item&&beat>item->timelineStart&&beat<item->timelineEnd()){const auto split=TimelineEditor::splitItems(sequence,{id},beat);selected.insert(selected.end(),split.begin(),split.end());}}std::sort(selected.begin(),selected.end());selected.erase(std::unique(selected.begin(),selected.end()),selected.end());return selected;}
void RemixToolkit::addAudioReactiveKeys(Sequence& sequence,const std::vector<Id>& ids,std::string_view effectType,std::string_view parameterName,double low,double high){const auto beats=beatTimes(sequence.beatGrid,sequence.duration());const auto*descriptor=findEffectDescriptor(effectType);if(!descriptor)throw std::invalid_argument("effect type missing");bool applied=false;for(const auto&id:ids){auto*item=sequence.findItem(id);if(!item)continue;const auto*track=sequence.findTrack(item->trackId);if(!track||track->locked)throw std::invalid_argument("a selected track is locked");if(descriptor->audio!=(track->kind==TrackKind::Audio))continue;auto effect=createEffect(effectType);auto*parameter=findParameter(effect,parameterName);if(!parameter)throw std::invalid_argument("effect parameter missing");parameter->keyframes.clear();for(const auto&beat:beats)if(beat>=item->timelineStart&&beat<item->timelineEnd()){const auto local=beat-item->timelineStart;parameter->keyframes.push_back({.id=createId(),.time=local,.value=std::clamp(high,parameter->minimum,parameter->maximum),.interpolation=KeyframeInterpolation::Smooth});const auto settle=std::min(item->duration,local+Rational{1,10});if(settle>local)parameter->keyframes.push_back({.id=createId(),.time=settle,.value=std::clamp(low,parameter->minimum,parameter->maximum),.interpolation=KeyframeInterpolation::Smooth});}std::sort(parameter->keyframes.begin(),parameter->keyframes.end(),[](const auto&a,const auto&b){return a.time<b.time;});parameter->keyframes.erase(std::unique(parameter->keyframes.begin(),parameter->keyframes.end(),[](const auto&a,const auto&b){return a.time==b.time;}),parameter->keyframes.end());item->effects.push_back(std::move(effect));applied=true;}if(!applied)throw std::invalid_argument("effect is incompatible with selected tracks");}
} // namespace ytp
