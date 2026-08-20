#include "commands/timeline_commands.h"
#include "commands/command_stack.h"
#include "persistence/project_serializer.h"
#include "timeline/timeline_editor.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace ytp;
namespace {
Project sampleProject() {
    Project project{"Timeline Test"};
    MediaAsset media{.id=createId(), .path="sample.mp4", .displayName="Sample",
        .duration=Rational{30,1}, .frameRateNumerator=30, .frameRateDenominator=1,
        .width=1920, .height=1080, .audioSampleRate=48000};
    project.addMediaAsset(media);
    project.addLibraryClip(LibraryClip{.id=createId(), .mediaAssetId=media.id,
        .sourceRange=TimeRange{Rational{2,1},Rational{4,1}}, .name="Quote",
        .thumbnailTime=Rational{3,1}});
    return project;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    auto project = sampleProject();
    auto sequence = project.sequences().front();
    const auto clipId = project.libraryClips().front().id;
    const auto videoId = sequence.tracks[1].id;
    const auto audioId = sequence.tracks[2].id;

    sequence.rippleMode = RippleMode::AllTracks;
    auto first = TimelineEditor::insertLibraryClip(project, sequence, clipId, videoId,
        Rational{}, EditMode::Insert);
    assert(first.itemIds.size() == 2);
    auto second = TimelineEditor::insertLibraryClip(project, sequence, clipId, videoId,
        Rational{}, EditMode::Insert);
    assert((sequence.findItem(first.itemIds.front())->timelineStart == Rational{4,1}));
    assert((sequence.findItem(first.itemIds.back())->timelineStart == Rational{4,1}));
    assert(sequence.findItem(second.itemIds.front())->linkedGroupId ==
           sequence.findItem(second.itemIds.back())->linkedGroupId);

    sequence.tracks[3].locked = true;
    TimelineItem locked = *sequence.findItem(first.itemIds.back());
    locked.id=createId(); locked.trackId=sequence.tracks[3].id; locked.timelineStart=Rational{8,1};
    sequence.tracks[3].items.push_back(locked);
    (void)TimelineEditor::addMarker(sequence, Rational{8,1}, "beat");
    (void)TimelineEditor::insertLibraryClip(project, sequence, clipId, videoId,
        Rational{}, EditMode::Insert);
    assert((sequence.tracks[3].items.front().timelineStart == Rational{8,1}));
    assert((sequence.markers.front().time == Rational{12,1}));

    const auto splitTarget = second.itemIds.front();
    const auto splitAt = sequence.findItem(splitTarget)->timelineStart + Rational{2,1};
    auto right = TimelineEditor::splitItems(sequence, std::vector<Id>{splitTarget}, splitAt);
    assert(right.size() == 2); // linked video and audio split together
    TimelineEditor::unlinkItems(sequence, std::vector<Id>{right.front()});
    assert(sequence.findItem(right.front())->linkedGroupId.empty());
    TimelineEditor::setItemFades(sequence, right.front(), Rational{1,2}, Rational{1,2});
    assert((sequence.findItem(right.front())->fadeIn == Rational{1,2}));
    TimelineEditor::slipItem(project, sequence, right.front(), Rational{10,1});
    assert((sequence.findItem(right.front())->sourceRange.start() == Rational{10,1}));

    const auto snap = TimelineEditor::snapTime(sequence, Rational{121,10}, Rational{}, Rational{1,5});
    assert((snap == Rational{12,1}));
    TimelineEditor::groupItems(sequence, std::vector<Id>{right.front(), first.itemIds.front()});
    assert(!sequence.findItem(right.front())->groupId.empty());
    TimelineEditor::ungroupItems(sequence, std::vector<Id>{right.front()});
    assert(sequence.findItem(right.front())->groupId.empty());

    // Overwrite carves an existing event and replace preserves its timeline slot.
    sequence.rippleMode = RippleMode::Off;
    auto overwrite = TimelineEditor::insertLibraryClip(project, sequence, clipId, videoId,
        Rational{5,1}, EditMode::Overwrite);
    assert(!overwrite.itemIds.empty());
    const auto replaceId = overwrite.itemIds.front();
    const auto beforeStart = sequence.findItem(replaceId)->timelineStart;
    (void)TimelineEditor::insertLibraryClip(project, sequence, clipId, videoId,
        Rational{}, EditMode::Replace, replaceId);
    assert(sequence.findItem(replaceId)->timelineStart == beforeStart);

    sequence.findItem(replaceId)->name="Round-trip event name";
    project.updateSequence(sequence);
    QTemporaryDir directory; assert(directory.isValid());
    const auto path = directory.filePath("roundtrip.ytp");
    QString error;
    assert(ProjectSerializer::save(project, path, &error));
    const auto loaded = ProjectSerializer::load(path, &error);
    assert(loaded.has_value());
    assert(loaded->sequences() == project.sequences());
    assert(!loaded->validate());

    // Each ripple scope has deterministic behavior, including locked tracks.
    {
        auto p = sampleProject(); auto seq = p.sequences().front();
        const auto clip = p.libraryClips().front().id; const auto v1=seq.tracks[1].id;
        seq.rippleMode=RippleMode::Off;
        auto a=TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{5,1},EditMode::Insert);
        (void)TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{},EditMode::Insert);
        assert((seq.findItem(a.itemIds.front())->timelineStart==Rational{5,1}));
    }
    {
        auto p = sampleProject(); auto seq = p.sequences().front();
        const auto clip=p.libraryClips().front().id; const auto v2=seq.tracks[0].id; const auto v1=seq.tracks[1].id;
        seq.rippleMode=RippleMode::AffectedTracks;
        auto other=TimelineEditor::insertLibraryClip(p,seq,clip,v2,Rational{10,1},EditMode::Insert);
        auto target=TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{10,1},EditMode::Insert);
        (void)target;
        assert((seq.findItem(other.itemIds.front())->timelineStart==Rational{10,1}));
        assert((seq.findItem(other.itemIds.back())->timelineStart==Rational{14,1}));
    }

    // Ripple moves always carry downstream events and are exactly reversible.
    {
        auto p=sampleProject();auto seq=p.sequences().front();seq.rippleMode=RippleMode::AllTracks;
        const auto clip=p.libraryClips().front().id;const auto v1=seq.tracks[1].id;
        const auto left=TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{},EditMode::Overwrite);
        const auto middle=TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{4,1},EditMode::Overwrite);
        const auto rightmost=TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{8,1},EditMode::Overwrite);
        TimelineEditor::moveItems(seq,{left.itemIds.front()},Rational{2,1});
        assert((seq.findItem(left.itemIds.front())->timelineStart==Rational{2,1}));
        assert((seq.findItem(middle.itemIds.front())->timelineStart==Rational{6,1}));
        assert((seq.findItem(rightmost.itemIds.front())->timelineStart==Rational{10,1}));
        TimelineEditor::moveItems(seq,{left.itemIds.front()},Rational{});
        assert((seq.findItem(left.itemIds.front())->timelineStart==Rational{}));
        assert((seq.findItem(middle.itemIds.front())->timelineStart==Rational{4,1}));
        assert((seq.findItem(rightmost.itemIds.front())->timelineStart==Rational{8,1}));
    }
    {
        auto p=sampleProject();auto seq=p.sequences().front();seq.rippleMode=RippleMode::AllTracks;
        const auto clip=p.libraryClips().front().id;const auto v1=seq.tracks[1].id;
        const auto left=TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{},EditMode::Overwrite);
        const auto middle=TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{4,1},EditMode::Overwrite);
        const auto rightmost=TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{8,1},EditMode::Overwrite);
        TimelineEditor::moveItems(seq,{middle.itemIds.front()},Rational{6,1});
        assert((seq.findItem(left.itemIds.front())->timelineStart==Rational{}));
        assert((seq.findItem(middle.itemIds.front())->timelineStart==Rational{6,1}));
        assert((seq.findItem(rightmost.itemIds.front())->timelineStart==Rational{10,1}));
        TimelineEditor::moveItems(seq,{middle.itemIds.front()},Rational{4,1});
        assert((seq.findItem(left.itemIds.front())->timelineStart==Rational{}));
        assert((seq.findItem(middle.itemIds.front())->timelineStart==Rational{4,1}));
        assert((seq.findItem(rightmost.itemIds.front())->timelineStart==Rational{8,1}));
    }

    // Duplicate and paste always open a snapped insertion gap, even when general ripple is off.
    {
        auto p=sampleProject();auto seq=p.sequences().front();seq.rippleMode=RippleMode::Off;
        const auto clip=p.libraryClips().front().id;const auto v1=seq.tracks[1].id;
        (void)TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{},EditMode::Overwrite);
        const auto middle=TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{4,1},EditMode::Overwrite);
        const auto tail=TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{8,1},EditMode::Overwrite);
        const auto duplicates=TimelineEditor::duplicateItems(seq,{middle.itemIds.front()});
        assert(duplicates.size()==2);
        assert((seq.findItem(duplicates.front())->timelineStart==Rational{8,1}));
        assert((seq.findItem(tail.itemIds.front())->timelineStart==Rational{12,1}));
    }
    {
        auto p=sampleProject();auto seq=p.sequences().front();seq.rippleMode=RippleMode::Off;
        const auto clip=p.libraryClips().front().id;const auto v1=seq.tracks[1].id;
        (void)TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{},EditMode::Overwrite);
        const auto middle=TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{4,1},EditMode::Overwrite);
        const auto tail=TimelineEditor::insertLibraryClip(p,seq,clip,v1,Rational{8,1},EditMode::Overwrite);
        std::vector<TimelineItem> copied;
        for(const auto&id:TimelineEditor::expandedSelection(seq,{middle.itemIds.front()}))copied.push_back(*seq.findItem(id));
        const auto pasted=TimelineEditor::pasteItems(seq,copied,Rational{4,1});
        assert(pasted.size()==2);
        assert((seq.findItem(pasted.front())->timelineStart==Rational{4,1}));
        assert((seq.findItem(middle.itemIds.front())->timelineStart==Rational{8,1}));
        assert((seq.findItem(tail.itemIds.front())->timelineStart==Rational{12,1}));
    }

    // Every structural edit maps timeline time through speed and direction.
    {
        auto p=sampleProject();auto seq=p.sequences().front();seq.rippleMode=RippleMode::Off;
        const auto inserted=TimelineEditor::insertLibraryClip(p,seq,p.libraryClips().front().id,
            seq.tracks[1].id,Rational{},EditMode::Overwrite);
        const auto video=inserted.itemIds.front();
        TimelineEditor::setItemSpeed(seq,video,2.0,true);
        const auto oldLink=seq.findItem(video)->linkedGroupId;
        const auto rightIds=TimelineEditor::splitItems(seq,{video},Rational{1,1});
        const auto*left=seq.findItem(video);
        assert((left->duration==Rational{1,1}));
        assert((left->sourceRange==TimeRange{Rational{2,1},Rational{2,1}}));
        const auto rightVideo=std::find_if(rightIds.begin(),rightIds.end(),[&](const auto&id){return seq.findTrack(seq.findItem(id)->trackId)->kind==TrackKind::Video;});
        assert(rightVideo!=rightIds.end());
        const auto*rightItem=seq.findItem(*rightVideo);
        assert((rightItem->sourceRange==TimeRange{Rational{4,1},Rational{2,1}}));
        assert(!rightItem->linkedGroupId.empty()&&rightItem->linkedGroupId!=oldLink);
        assert(std::count_if(rightIds.begin(),rightIds.end(),[&](const auto&id){return seq.findItem(id)->linkedGroupId==rightItem->linkedGroupId;})==2);
    }
    {
        auto p=sampleProject();auto seq=p.sequences().front();seq.rippleMode=RippleMode::Off;
        const auto inserted=TimelineEditor::insertLibraryClip(p,seq,p.libraryClips().front().id,
            seq.tracks[1].id,Rational{},EditMode::Overwrite);
        const auto video=inserted.itemIds.front();
        TimelineEditor::setItemSpeed(seq,video,2.0,true);
        TimelineEditor::setItemReverse(seq,video,true);
        const auto rightIds=TimelineEditor::splitItems(seq,{video},Rational{1,1});
        assert((seq.findItem(video)->sourceRange==TimeRange{Rational{4,1},Rational{2,1}}));
        const auto rightVideo=std::find_if(rightIds.begin(),rightIds.end(),[&](const auto&id){return seq.findTrack(seq.findItem(id)->trackId)->kind==TrackKind::Video;});
        assert(rightVideo!=rightIds.end());
        assert((seq.findItem(*rightVideo)->sourceRange==TimeRange{Rational{2,1},Rational{2,1}}));
    }
    {
        auto p=sampleProject();auto seq=p.sequences().front();seq.rippleMode=RippleMode::Off;
        const auto inserted=TimelineEditor::insertLibraryClip(p,seq,p.libraryClips().front().id,
            seq.tracks[1].id,Rational{},EditMode::Overwrite);
        const auto video=inserted.itemIds.front();
        TimelineEditor::setItemSpeed(seq,video,2.0,true);
        TimelineEditor::setItemReverse(seq,video,true);
        TimelineEditor::trimItemStart(seq,video,Rational{1,2});
        assert((seq.findItem(video)->sourceRange==TimeRange{Rational{2,1},Rational{3,1}}));
        TimelineEditor::trimItemEnd(seq,video,Rational{3,2});
        assert((seq.findItem(video)->sourceRange==TimeRange{Rational{3,1},Rational{2,1}}));
        TimelineEditor::slipItem(p,seq,video,Rational{10,1});
        assert((seq.findItem(video)->sourceRange==TimeRange{Rational{10,1},Rational{2,1}}));
    }
    {
        auto p=sampleProject();auto seq=p.sequences().front();seq.rippleMode=RippleMode::Off;
        const auto inserted=TimelineEditor::insertLibraryClip(p,seq,p.libraryClips().front().id,
            seq.tracks[1].id,Rational{},EditMode::Overwrite);
        seq.findTrack(seq.findItem(inserted.itemIds.back())->trackId)->locked=true;
        const auto before=seq;
        bool rejected=false;try{(void)TimelineEditor::splitItems(seq,{inserted.itemIds.front()},Rational{1,1});}catch(const std::invalid_argument&){rejected=true;}
        assert(rejected&&seq==before);
    }

    // Frame-rate matrix: exact rational placement remains stable without millisecond rounding.
    for (const auto [numerator, denominator] : std::vector<std::pair<std::int64_t,std::int64_t>>{
        {24000,1001},{24,1},{25,1},{30000,1001},{30,1},{50,1},{60000,1001},{60,1}}) {
        auto p=sampleProject(); auto seq=p.sequences().front(); seq.rippleMode=RippleMode::AllTracks;
        const auto frame=frameDuration(numerator,denominator); const auto at=frame*Rational{137,1};
        auto inserted=TimelineEditor::insertLibraryClip(p,seq,p.libraryClips().front().id,seq.tracks[1].id,at,EditMode::Insert);
        assert(seq.findItem(inserted.itemIds.front())->timelineStart==at);
        auto copies=TimelineEditor::pasteItems(seq,{*seq.findItem(inserted.itemIds.front())},at+frame*Rational{200,1});
        assert((seq.findItem(copies.front())->timelineStart==at+frame*Rational{200,1}));
    }

    // A sequence edit is one atomic undo/redo transaction.
    {
        auto p=sampleProject(); auto before=p.sequences().front(); auto after=before;
        (void)TimelineEditor::insertLibraryClip(p,after,p.libraryClips().front().id,after.tracks[1].id,Rational{},EditMode::Insert);
        CommandStack history;
        history.execute(std::make_unique<UpdateSequenceCommand>(p,before,after,"insert"));
        assert(p.sequences().front()==after);
        assert(history.undo() && p.sequences().front()==before);
        assert(history.redo() && p.sequences().front()==after);
    }

    std::cout << "Timeline editing, ripple, links, precision and persistence passed.\n";
    return 0;
}
