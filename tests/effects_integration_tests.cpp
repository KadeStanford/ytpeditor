#include "media/media_cache.h"
#include "persistence/project_serializer.h"
#include "timeline/effects_editor.h"
#include "timeline/timeline_editor.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <cassert>
#include <limits>
#include <iostream>

using namespace ytp;
int main(int argc,char**argv){QCoreApplication app(argc,argv);QTemporaryDir temp;assert(temp.isValid());qputenv("YTP_CACHE_DIR",temp.path().toUtf8());
    const auto mediaPath=temp.filePath("input.mp4");QProcess ffmpeg;ffmpeg.start("C:/msys64/ucrt64/bin/ffmpeg.exe",{"-hide_banner","-loglevel","error","-y","-f","lavfi","-i","testsrc2=size=320x240:rate=30:duration=2","-f","lavfi","-i","sine=frequency=440:duration=2","-c:v","libx264","-pix_fmt","yuv420p","-c:a","aac","-shortest",mediaPath});assert(ffmpeg.waitForFinished(30000)&&ffmpeg.exitCode()==0);
    Project project{"Effects"};MediaAsset media{.id=createId(),.path=mediaPath.toStdString(),.displayName="fixture",.duration=Rational{2,1},.frameRateNumerator=30,.frameRateDenominator=1,.width=320,.height=240,.audioSampleRate=48000};project.addMediaAsset(media);LibraryClip clip{.id=createId(),.mediaAssetId=media.id,.sourceRange=TimeRange{Rational{},Rational{2,1}},.name="effect clip",.thumbnailTime=Rational{1,1}};project.addLibraryClip(clip);
    auto sequence=project.sequences().front();sequence.rippleMode=RippleMode::AllTracks;auto inserted=TimelineEditor::insertLibraryClip(project,sequence,clip.id,sequence.tracks[1].id,Rational{},EditMode::Insert);assert(inserted.itemIds.size()==2);const auto videoId=inserted.itemIds.front();const auto audioId=inserted.itemIds.back();
    TransformSettings transform;transform.positionX=120;transform.positionY=-30;transform.scaleX=1.25;transform.scaleY=.8;transform.rotation=15;transform.opacity=.7;transform.cropLeft=.1;EffectsEditor::setTransform(sequence,videoId,transform);
    (void)EffectsEditor::addTransformKeyframe(sequence,videoId,"positionX",Rational{1,2},240,KeyframeInterpolation::Smooth);
    const auto brightness=EffectsEditor::addEffect(sequence,EffectTarget::Item,videoId,"brightness_contrast");EffectsEditor::setParameter(sequence,EffectTarget::Item,videoId,brightness,"brightness",.25);const auto key=EffectsEditor::addKeyframe(sequence,EffectTarget::Item,videoId,brightness,"brightness",Rational{1,2},.5,KeyframeInterpolation::Smooth);(void)key;const auto*parameter=findParameter(sequence.findItem(videoId)->effects.front(),"brightness");assert(parameter&&evaluateParameter(*parameter,Rational{1,2})==.5);
    const auto reverb=EffectsEditor::addEffect(sequence,EffectTarget::Item,audioId,"reverb");EffectsEditor::setEffectEnabled(sequence,EffectTarget::Item,audioId,reverb,false);EffectsEditor::moveEffect(sequence,EffectTarget::Item,audioId,reverb,-1);
    AudioSettings clipAudio;clipAudio.gainDb=6;clipAudio.pan=-.25;EffectsEditor::setClipAudio(sequence,audioId,clipAudio);(void)EffectsEditor::addAudioKeyframe(sequence,EffectTarget::Item,audioId,"gain",Rational{1,1},-3,KeyframeInterpolation::Linear);
    bool invalidAudioParameter=false;try{(void)EffectsEditor::addAudioKeyframe(sequence,EffectTarget::Item,audioId,"volume",Rational{1,1},0,KeyframeInterpolation::Linear);}catch(const std::invalid_argument&){invalidAudioParameter=true;}assert(invalidAudioParameter);
    auto invalidTransform=transform;invalidTransform.anchorX=std::numeric_limits<double>::quiet_NaN();assert(validateTransform(invalidTransform).has_value());
    AudioSettings trackAudio;trackAudio.gainDb=-4;trackAudio.pan=.2;EffectsEditor::setTrackAudio(sequence,sequence.tracks[2].id,trackAudio);AudioSettings master;master.gainDb=-1;EffectsEditor::setMasterAudio(sequence,master);(void)EffectsEditor::addEffect(sequence,EffectTarget::Master,{},"limiter");
    TimelineEditor::setItemReverse(sequence,videoId,true);TimelineEditor::setFreezeFrame(sequence,videoId,true,Rational{1,1});TimelineEditor::setItemSpeed(sequence,videoId,2.0,true);assert((sequence.findItem(videoId)->duration==Rational{1,1}));assert((sequence.findItem(audioId)->duration==Rational{1,1}));
    auto copied=*sequence.findItem(videoId);auto duplicate=TimelineEditor::pasteItems(sequence,{copied},Rational{3,1});EffectsEditor::pasteItemAttributes(sequence,videoId,duplicate,true,true,true,true);assert(sequence.findItem(duplicate.front())->effects.size()==1);assert(sequence.findItem(duplicate.front())->effects.front().id!=brightness);
    sequence.previewQuality=PreviewQuality::Quarter;project.updateSequence(sequence);assert(!project.validate());
    const auto projectPath=temp.filePath("effects.ytp.json");QString error;assert(ProjectSerializer::save(project,projectPath,&error));auto loaded=ProjectSerializer::load(projectPath,&error);assert(loaded&&loaded->sequences()==project.sequences());assert(loaded->formatVersion()==6);
    auto previewItem=*sequence.findItem(videoId);previewItem.freezeFrame=false;assert(MediaCache::generateEffectPreview(mediaPath,previewItem,Rational{1,2},PreviewQuality::Quarter,&error));assert(QFileInfo::exists(MediaCache::effectPreviewPath(QString::fromStdString(videoId))));assert(MediaCache::generateProxy(mediaPath,QString::fromStdString(media.id),&error));assert(QFileInfo(MediaCache::proxyPath(QString::fromStdString(media.id))).size()>0);
    auto previewAudio=*sequence.findItem(audioId);previewAudio.effects.clear();previewItem.effects.clear();for(const auto&descriptor:effectCatalog())if(descriptor.audio)previewAudio.effects.push_back(createEffect(descriptor.type));assert(MediaCache::generatePlaybackPreview(mediaPath,previewItem,previewAudio,sequence.tracks[1],sequence.tracks[2],sequence,&error));assert(QFileInfo(MediaCache::playbackPreviewPath(QString::fromStdString(videoId))).size()>0);
    const auto dimensions=[](const QString&path){QProcess probe;probe.start("C:/msys64/ucrt64/bin/ffprobe.exe",{"-v","error","-select_streams","v:0","-show_entries","stream=width,height","-of","csv=p=0:s=x",path});assert(probe.waitForFinished(30000)&&probe.exitCode()==0);return QString::fromUtf8(probe.readAllStandardOutput()).trimmed();};
    int testedVisualEffects=0;
    for(const auto&descriptor:effectCatalog()){
        if(descriptor.audio)continue;
        auto isolatedVideo=*sequence.findItem(videoId);auto isolatedAudio=*sequence.findItem(audioId);isolatedVideo.id=createId();isolatedVideo.transform={};isolatedVideo.effects={createEffect(descriptor.type)};isolatedAudio.effects.clear();error.clear();
        const bool generated=MediaCache::generatePlaybackPreview(mediaPath,isolatedVideo,isolatedAudio,sequence.tracks[1],sequence.tracks[2],sequence,&error);if(!generated)std::cerr<<"Preview failure in "<<descriptor.type<<": "<<error.toStdString()<<"\n";assert(generated);
        const auto output=MediaCache::playbackPreviewPath(QString::fromStdString(isolatedVideo.id));assert(QFileInfo(output).size()>0);const auto actualDimensions=dimensions(output);const auto parts=actualDimensions.split('x');const bool aspectSafe=parts.size()==2&&parts[0].toInt()*3==parts[1].toInt()*4;if(!aspectSafe)std::cerr<<"Aspect regression in "<<descriptor.type<<": "<<actualDimensions.toStdString()<<"\n";assert(aspectSafe);++testedVisualEffects;
    }
    assert(testedVisualEffects==85);
    int testedAudioEffects=0;
    for(const auto&descriptor:effectCatalog()){
        if(!descriptor.audio)continue;
        auto isolatedVideo=*sequence.findItem(videoId);
        auto isolatedAudio=*sequence.findItem(audioId);
        isolatedVideo.id=createId();
        isolatedVideo.effects.clear();
        isolatedAudio.effects={createEffect(descriptor.type)};
        error.clear();
        assert(MediaCache::generatePlaybackPreview(mediaPath,isolatedVideo,isolatedAudio,sequence.tracks[1],sequence.tracks[2],sequence,&error));
        assert(QFileInfo(MediaCache::playbackPreviewPath(QString::fromStdString(isolatedVideo.id))).size()>0);
        ++testedAudioEffects;
    }
    assert(testedAudioEffects==27);
    std::cout<<"Effects, keyframes, mixer state, preview decoding, proxy, and persistence passed.\n";return 0;}
