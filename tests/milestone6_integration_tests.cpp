#include "media/media_analysis.h"
#include "model/project.h"
#include "persistence/project_serializer.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace ytp;
TimelineItem mediaItem(const MediaAsset& media,const Track& track,Rational start=Rational{}){return {.id=createId(),.mediaAssetId=media.id,.trackId=track.id,.timelineStart=start,.sourceRange=TimeRange{Rational{},Rational{1,1}},.duration=Rational{1,1}};}
int main(int argc,char**argv){QCoreApplication app(argc,argv);QString error;
 const auto words=MediaAnalysis::parseWhisperJson(R"({"segments":[{"start":0.10,"end":0.42,"text":"BUT WAIT"},{"start":0.5,"end":0.8,"text":"THERE'S MORE"}]})",&error);assert(error.isEmpty()&&words.size()==2&&words.front().text=="BUT WAIT"&&words.front().start==Rational(1,10));
 std::vector<float> pcm(32000);for(int pulse:{1600,6400,11200})for(int i=0;i<300;++i)pcm[pulse+i]=.9f*std::sin(i*.3f);const auto beats=MediaAnalysis::detectOnsetsFromPcm(pcm,16000);assert(beats.size()>=3);
 Project project("Milestone 6");MediaAsset media{.id=createId(),.path="fixture.mp4",.displayName="fixture",.duration=Rational{10,1},.frameRateNumerator=30,.frameRateDenominator=1,.width=640,.height=360,.audioSampleRate=48000,.transcriptionLanguage="en",.transcriptionModel="tiny.bin",.transcript=words};project.addMediaAsset(media);
 auto sequences=project.sequences();auto& main=sequences.front();main.name="Main";main.tracks[1].items.push_back(mediaItem(media,main.tracks[1]));auto child=createDefaultSequence();child.name="Cutaway";child.tracks[1].items.push_back(mediaItem(media,child.tracks[1]));
 TimelineItem nested{.id=createId(),.trackId=main.tracks[0].id,.timelineStart=Rational{2,1},.sourceRange=TimeRange{Rational{},Rational{1,1}},.duration=Rational{1,1}};nested.nestedSequenceId=child.id;main.tracks[0].items.push_back(nested);
 TimelineItem adjustment{.id=createId(),.trackId=main.tracks[0].id,.timelineStart=Rational{},.sourceRange=TimeRange{Rational{},Rational{1,1}},.duration=Rational{1,1}};adjustment.adjustmentClip=true;adjustment.effects.push_back(createEffect("invert"));main.tracks[0].items.push_back(adjustment);
 main.tracks[1].items.front().masks.push_back(MaskSettings{.id=createId(),.shape=MaskShape::Ellipse,.x=.2,.y=.2,.width=.5,.height=.5,.feather=.1,.opacity=.8});main.markers.push_back({.id=createId(),.time=Rational{1,2},.label="Beat",.color="#00d4ff"});sequences.push_back(child);project.setSequences(sequences);assert(!project.validate());
 QTemporaryDir temp;const auto file=temp.filePath("v6.ytp.json");assert(ProjectSerializer::save(project,file,&error));auto loaded=ProjectSerializer::load(file,&error);assert(loaded&&loaded->formatVersion()==6&&!loaded->validate());assert(loaded->mediaAssets().front().transcript==words);assert(loaded->sequences()==project.sequences());
 auto cyclic=sequences;TimelineItem recurse{.id=createId(),.trackId=cyclic[1].tracks[0].id,.timelineStart=Rational{},.sourceRange=TimeRange{Rational{},Rational{1,1}},.duration=Rational{1,1}};recurse.nestedSequenceId=cyclic[0].id;cyclic[1].tracks[0].items.push_back(recurse);Project invalid=project;invalid.setSequences(cyclic);assert(invalid.validate().value().find("cycle")!=std::string::npos);
 auto longSequence=createDefaultSequence();longSequence.name="Long performance";const auto track=longSequence.tracks[1];QElapsedTimer timer;timer.start();for(int i=0;i<20000;++i)longSequence.tracks[1].items.push_back(mediaItem(media,track,Rational{i,10}));assert(!longSequence.validate());const auto elapsed=timer.elapsed();assert(elapsed<5000);std::cout<<"Milestone 6 validation passed; 20k events in "<<elapsed<<" ms\n";return 0;}
