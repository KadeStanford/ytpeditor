#include "model/project.h"
#include "timeline/effects_editor.h"
#include "timeline/timeline_editor.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

using namespace ytp;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Project project{"30-second infomercial sentence mix"};
    MediaAsset media{.id=createId(),.path="dogfood-infomercial.mp4",.displayName="Classic kitchen infomercial",.duration=Rational{300,1},.frameRateNumerator=30000,.frameRateDenominator=1001,.width=1920,.height=1080,.audioSampleRate=48000}; project.addMediaAsset(media);
    for(int index=0;index<500;++index) {
        const auto start=Rational{index%290,1};
        std::vector<std::string> tags{index%2?"dialogue":"reaction"}; if(index%5==0)tags.push_back("favorite");
        project.addLibraryClip(LibraryClip{.id=createId(),.mediaAssetId=media.id,.sourceRange=TimeRange{start,Rational{1,2}},.name="phrase "+std::to_string(index),.tags=std::move(tags),.thumbnailTime=start});
    }
    auto sequence=project.sequences().front(); sequence.rippleMode=RippleMode::AllTracks;
    QElapsedTimer timer; timer.start();
    // Recreate a dense 30-second YTP sentence mix: 60 half-second cuts with repeated source fragments.
    for(int index=0;index<60;++index) {
        const auto& clip=project.libraryClips()[static_cast<std::size_t>((index*17)%500)];
        const auto inserted=TimelineEditor::insertLibraryClip(project,sequence,clip.id,sequence.tracks[1].id,Rational{index,2},EditMode::Overwrite);
        if(index%7==0) for(const auto& id:inserted.itemIds) TimelineEditor::setItemReverse(sequence,id,true);
        if(index%9==0) (void)EffectsEditor::addEffect(sequence,EffectTarget::Item,inserted.itemIds.front(),"pixelate");
    }
    const auto buildMs=timer.elapsed(); assert((sequence.duration()==Rational{30,1}));
    timer.restart();
    auto selected=sequence.tracks[1].items.front(); std::vector<TimelineItem> copies;
    for(int index=0;index<20;++index) { auto copy=selected; copy.id=createId(); copy.timelineStart=Rational{31+index,1}; copy.speed=1.0+index*.15; const auto ratio=Rational{static_cast<std::int64_t>(std::llround(copy.speed*1'000'000.0)),1'000'000};copy.duration=copy.sourceRange.duration()/ratio;copy.pitchSemitones=index; copies.push_back(copy); }
    const auto pasted=TimelineEditor::pasteItems(sequence,copies,Rational{31,1});
    const auto speedGagMs=timer.elapsed(); assert(pasted.size()==20);
    timer.restart();
    const auto middleId=sequence.tracks[1].items[20].id; TimelineEditor::deleteItems(sequence,{middleId});
    const auto rippleDeleteMs=timer.elapsed();
    timer.restart();
    const auto found=std::count_if(project.libraryClips().begin(),project.libraryClips().end(),[](const auto& clip){return clip.name.find("phrase 42")!=std::string::npos||std::find(clip.tags.begin(),clip.tags.end(),"reaction")!=clip.tags.end();});
    const auto searchMs=timer.elapsed(); assert(found>0); assert(!sequence.validate());
    std::cout << "Dogfood timing (ms): 60-cut sentence mix="<<buildMs<<", 20-copy speed/pitch gag="<<speedGagMs<<", all-track ripple delete="<<rippleDeleteMs<<", 500-clip retrieval="<<searchMs<<"\n";
    assert(buildMs<2000&&speedGagMs<500&&rippleDeleteMs<500&&searchMs<250);
    return 0;
}
