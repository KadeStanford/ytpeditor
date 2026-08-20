#include "ytp/ytp_toolkit.h"

#include "model/effects.h"
#include "timeline/timeline_editor.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <array>

namespace ytp {
namespace {

Rational speedRatio(double speed) {
    return Rational{static_cast<std::int64_t>(std::llround(speed * 1'000'000.0)), 1'000'000};
}

TimeRange sourceSlice(const TimelineItem& item,Rational timelineOffset,Rational timelineDuration){const auto ratio=speedRatio(item.speed);const auto sourceOffset=std::clamp(timelineOffset*ratio,Rational{},item.sourceRange.duration());const auto sourceDuration=std::min(timelineDuration*ratio,item.sourceRange.duration()-sourceOffset);const auto sourceStart=item.reverse?item.sourceRange.end()-sourceOffset-sourceDuration:item.sourceRange.start()+sourceOffset;return {sourceStart,sourceDuration};}

double number(const MacroStep& step, const std::string& key, double fallback) {
    const auto found = step.numbers.find(key); return found == step.numbers.end() ? fallback : found->second;
}

std::string text(const MacroStep& step, const std::string& key, std::string fallback = {}) {
    const auto found = step.strings.find(key); return found == step.strings.end() ? std::move(fallback) : found->second;
}

struct Block final {
    std::vector<TimelineItem> items;
    Rational start;
    Rational duration;
    std::unordered_set<std::string> tracks;
};

Block selectionBlock(const Sequence& sequence, const std::vector<Id>& ids) {
    const auto expanded = TimelineEditor::expandedSelection(sequence, ids);
    if (expanded.empty()) throw std::invalid_argument("select a timeline event first");
    std::unordered_set<std::string> selected(expanded.begin(), expanded.end());
    Block block;
    bool initialized = false;
    for (const auto& track : sequence.tracks) for (const auto& item : track.items) if (selected.contains(item.id)) {
        if (track.locked) throw std::invalid_argument("a selected track is locked");
        if (!initialized) { block.start=item.timelineStart; block.duration=item.duration; initialized=true; }
        if (item.timelineStart != block.start || item.duration != block.duration)
            throw std::invalid_argument("tool requires one aligned linked A/V event");
        block.items.push_back(item); block.tracks.insert(track.id);
    }
    if (block.items.empty() || block.duration <= Rational{}) throw std::invalid_argument("selection is empty");
    return block;
}

void sortTracks(Sequence& sequence) {
    for (auto& track : sequence.tracks) std::stable_sort(track.items.begin(), track.items.end(), [](const auto& a,const auto& b){return a.timelineStart<b.timelineStart;});
}

void shiftAfter(Sequence& sequence, const Block& block, Rational newDuration) {
    const auto delta = newDuration - block.duration;
    if (delta == Rational{}) return;
    const auto pivot = block.start + block.duration;
    for (auto& track : sequence.tracks) {
        // A structural YTP edit replaces the selected block, so its own linked A/V tail must
        // always follow the replacement even when general timeline ripple is disabled.
        const bool included = block.tracks.contains(track.id) || sequence.rippleMode == RippleMode::AllTracks;
        if (!included || track.locked) continue;
        for (auto& item : track.items) if (item.timelineStart >= pivot) item.timelineStart = item.timelineStart + delta;
    }
    if (sequence.rippleMarkers) for (auto& marker : sequence.markers) if (marker.time >= pivot) marker.time = std::max(Rational{}, marker.time + delta);
}

std::vector<Id> replaceBlock(Sequence& sequence, const Block& block, std::vector<TimelineItem> generated, Rational newDuration) {
    // Remove any fractional rounding drift between generated fragments. Each affected track is
    // compacted independently so linked video and audio stay aligned without creating micro-gaps.
    std::unordered_map<Id,std::vector<TimelineItem*>> byTrack;
    for (auto& item : generated) byTrack[item.trackId].push_back(&item);
    for (auto& [trackId,items] : byTrack) {
        (void)trackId;
        std::stable_sort(items.begin(),items.end(),[](const auto* a,const auto* b){return a->timelineStart<b->timelineStart;});
        auto cursor=block.start;
        for (auto* item : items) { item->timelineStart=cursor; cursor=cursor+item->duration; }
    }
    shiftAfter(sequence, block, newDuration);
    std::unordered_set<std::string> originals; for(const auto& item:block.items)originals.insert(item.id);
    for(auto& track:sequence.tracks)std::erase_if(track.items,[&](const auto& item){return originals.contains(item.id);});
    std::vector<Id> result;
    for(auto& item:generated){auto* track=sequence.findTrack(item.trackId);if(!track||track->locked)throw std::invalid_argument("generated track unavailable");result.push_back(item.id);track->items.push_back(std::move(item));}
    sortTracks(sequence); return result;
}

TimelineItem clone(const TimelineItem& source, Rational start, Rational duration,
                   Rational sourceStart, Rational sourceDuration, const Id& link = {}) {
    auto item=source; item.id=createId(); item.timelineStart=start; item.duration=duration;
    item.sourceRange=TimeRange{sourceStart,sourceDuration}; item.linkedGroupId=link; item.groupId={};
    item.fadeIn={}; item.fadeOut={}; return item;
}

Id repetitionLink(const Block& block) {
    return block.items.size()>1 ? createId() : Id{};
}

EffectInstance effect(const std::string& type, std::initializer_list<std::pair<const char*,double>> values={}) {
    auto result=createEffect(type); for(const auto& [name,value]:values)if(auto* p=findParameter(result,name))p->value=std::clamp(value,p->minimum,p->maximum);return result;
}

Id deterministicId(std::mt19937_64& random) {
    std::array<unsigned char,16> bytes{};for(auto& value:bytes)value=static_cast<unsigned char>(random()&0xffU);
    bytes[6]=static_cast<unsigned char>((bytes[6]&0x0fU)|0x40U);bytes[8]=static_cast<unsigned char>((bytes[8]&0x3fU)|0x80U);
    constexpr char digits[]="0123456789abcdef";Id result;result.reserve(36);
    for(std::size_t index=0;index<bytes.size();++index){if(index==4||index==6||index==8||index==10)result.push_back('-');result.push_back(digits[bytes[index]>>4U]);result.push_back(digits[bytes[index]&0x0fU]);}
    return result;
}

std::vector<TimelineItem*> selectedItems(Sequence& sequence,const std::vector<Id>& ids) {
    const auto expanded=TimelineEditor::expandedSelection(sequence,ids);std::vector<TimelineItem*> result;
    for(const auto& id:expanded)if(auto* item=sequence.findItem(id)){const auto*track=sequence.findTrack(item->trackId);if(!track||track->locked)throw std::invalid_argument("a selected track is locked");result.push_back(item);}
    if(result.empty()) throw std::invalid_argument("select a timeline event first");
    return result;
}

ToolkitResult result(std::vector<Id> ids,Rational start,Rational duration,std::string summary){return {std::move(ids),start,duration,std::move(summary)};}

} // namespace

ToolkitResult YtpToolkit::stutter(Sequence& sequence,const std::vector<Id>& ids,int repeats,Rational slice,bool alternate){
    if(repeats<2||repeats>128||slice<=Rational{})throw std::invalid_argument("invalid stutter settings");
    const auto block=selectionBlock(sequence,ids);slice=std::min(slice,block.duration);std::vector<TimelineItem> generated;
    for(int index=0;index<repeats;++index){const auto link=repetitionLink(block);for(const auto& source:block.items){const auto range=sourceSlice(source,Rational{},slice);auto item=clone(source,block.start+slice*Rational{index,1},slice,range.start(),range.duration(),link);if(alternate&&index%2)item.reverse=!item.reverse;generated.push_back(std::move(item));}}
    const auto duration=slice*Rational{repeats,1};auto created=replaceBlock(sequence,block,std::move(generated),duration);return result(std::move(created),block.start,duration,"Stutter Builder: "+std::to_string(repeats)+" slices");
}

ToolkitResult YtpToolkit::rapidReverse(Sequence& sequence,const std::vector<Id>& ids,int segments,Rational segment){
    if(segments<2||segments>128||segment<=Rational{})throw std::invalid_argument("invalid rapid reverse settings");
    const auto block=selectionBlock(sequence,ids);segment=std::min(segment,block.duration);std::vector<TimelineItem> generated;
    for(int index=0;index<segments;++index){const auto link=repetitionLink(block);for(const auto& source:block.items){const auto timelineOffset=std::min(std::max(Rational{},source.duration-segment),segment*Rational{index,1});const auto range=sourceSlice(source,timelineOffset,segment);auto item=clone(source,block.start+segment*Rational{index,1},segment,range.start(),range.duration(),link);item.reverse=(index%2==0)?source.reverse:!source.reverse;generated.push_back(std::move(item));}}
    const auto duration=segment*Rational{segments,1};auto created=replaceBlock(sequence,block,std::move(generated),duration);return result(std::move(created),block.start,duration,"Rapid Reverse: "+std::to_string(segments)+" alternating segments");
}

ToolkitResult YtpToolkit::frameRepeat(Sequence& sequence,const std::vector<Id>& ids,Rational frame,int sourceFrames,int repeatsPerFrame){
    if(frame<=Rational{}||sourceFrames<1||sourceFrames>120||repeatsPerFrame<1||repeatsPerFrame>120)throw std::invalid_argument("invalid frame repeat settings");
    const auto block=selectionBlock(sequence,ids);sourceFrames=std::min(sourceFrames,std::max(1,static_cast<int>(std::ceil((block.duration/frame).asLongDouble()))));std::vector<TimelineItem> generated;int output=0;
    for(int sourceIndex=0;sourceIndex<sourceFrames;++sourceIndex)for(int repeat=0;repeat<repeatsPerFrame;++repeat,++output){const auto link=repetitionLink(block);for(const auto& source:block.items){const auto ratio=speedRatio(source.speed);const auto sourceOffset=frame*Rational{sourceIndex,1}*ratio;const auto available=std::max(Rational{},source.sourceRange.duration()-sourceOffset);const auto sourceDuration=std::min(available,frame*ratio);if(sourceDuration<=Rational{})continue;const auto sourceStart=source.reverse?source.sourceRange.end()-sourceOffset-sourceDuration:source.sourceRange.start()+sourceOffset;auto item=clone(source,block.start+frame*Rational{output,1},frame,sourceStart,sourceDuration,link);const auto*track=sequence.findTrack(source.trackId);if(track&&track->kind==TrackKind::Video){item.freezeFrame=true;item.freezeSourceTime=source.reverse?item.sourceRange.end()-Rational{1,1'000'000}:item.sourceRange.start();}generated.push_back(std::move(item));}}
    const auto duration=frame*Rational{output,1};auto created=replaceBlock(sequence,block,std::move(generated),duration);return result(std::move(created),block.start,duration,"Frame Repeat: "+std::to_string(sourceFrames)+" frames x "+std::to_string(repeatsPerFrame));
}

ToolkitResult YtpToolkit::rhythmRepeat(Sequence& sequence,const std::vector<Id>& ids,Rational at,double bpm,int beats,Rational gate,bool useMarkers){
    if(bpm<20||bpm>400||beats<1||beats>256||gate<=Rational{}||at<Rational{})throw std::invalid_argument("invalid rhythm settings");
    const auto block=selectionBlock(sequence,ids);std::vector<Rational> starts;
    if(useMarkers){for(const auto& marker:sequence.markers)if(marker.time>=at)starts.push_back(marker.time);std::sort(starts.begin(),starts.end());if(static_cast<int>(starts.size())>beats)starts.resize(static_cast<std::size_t>(beats));}
    const auto beat=Rational{60'000'000,static_cast<std::int64_t>(std::llround(bpm*1'000'000))};while(static_cast<int>(starts.size())<beats)starts.push_back(starts.empty()?at:starts.back()+beat);
    std::vector<TimelineItem> generated;for(const auto& start:starts){const auto link=repetitionLink(block);for(const auto& source:block.items){const auto duration=std::min(gate,source.duration);const auto range=sourceSlice(source,Rational{},duration);auto item=clone(source,start,duration,range.start(),range.duration(),link);generated.push_back(std::move(item));}}
    // Rhythm Repeat is additive: it never removes the source phrase.
    std::vector<Id> created;for(auto& item:generated){created.push_back(item.id);sequence.findTrack(item.trackId)->items.push_back(std::move(item));}sortTracks(sequence);
    const auto end=starts.empty()?at:starts.back()+gate;return result(std::move(created),at,end-at,"Rhythm Repeat: "+std::to_string(beats)+(useMarkers?" marker beats":" tempo beats"));
}

ToolkitResult YtpToolkit::speedLadder(Sequence& sequence,const std::vector<Id>& ids,int steps,double first,double last,double pitchStep,bool preserve){
    if(steps<2||steps>64||first<0.05||last<0.05||first>16||last>16||pitchStep<-24||pitchStep>24)throw std::invalid_argument("invalid speed ladder settings");
    const auto block=selectionBlock(sequence,ids);std::vector<TimelineItem> generated;Rational cursor=block.start;
    for(int index=0;index<steps;++index){const auto amount=static_cast<double>(index)/(steps-1);const auto speed=first+(last-first)*amount;Rational stepDuration;bool initialized=false;const auto link=repetitionLink(block);for(const auto& source:block.items){const auto duration=source.sourceRange.duration()/speedRatio(speed);if(!initialized){stepDuration=duration;initialized=true;}auto item=clone(source,cursor,duration,source.sourceRange.start(),source.sourceRange.duration(),link);item.speed=speed;item.pitchSemitones=source.pitchSemitones+pitchStep*index;item.preservePitch=preserve;generated.push_back(std::move(item));}cursor=cursor+stepDuration;}
    const auto duration=cursor-block.start;auto created=replaceBlock(sequence,block,std::move(generated),duration);return result(std::move(created),block.start,duration,"Speed Ladder: "+std::to_string(steps)+" steps");
}

ToolkitResult YtpToolkit::safeEarrape(Sequence& sequence,const std::vector<Id>& ids,double intensity){
    intensity=std::clamp(intensity,0.0,1.0);auto items=selectedItems(sequence,ids);std::vector<Id> changed;
    for(auto* item:items){const auto* track=sequence.findTrack(item->trackId);if(!track||track->kind!=TrackKind::Audio)continue;item->audio.gainDb=std::min(12.0,item->audio.gainDb+4.0+intensity*8.0);item->effects.push_back(effect("eq",{{"lowDb",3+intensity*5},{"midDb",5+intensity*10},{"highDb",2+intensity*8}}));item->effects.push_back(effect("compressor",{{"threshold",-20},{"ratio",6+intensity*8},{"attack",5},{"release",120}}));item->effects.push_back(effect("distortion",{{"drive",6+intensity*18},{"mix",.7+intensity*.3}}));item->effects.push_back(effect("limiter",{{"ceiling",-1},{"release",45}}));changed.push_back(item->id);}
    if(changed.empty()) throw std::invalid_argument("safe Earrape requires selected audio");
    sequence.masterLimiter=true;
    return result(std::move(changed),{}, {},"Safe Earrape: boosted, crushed, and hard-limited");
}

const std::vector<YtpPresetDescriptor>& YtpToolkit::visualPresets(){
    static const std::vector<YtpPresetDescriptor> values{
        {"feedback_void","Feedback Void","Recursive transformed frames collapse into an evolving visual tunnel"},
        {"pixel_sort_crush","Pixel Sort Crush","Luminance-sorted pixel runs rupture under digital block corruption"},
        {"liquid_memory","Liquid Memory","Water refraction carries delayed luminous frame memories"},
        {"projector_break","Projector Breakdown","Mechanical gate weave, dust, flicker, and nervous frame recall"},
        {"analog_freefall","Analog Freefall","Horizontal synchronization collapses while the film gate drifts"},
        {"graffiti_ghost","Graffiti Ghost","Moving highlights write themselves permanently across the picture"},
        {"nervous_breakdown","Nervous Breakdown","The frame recalls the wrong moments while geometry contracts elastically"},
        {"cmyk_attack","CMYK Attack","Misregistered print screens collide with temporal impact"},
        {"clone_army","Clone Army","Full-frame clones occupy an animated grid under alternating field damage"},
        {"dither_game","Dither Game","Ordered palette texture with low-cadence game-like motion"},
        {"elastic_reaction","Elastic Reaction","The image stretches around its center and snaps through radial shock"},
        {"native_kaleido","Radial Kaleidoscope","True radial reflection driven through feedback and color motion"},
        {"strip_tornado","Strip Tornado","Horizontal and vertical strips reorder inside a violent camera spiral"},
        {"nine_lives","Nine Lives","Nine neighboring moments coexist in a live temporal mosaic"},
        {"liquid_lens","Liquid Lens","Two-axis funhouse glass ripples outward from the center"},
        {"gravity_well","Gravity Well","The frame twists inward around a dark orbital center"},
        {"video_melt","Video Melt","Columns of imagery liquefy and leave colored residue"},
        {"newspaper_riot","Newspaper Riot","Moving footage becomes a shaking high-contrast printed halftone"},
        {"cellular_bloom","Cellular Bloom","Bright forms grow outward like false-color biological cells"},
        {"rotting_film","Rotting Film","Dark image regions erode into stained temporal residue"},
        {"interlace_demon","Interlace Demon","Luma and chroma fields exchange places during a sync failure"},
        {"stop_motion_panic","Stop-Motion Panic","Dropped motion cadence collides with temporal XOR silhouettes"},
        {"chroma_theft","Chroma Theft","Color planes are stolen and reassembled under posterized contrast"},
        {"shockwave","Radial Shockwave","Concentric impact waves punch through a magnified frame"},
        {"heat_memory","Heat Memory","Successive frames burn together through a solarized heat blend"},
        {"oil_stain","Oil Stain","Temporal stains spread through aggressively normalized color"},
        {"morph_monster","Morph Monster","Alternating growth and erosion mutate every contour"},
        {"strip_mine","Strip Mine","Deterministic block and strip shuffles dismantle spatial continuity"},
        {"tunnel_vision","Tunnel Vision","Spherical geometry feeds an animated central vortex"},
        {"crt_surgery","CRT Surgery","Field damage is examined under a live engineering scope"},
        {"comic_freeze","Comic Freeze","Low-cadence movement hardens into printed impact frames"},
        {"dream_print","Dream Print","Halftone fragments float through soft radial dream waves"},
        {"block_party","Block Party","Rebuild the frame from shuffled rectangular chunks"},
        {"fisheye_panic","Fisheye Panic","Trap the image inside an extreme circular bubble lens"},
        {"tiny_planet_spin","Tiny Planet Spin","Stereographic world projection rotating through color trails"},
        {"scope_creep","Scope Creep","Turn footage into a live engineering monitor with signal traces"},
        {"time_scramble","Time Scramble","Randomly reorder nearby frames and burn their movement together"},
        {"motion_detector","Motion Detector","Expose and violently amplify only what changes between frames"},
        {"xor_nightmare","XOR Nightmare","Successive frames collide into stark digital silhouettes"},
        {"pixel_bloom_pack","Pixel Bloom","Bright areas consume large pixel blocks like a spreading mosaic"},
        {"xray_fever","X-Ray Fever","Direction-scanned contours remapped into false-color energy"},
        {"slanted_universe","Slanted Universe","Shear the world sideways while gravity rocks the frame"},
        {"impact_crater","Impact Crater","Violent crash zoom with channel separation and camera recoil"},
        {"spin_cycle","Spin Cycle","Continuous rotation dragging colorful motion-burn ghosts"},
        {"rubber_room","Rubber Room","Elastic pixel waves, pendulum motion, and swollen optics"},
        {"signal_possession","Signal Possession","Animated horizontal tears, sync damage, and hostile flashes"},
        {"thermal_runaway","Thermal Runaway","False-color heat vision burned into persistent motion trails"},
        {"perspective_drop","Perspective Drop","The frame collapses backward before a hard push-in"},
        {"orbiting_ghosts","Orbiting Ghosts","Spinning neon contours multiplied across recent frames"},
        {"glitch_shredder","Glitch Shredder","RGB slices tear through elastic moving scan bands"},
        {"panic_cam","Panic Cam","Close, rocking handheld chaos with aggressive impact shake"},
        {"afterimage_burn","Afterimage Burn","Solarized color leaves luminous motion scars behind"},
        {"deep_fried","Deep Fried","Extreme contrast, saturation, sharpen, and heat"},
        {"vhs_breakdown","VHS Breakdown","Animated tape noise, chroma drift, scanlines, and soft focus"},
        {"pixel_scream","Pixel Scream","Large pixels, hard sharpen, and posterization"},
        {"threshold_vision","Threshold Vision","High-contrast monochrome threshold"},
        {"mirror_hell","Mirror Hell","Flip, rotate, hue shift, overscale, and lens warp"},
        {"acid_trip","Acid Trip","Continuously cycling hue with crushed psychedelic color"},
        {"crt_meltdown","CRT Meltdown","Bad sync, RGB separation, scanlines, and dark tube edges"},
        {"neon_pulse","Neon Pulse","Animated neon contours with hue cycling and rhythmic flashes"},
        {"memory_leak","Memory Leak","Long recursive trails and smeared temporal color"},
        {"vertical_sync","Vertical Sync Failure","Rolling picture, VHS noise, and nervous camera shake"},
        {"cartoon_panic","Cartoon Panic","Graphic ink edges, posterized color, and frantic shake"},
        {"solar_flare","Solar Flare","Solarized highlights with animated color and strobe bursts"},
        {"rgb_quake","RGB Quake","Large channel splits, warped motion, and heavy shake"},
        {"data_fever","Data Fever","Datamosh decay, time smear, and cross-wired channels"},
        {"prism_tunnel","Prism Tunnel","Kaleidoscopic lens distortion with continuous color motion"},
        {"ghost_echo","Ghost Echo","Edge ghosts dragged through recursive frame trails"},
        {"channel_surfer","Channel Surfer","Noisy channel-change color, scanlines, and rolling sync"},
        {"hard_cutout","Hard Cutout","Embossed threshold shapes with a severe vignette"}
    };
    return values;
}

ToolkitResult YtpToolkit::applyVisualPreset(Sequence& sequence,const std::vector<Id>& ids,const std::string& preset){
    auto items=selectedItems(sequence,ids);std::vector<Id> changed;
    const bool found=std::any_of(visualPresets().begin(),visualPresets().end(),[&](const auto&value){return value.id==preset;});
    if(!found)throw std::invalid_argument("unknown visual preset");
    for(auto* item:items){
        const auto* track=sequence.findTrack(item->trackId);if(!track||track->kind!=TrackKind::Video)continue;
        const auto add=[&](const std::string&type,std::initializer_list<std::pair<const char*,double>> values={}){item->effects.push_back(effect(type,values));};
        if(preset=="feedback_void"){add("video_feedback",{{"phase",.24},{"zoom",.8}});add("vortex",{{"strength",1.5},{"radius",.9},{"speed",.45}});add("vignette",{{"strength",.86}});}
        else if(preset=="pixel_sort_crush"){add("pixel_sort");add("digital_glitch",{{"frequency",.42},{"blockHeight",.22},{"shift",.72},{"color",.38}});add("sharpen",{{"amount",1.6}});}
        else if(preset=="liquid_memory"){add("water_surface",{{"amplitude",.62},{"frequency",.44}});add("frame_delay",{{"delay",.48}});add("light_graffiti",{{"sensitivity",.42},{"decay",.7}});}
        else if(preset=="projector_break"){add("film_projector",{{"grain",.52},{"dust",.6},{"flicker",.5}});add("film_gate_weave",{{"interval",.26},{"horizontal",.65},{"vertical",.5}});add("nervous_frames",{{"strength",.36}});}
        else if(preset=="analog_freefall"){add("analog_nosync",{{"strength",.78}});add("film_gate_weave",{{"interval",.2},{"horizontal",.75},{"vertical",.58}});add("vertical_roll",{{"speed",.08}});}
        else if(preset=="graffiti_ghost"){add("light_graffiti",{{"sensitivity",.62},{"decay",.78}});add("edge_glow_native",{{"strength",.82}});add("motion_burn",{{"decay",.975}});}
        else if(preset=="nervous_breakdown"){add("nervous_frames",{{"strength",.82}});add("elastic_scale",{{"centerX",.5},{"centerY",.45},{"strength",.78}});add("frame_xor",{{"opacity",.36}});}
        else if(preset=="cmyk_attack"){add("cmyk_halftone",{{"dotSize",.55},{"angle",.62}});add("impact_zoom",{{"zoom",1.28}});add("chroma_planes",{{"mode",1}});}
        else if(preset=="clone_army"){add("clone_grid",{{"columns",.48},{"rows",.42}});add("field_corruption");add("color_cycle",{{"speed",.12}});}
        else if(preset=="dither_game"){add("ordered_dither",{{"levels",.36},{"pattern",.78}});add("frame_skip",{{"step",4}});add("chroma_planes",{{"mode",2}});}
        else if(preset=="elastic_reaction"){add("elastic_scale",{{"centerX",.5},{"centerY",.5},{"strength",.86}});add("radial_ripple",{{"amount",.28},{"spacing",14},{"speed",13}});add("screen_shake",{{"amount",5},{"speed",26}});}
        else if(preset=="native_kaleido"){add("kaleidoscope",{{"strength",.82}});add("video_feedback",{{"phase",.12},{"zoom",.64}});add("color_cycle",{{"speed",.21}});}
        else if(preset=="strip_tornado"){add("horizontal_shuffle",{{"strip",18},{"seed",901}});add("vertical_shuffle",{{"strip",27},{"seed",331}});add("vortex",{{"strength",1.1},{"radius",.7},{"speed",2.8}});add("screen_shake",{{"amount",7},{"speed",28}});}
        else if(preset=="nine_lives"){add("temporal_mosaic",{{"grid",3},{"border",2}});add("color_cycle",{{"speed",.18}});add("sharpen",{{"amount",1.4}});}
        else if(preset=="liquid_lens"){add("funhouse",{{"amount",34},{"spacing",24},{"speed",5.2}});add("radial_ripple",{{"amount",.14},{"spacing",19},{"speed",6}});}
        else if(preset=="gravity_well"){add("vortex",{{"strength",3.8},{"radius",.8},{"speed",.75}});add("vignette",{{"strength",.9}});add("motion_burn",{{"decay",.97}});}
        else if(preset=="video_melt"){add("melt",{{"amount",62},{"spacing",15},{"speed",4}});add("temporal_stain",{{"opacity",.72}});add("color_cycle",{{"speed",.16}});}
        else if(preset=="newspaper_riot"){add("halftone",{{"cell",3},{"contrast",72}});add("screen_shake",{{"amount",9},{"speed",31}});add("brightness_contrast",{{"contrast",1.55}});}
        else if(preset=="cellular_bloom"){add("dilation_bloom",{{"iterations",4}});add("thermal",{{"palette",11}});add("motion_amplify",{{"factor",4},{"radius",2},{"threshold",5}});}
        else if(preset=="rotting_film"){add("erosion_decay",{{"iterations",4}});add("temporal_stain",{{"opacity",.84}});add("vignette",{{"strength",.7}});}
        else if(preset=="interlace_demon"){add("field_corruption");add("vertical_roll",{{"speed",.09}});add("rgb_split",{{"offset",9},{"vertical",5}});}
        else if(preset=="stop_motion_panic"){add("frame_skip",{{"step",6}});add("frame_xor",{{"opacity",.72}});add("screen_shake",{{"amount",6},{"speed",17}});}
        else if(preset=="chroma_theft"){add("chroma_planes",{{"mode",1}});add("posterize",{{"levels",9}});add("brightness_contrast",{{"contrast",1.45}});}
        else if(preset=="shockwave"){add("radial_ripple",{{"amount",.34},{"spacing",18},{"speed",12}});add("impact_zoom",{{"zoom",1.32}});add("sharpen",{{"amount",2}});}
        else if(preset=="heat_memory"){add("temporal_heat",{{"opacity",.86}});add("solarize",{{"threshold",.38}});add("motion_burn",{{"decay",.965}});}
        else if(preset=="oil_stain"){add("temporal_stain",{{"opacity",.94}});add("saturation",{{"amount",2.6}});add("brightness_contrast",{{"contrast",1.3}});}
        else if(preset=="morph_monster"){add("dilation_bloom",{{"iterations",3}});add("erosion_decay",{{"iterations",2}});add("xray_edges",{{"strength",1.2},{"palette",15}});}
        else if(preset=="strip_mine"){add("block_shuffle",{{"width",26},{"height",18},{"seed",616}});add("horizontal_shuffle",{{"strip",11},{"seed",77}});add("vertical_shuffle",{{"strip",19},{"seed",88}});}
        else if(preset=="tunnel_vision"){add("fisheye",{{"strength",1.15}});add("vortex",{{"strength",2.8},{"radius",.5},{"speed",1.2}});add("vignette",{{"strength",.86}});}
        else if(preset=="crt_surgery"){add("field_corruption");add("oscilloscope",{{"size",.84},{"tilt",.12},{"opacity",1}});add("scanlines",{{"spacing",3},{"opacity",.3}});}
        else if(preset=="comic_freeze"){add("frame_skip",{{"step",5}});add("halftone",{{"cell",2.5},{"contrast",66}});add("impact_zoom",{{"zoom",1.18}});}
        else if(preset=="dream_print"){add("halftone",{{"cell",4},{"contrast",38}});add("radial_ripple",{{"amount",.1},{"spacing",25},{"speed",3}});add("motion_burn",{{"decay",.94}});}
        else if(preset=="block_party"){add("block_shuffle",{{"width",44},{"height",30},{"seed",420}});add("posterize",{{"levels",14}});add("screen_shake",{{"amount",5},{"speed",22}});}
        else if(preset=="fisheye_panic"){add("fisheye",{{"horizontalFov",185},{"verticalFov",145}});add("impact_zoom",{{"zoom",1.2}});add("vignette",{{"strength",.84}});}
        else if(preset=="tiny_planet_spin"){add("tiny_planet",{{"horizontalFov",255},{"verticalFov",185},{"roll",28}});add("spin",{{"speed",.62}});add("color_cycle",{{"speed",.2}});add("motion_burn",{{"decay",.965}});}
        else if(preset=="scope_creep"){add("oscilloscope",{{"size",.78},{"tilt",.18},{"opacity",.96}});add("grayscale");add("tint",{{"red",.3},{"green",1.5},{"blue",.45},{"mix",.82}});add("scanlines",{{"spacing",3},{"opacity",.24}});}
        else if(preset=="time_scramble"){add("frame_randomizer",{{"frames",14},{"seed",1987}});add("motion_burn",{{"decay",.972}});add("chromatic_aberration",{{"offset",11}});}
        else if(preset=="motion_detector"){add("motion_amplify",{{"factor",18},{"radius",4},{"threshold",2}});add("xray_edges",{{"strength",1.4},{"palette",9}});}
        else if(preset=="xor_nightmare"){add("frame_xor",{{"opacity",.92}});add("threshold",{{"level",.4}});add("rgb_split",{{"offset",18},{"vertical",5}});}
        else if(preset=="pixel_bloom_pack"){add("pixel_bloom",{{"width",30},{"height",24},{"mode",2}});add("saturation",{{"amount",2.2}});add("motion_burn",{{"decay",.93}});}
        else if(preset=="xray_fever"){add("xray_edges",{{"strength",4.2},{"palette",7}});add("color_cycle",{{"speed",.38}});add("strobe",{{"rate",5},{"duty",.08}});}
        else if(preset=="slanted_universe"){add("shear",{{"horizontal",.52},{"vertical",-.18}});add("pendulum",{{"angle",9},{"speed",1.1}});add("lens_warp",{{"amount",-.22},{"secondary",.14}});}
        else if(preset=="impact_crater"){add("impact_zoom",{{"zoom",1.65}});add("screen_shake",{{"amount",16},{"speed",34}});add("rgb_split",{{"offset",24},{"vertical",6}});add("brightness_contrast",{{"brightness",.06},{"contrast",1.35}});}
        else if(preset=="spin_cycle"){add("spin",{{"speed",2.15}});add("motion_burn",{{"decay",.965}});add("color_cycle",{{"speed",.55}});}
        else if(preset=="rubber_room"){add("elastic_wave",{{"amount",34},{"spacing",21},{"speed",10}});add("pendulum",{{"angle",11},{"speed",1.35}});add("lens_warp",{{"amount",.42},{"secondary",-.12}});}
        else if(preset=="signal_possession"){add("glitch_bands",{{"amount",72},{"band",42},{"speed",175}});add("bad_tv",{{"noise",20},{"scanlines",.32},{"roll",.045}});add("strobe",{{"rate",9},{"duty",.10}});}
        else if(preset=="thermal_runaway"){add("thermal",{{"palette",7}});add("motion_burn",{{"decay",.975}});add("posterize",{{"levels",16}});add("brightness_contrast",{{"contrast",1.25}});}
        else if(preset=="perspective_drop"){add("perspective_tilt",{{"amount",.24},{"vertical",.15}});add("impact_zoom",{{"zoom",1.28},{"offsetY",-.18}});add("vignette",{{"strength",.68}});}
        else if(preset=="orbiting_ghosts"){add("spin",{{"speed",.72}});add("neon_edges",{{"strength",.36},{"saturation",2.8}});add("recursive_trails",{{"frames",9},{"decay",.78}});add("color_cycle",{{"speed",.3}});}
        else if(preset=="glitch_shredder"){add("glitch_bands",{{"amount",96},{"band",34},{"speed",230}});add("elastic_wave",{{"amount",15},{"spacing",12},{"speed",16}});add("rgb_split",{{"offset",28},{"vertical",11}});}
        else if(preset=="panic_cam"){add("impact_zoom",{{"zoom",1.48},{"offsetX",.18}});add("pendulum",{{"angle",8},{"speed",2.6}});add("screen_shake",{{"amount",13},{"speed",42}});add("sharpen",{{"amount",1.8}});}
        else if(preset=="afterimage_burn"){add("motion_burn",{{"decay",.985}});add("solarize",{{"threshold",.42}});add("saturation",{{"amount",2.3}});add("color_cycle",{{"speed",.22}});}
        else if(preset=="deep_fried"){add("brightness_contrast",{{"brightness",.12},{"contrast",1.8}});add("saturation",{{"amount",2.5}});add("sharpen",{{"amount",2.2}});add("tint",{{"red",1.3},{"green",.9},{"blue",.72},{"mix",.5}});}
        else if(preset=="vhs_breakdown"){add("vhs_noise",{{"strength",22},{"chroma",8}});add("scanlines",{{"spacing",4},{"opacity",.32}});add("blur",{{"radius",2}});add("hue",{{"degrees",-12}});}
        else if(preset=="pixel_scream"){add("pixel_bloom",{{"width",26},{"height",20},{"mode",2}});add("horizontal_shuffle",{{"strip",9},{"seed",212}});add("sharpen",{{"amount",2.5}});}
        else if(preset=="threshold_vision"){add("halftone",{{"cell",2},{"contrast",82}});add("threshold",{{"level",.46}});add("motion_amplify",{{"factor",3},{"radius",2},{"threshold",6}});}
        else if(preset=="mirror_hell"){item->transform.flipHorizontal=!item->transform.flipHorizontal;add("funhouse",{{"amount",31},{"spacing",17},{"speed",8}});add("vortex",{{"strength",-1.6},{"radius",.6},{"speed",1.6}});}
        else if(preset=="acid_trip"){add("vortex",{{"strength",1.3},{"radius",1.1},{"speed",.7}});add("color_cycle",{{"speed",.45}});add("saturation",{{"amount",3}});add("temporal_stain",{{"opacity",.45}});}
        else if(preset=="crt_meltdown"){add("bad_tv",{{"noise",24},{"scanlines",.38},{"roll",.06}});add("rgb_split",{{"offset",14},{"vertical",3}});add("vignette",{{"strength",.72}});}
        else if(preset=="neon_pulse"){add("dilation_bloom",{{"iterations",2}});add("neon_edges",{{"strength",.48},{"saturation",3}});add("color_cycle",{{"speed",.85}});}
        else if(preset=="memory_leak"){add("temporal_mosaic",{{"grid",2},{"border",1}});add("recursive_trails",{{"frames",8},{"decay",.78}});add("chroma_planes",{{"mode",2}});}
        else if(preset=="vertical_sync"){add("vertical_roll",{{"speed",.14}});add("vhs_noise",{{"strength",20},{"chroma",7}});add("screen_shake",{{"amount",6},{"speed",24}});}
        else if(preset=="cartoon_panic"){add("halftone",{{"cell",2.4},{"contrast",48}});add("posterize",{{"levels",10}});add("screen_shake",{{"amount",10},{"speed",30}});}
        else if(preset=="solar_flare"){add("temporal_heat",{{"opacity",.78}});add("solarize",{{"threshold",.4}});add("radial_ripple",{{"amount",.08},{"spacing",30},{"speed",5}});}
        else if(preset=="rgb_quake"){add("radial_ripple",{{"amount",.28},{"spacing",16},{"speed",14}});add("rgb_split",{{"offset",30},{"vertical",8}});add("screen_shake",{{"amount",18},{"speed",35}});}
        else if(preset=="data_fever"){add("frame_randomizer",{{"frames",9},{"seed",2600}});add("datamosh",{{"decay",.92}});add("chroma_planes",{{"mode",1}});}
        else if(preset=="prism_tunnel"){add("vortex",{{"strength",4.4},{"radius",.9},{"speed",.55}});add("fisheye",{{"strength",.8}});add("color_cycle",{{"speed",.25}});}
        else if(preset=="ghost_echo"){add("motion_amplify",{{"factor",9},{"radius",3},{"threshold",3}});add("recursive_trails",{{"frames",7},{"decay",.72}});add("xray_edges",{{"strength",.8},{"palette",8}});}
        else if(preset=="channel_surfer"){add("field_corruption");add("bad_tv",{{"noise",13},{"scanlines",.3},{"roll",.025}});add("frame_skip",{{"step",3}});}
        else {add("erosion_decay",{{"iterations",3}});add("dilation_bloom",{{"iterations",2}});add("threshold",{{"level",.46}});add("vignette",{{"strength",.78}});}
        changed.push_back(item->id);
    }
    if(changed.empty())throw std::invalid_argument("visual preset requires selected video");
    return result(std::move(changed),{}, {},"Visual preset: "+preset);
}

const std::vector<YtpPresetDescriptor>& YtpToolkit::audioPresets(){
    static const std::vector<YtpPresetDescriptor> values{
        {"alien_swarm","Alien Swarm","Ring-modulated voice multiplied into an unstable chorus"},
        {"phase_vortex","Phase Vortex","Wide stereo motion spiraling through a deep phase sweep"},
        {"glass_machine","Glass Machine","Brittle crystalline transients with metallic chorus and bite"},
        {"stereo_panic","Stereo Panic","Rapid widening, crossfeed, echoes, and moving phase"},
        {"cyber_demon","Cyber Demon","Low robotic ring modulation driven into dark saturation"},
        {"stadium_clone","Stadium Clone","A crowd of voices spread through a huge reflective room"},
        {"robot_radio","Robot Radio","Telephone band-limit, rhythmic tremolo, and digital crush"},
        {"demon_bass","Demon Bass","Sub-heavy saturation with an unstable low pitch wobble"},
        {"chipmunk_panic","Chipmunk Panic","High pitch, frantic volume chops, and exaggerated treble"},
        {"haunted_hall","Haunted Hall","Large dark reverb, echo, and slow spectral wobble"},
        {"laser_voice","Laser Voice","Fast jet flanging through a narrow electronic tone"},
        {"broken_speaker","Broken Speaker","Hard clipping, low-resolution audio, compression, and limiting"}
    };
    return values;
}

ToolkitResult YtpToolkit::applyAudioPreset(Sequence& sequence,const std::vector<Id>& ids,const std::string& preset){
    const bool found=std::any_of(audioPresets().begin(),audioPresets().end(),[&](const auto&value){return value.id==preset;});
    if(!found)throw std::invalid_argument("unknown audio preset");
    auto items=selectedItems(sequence,ids);std::vector<Id> changed;
    for(auto*item:items){
        const auto*track=sequence.findTrack(item->trackId);if(!track||track->kind!=TrackKind::Audio)continue;
        const auto add=[&](const std::string&type,std::initializer_list<std::pair<const char*,double>> values={}){item->effects.push_back(effect(type,values));};
        if(preset=="alien_swarm"){add("ring_mod",{{"frequency",84},{"mix",.82}});add("chorus",{{"depth",.78},{"speed",1.7},{"mix",.9}});add("highpass",{{"frequency",180}});add("limiter",{{"ceiling",-1}});}
        else if(preset=="phase_vortex"){add("phaser",{{"speed",.62},{"decay",.72},{"depth",.92}});add("stereo_widen",{{"delay",27},{"feedback",.48},{"width",.92}});add("reverb",{{"room",.55},{"damping",.42},{"mix",.25}});}
        else if(preset=="glass_machine"){add("crystalizer",{{"intensity",6.8},{"clipping",1}});add("chorus",{{"depth",.35},{"speed",2.4},{"mix",.55}});add("treble_boost",{{"gain",9},{"frequency",6200}});add("limiter",{{"ceiling",-1}});}
        else if(preset=="stereo_panic"){add("stereo_widen",{{"delay",42},{"feedback",.62},{"width",1}});add("phaser",{{"speed",1.55},{"decay",.48},{"depth",.75}});add("delay",{{"time",115},{"feedback",.28},{"mix",.2}});}
        else if(preset=="cyber_demon"){item->pitchSemitones=std::max(-48.0,item->pitchSemitones-5);add("ring_mod",{{"frequency",52},{"mix",.68}});add("distortion",{{"drive",12},{"mix",.76}});add("lowpass",{{"frequency",4200}});add("limiter",{{"ceiling",-1}});}
        else if(preset=="stadium_clone"){add("chorus",{{"depth",.68},{"speed",.55},{"mix",.86}});add("reverb",{{"room",.9},{"damping",.35},{"mix",.5}});add("stereo_widen",{{"delay",22},{"feedback",.3},{"width",.88}});}
        else if(preset=="robot_radio"){add("telephone",{{"low",360},{"high",3000},{"drive",4}});add("tremolo",{{"rate",12},{"depth",.58}});add("bitcrush",{{"bits",7},{"rate",10000}});}
        else if(preset=="demon_bass"){item->pitchSemitones=std::max(-48.0,item->pitchSemitones-7);add("bass_boost",{{"gain",14},{"frequency",90}});add("distortion",{{"drive",10},{"mix",.72}});add("vibrato",{{"rate",3},{"depth",.22}});add("limiter",{{"ceiling",-1}});}
        else if(preset=="chipmunk_panic"){item->pitchSemitones=std::min(48.0,item->pitchSemitones+10);add("tremolo",{{"rate",15},{"depth",.48}});add("treble_boost",{{"gain",12},{"frequency",5500}});add("compressor",{{"threshold",-16},{"ratio",5}});}
        else if(preset=="haunted_hall"){add("reverb",{{"room",.88},{"damping",.22},{"mix",.58}});add("delay",{{"time",310},{"feedback",.48},{"mix",.34}});add("vibrato",{{"rate",2.2},{"depth",.16}});}
        else if(preset=="laser_voice"){add("flanger",{{"delay",2},{"depth",7},{"speed",2.5}});add("telephone",{{"low",500},{"high",4400},{"drive",2}});add("treble_boost",{{"gain",8},{"frequency",6500}});}
        else {add("distortion",{{"drive",18},{"mix",.9}});add("bitcrush",{{"bits",5},{"rate",6000}});add("compressor",{{"threshold",-22},{"ratio",10},{"attack",3},{"release",90}});add("limiter",{{"ceiling",-1}});}
        changed.push_back(item->id);
    }
    if(changed.empty())throw std::invalid_argument("audio preset requires selected audio");
    sequence.masterLimiter=true;
    return result(std::move(changed),{}, {},"Audio preset: "+preset);
}

const std::vector<YtpPresetDescriptor>& YtpToolkit::combinedPresets(){
    static const std::vector<YtpPresetDescriptor> values{
        {"feedback_scream","Feedback Scream","Recursive picture feedback tightens as the voice rises into a metallic alarm"},
        {"sorted_voice","Sorted Voice","Pixel order collapses while speech is quantized into a brittle digital carrier"},
        {"projector_ghost","Projector Ghost","A wandering damaged film projection carries a distant spectral duplicate"},
        {"analog_demon","Analog Demon","Broken horizontal sync and unstable film transport seize a low robotic transmission"},
        {"lightwriter","Lightwriter","Moving highlights burn into the screen while echoes write across the stereo field"},
        {"clone_chorus","Clone Chorus","A wall of picture clones sings as a wide detuned crowd"},
        {"strip_screamer","Strip Screamer","The picture tears into reordered strips while the voice is chopped across stereo"},
        {"mosaic_choir","Mosaic Choir","Nine moments sing together as a widened crowd of detuned voices"},
        {"gravity_bass","Gravity Bass","A central vortex bends around synthesized subharmonic pressure"},
        {"melting_voice","Melting Voice","The image liquefies while speech dissolves into spectral breath"},
        {"newsroom_possession","Newsroom Possession","Printed broadcast imagery is seized by a clipped robotic announcer"},
        {"stop_motion_robot","Stop-Motion Robot","Skipped physical motion locks to rigid phase-less machine speech"},
        {"cell_growth","Cell Growth","Image regions grow like organisms while voices multiply into a living chorus"},
        {"video_rot","Video Rot","Picture detail erodes as sound decays into dark overloaded bandwidth"},
        {"interlace_panic","Interlace Panic","Exchanged video fields are synchronized to gated digital panic"},
        {"chroma_phantom","Chroma Phantom","Stolen color planes drift apart with an unnaturally wide phase ghost"},
        {"sonic_shockwave","Sonic Shockwave","Radial image impacts arrive with compressed sub-bass detonations"},
        {"heatstroke","Heatstroke","Temporal heat burns the image while distorted bass overloads the room"},
        {"stained_memory","Stained Memory","Past frames stain the present inside a long delayed hall"},
        {"surveillance_entity","Surveillance Entity","A forensic scope captures a hidden machine speaking over radio"},
        {"cartoon_explosion","Cartoon Explosion","Printed X-ray impact frames strike with glass-like transient attacks"},
        {"reality_blender","Reality Blender","Funhouse space and vortex motion churn through a sweeping stereo phase"},
        {"buffer_underrun","Buffer Underrun","Frames skip and reorder while audio gates collapse into low-bit debris"},
        {"cosmic_dialup","Cosmic Dial-Up","A planetary signal tunnel transmits through modem-like robotic radio"},
        {"reality_collapse","Reality Collapse","Perspective and projection implode while the voice falls through a frequency vortex"},
        {"brain_scrambler","Brain Scrambler","Blocks and frames reorder as the audio becomes metallic and quantized"},
        {"possessed_broadcast","Possessed Broadcast","A hostile signal takeover with robotic speech and broken transmission audio"},
        {"vaporized","Vaporized","The subject dissolves into spectral afterimages and whispered air"},
        {"bass_quake","Bass Quake","Every low hit punches the camera inward with subharmonic weight"},
        {"time_machine","Time Machine Malfunction","Temporal order, trails, pitch, and phase all lose synchronization together"},
        {"alien_abduction","Alien Abduction","The frame bends into a portal while voices rise into an alien swarm"},
        {"security_breach","Security Breach","Forensic scopes and scrambled telemetry meet clipped surveillance radio"},
        {"comic_impact","Comic Impact","X-ray contours, hard zoom, crystalline attacks, and exaggerated transient punch"},
        {"dream_melt","Dream Melt","Elastic liquid imagery paired with a huge drifting choral space"},
        {"digital_shred","Digital Shred","Image blocks and RGB strips tear apart with crushed robotic audio"},
        {"void_portal","Void Portal","Spinning stereographic space pulls sound outward into a phase-warped void"}
    };
    return values;
}

ToolkitResult YtpToolkit::applyCombinedPreset(Sequence& sequence,const std::vector<Id>& ids,const std::string& preset){
    const bool found=std::any_of(combinedPresets().begin(),combinedPresets().end(),[&](const auto&value){return value.id==preset;});
    if(!found)throw std::invalid_argument("unknown combined preset");
    auto items=selectedItems(sequence,ids);std::vector<Id> changed;bool changedAudio=false;
    for(auto*item:items){
        const auto*track=sequence.findTrack(item->trackId);if(!track)continue;
        const auto add=[&](const std::string&type,std::initializer_list<std::pair<const char*,double>> values={}){item->effects.push_back(effect(type,values));};
        if(track->kind==TrackKind::Video){
            if(preset=="feedback_scream"){add("video_feedback",{{"phase",.34},{"zoom",.88}});add("vortex",{{"strength",1.8},{"radius",.78},{"speed",.8}});add("edge_glow_native",{{"strength",.76}});}
            else if(preset=="sorted_voice"){add("pixel_sort");add("ordered_dither",{{"levels",.28},{"pattern",.7}});add("digital_glitch",{{"frequency",.35},{"blockHeight",.18},{"shift",.66},{"color",.55}});}
            else if(preset=="projector_ghost"){add("film_projector",{{"grain",.56},{"dust",.72},{"flicker",.58}});add("film_gate_weave",{{"interval",.3},{"horizontal",.7},{"vertical",.58}});add("frame_delay",{{"delay",.52}});}
            else if(preset=="analog_demon"){add("analog_nosync",{{"strength",.92}});add("nervous_frames",{{"strength",.7}});add("film_gate_weave",{{"interval",.16},{"horizontal",.84},{"vertical",.62}});}
            else if(preset=="lightwriter"){add("light_graffiti",{{"sensitivity",.68},{"decay",.86}});add("soft_glow_native",{{"strength",.74}});add("frame_delay",{{"delay",.36}});}
            else if(preset=="clone_chorus"){add("clone_grid",{{"columns",.58},{"rows",.5}});add("elastic_scale",{{"centerX",.5},{"centerY",.5},{"strength",.42}});add("color_cycle",{{"speed",.16}});}
            else if(preset=="strip_screamer"){add("horizontal_shuffle",{{"strip",12},{"seed",313}});add("vertical_shuffle",{{"strip",21},{"seed",919}});add("glitch_bands",{{"amount",54},{"band",31},{"speed",190}});}
            else if(preset=="mosaic_choir"){add("temporal_mosaic",{{"grid",3},{"border",3}});add("motion_burn",{{"decay",.94}});add("color_cycle",{{"speed",.15}});}
            else if(preset=="gravity_bass"){add("vortex",{{"strength",4.6},{"radius",.72},{"speed",.62}});add("radial_ripple",{{"amount",.16},{"spacing",24},{"speed",5}});add("vignette",{{"strength",.92}});}
            else if(preset=="melting_voice"){add("melt",{{"amount",74},{"spacing",13},{"speed",3.8}});add("temporal_stain",{{"opacity",.78}});add("thermal",{{"palette",10}});}
            else if(preset=="newsroom_possession"){add("halftone",{{"cell",2.7},{"contrast",69}});add("oscilloscope",{{"size",.7},{"tilt",.08},{"opacity",.92}});add("frame_skip",{{"step",3}});}
            else if(preset=="stop_motion_robot"){add("frame_skip",{{"step",7}});add("motion_amplify",{{"factor",8},{"radius",2},{"threshold",3}});add("threshold",{{"level",.44}});}
            else if(preset=="cell_growth"){add("dilation_bloom",{{"iterations",5}});add("motion_burn",{{"decay",.96}});add("thermal",{{"palette",11}});}
            else if(preset=="video_rot"){add("erosion_decay",{{"iterations",5}});add("temporal_stain",{{"opacity",.88}});add("bad_tv",{{"noise",9},{"scanlines",.2},{"roll",.015}});}
            else if(preset=="interlace_panic"){add("field_corruption");add("frame_xor",{{"opacity",.5}});add("vertical_roll",{{"speed",.12}});}
            else if(preset=="chroma_phantom"){add("chroma_planes",{{"mode",2}});add("rgb_split",{{"offset",22},{"vertical",9}});add("recursive_trails",{{"frames",5},{"decay",.67}});}
            else if(preset=="sonic_shockwave"){add("radial_ripple",{{"amount",.4},{"spacing",17},{"speed",15}});add("impact_zoom",{{"zoom",1.45}});add("screen_shake",{{"amount",13},{"speed",34}});}
            else if(preset=="heatstroke"){add("temporal_heat",{{"opacity",.94}});add("thermal",{{"palette",7}});add("motion_amplify",{{"factor",6},{"radius",2},{"threshold",4}});}
            else if(preset=="stained_memory"){add("temporal_stain",{{"opacity",.9}});add("recursive_trails",{{"frames",9},{"decay",.76}});add("chroma_planes",{{"mode",1}});}
            else if(preset=="surveillance_entity"){add("oscilloscope",{{"size",.88},{"tilt",.16},{"opacity",1}});add("xray_edges",{{"strength",1.8},{"palette",9}});add("frame_skip",{{"step",4}});}
            else if(preset=="cartoon_explosion"){add("halftone",{{"cell",2},{"contrast",74}});add("xray_edges",{{"strength",2.6},{"palette",14}});add("impact_zoom",{{"zoom",1.6}});}
            else if(preset=="reality_blender"){add("funhouse",{{"amount",48},{"spacing",19},{"speed",8}});add("vortex",{{"strength",2.3},{"radius",.9},{"speed",1.7}});add("radial_ripple",{{"amount",.12},{"spacing",16},{"speed",7}});}
            else if(preset=="buffer_underrun"){add("frame_randomizer",{{"frames",18},{"seed",404}});add("frame_skip",{{"step",5}});add("block_shuffle",{{"width",20},{"height",16},{"seed",500}});}
            else if(preset=="cosmic_dialup"){add("tiny_planet",{{"strength",3.7},{"radius",.82},{"speed",.5}});add("oscilloscope",{{"size",.58},{"tilt",.88},{"opacity",.76}});add("color_cycle",{{"speed",.22}});}
            else if(preset=="reality_collapse"){add("perspective_tilt",{{"amount",.32},{"vertical",.2}});add("tiny_planet",{{"strength",3.2},{"radius",.8},{"speed",.45}});add("motion_burn",{{"decay",.975}});}
            else if(preset=="brain_scrambler"){add("block_shuffle",{{"width",38},{"height",26},{"seed",731}});add("frame_randomizer",{{"frames",11},{"seed",99}});add("frame_xor",{{"opacity",.6}});}
            else if(preset=="possessed_broadcast"){add("glitch_bands",{{"amount",86},{"band",36},{"speed",210}});add("oscilloscope",{{"size",.82},{"tilt",.7},{"opacity",.88}});add("bad_tv",{{"noise",26},{"scanlines",.38},{"roll",.07}});}
            else if(preset=="vaporized"){add("motion_amplify",{{"factor",12},{"radius",3},{"threshold",2}});add("motion_burn",{{"decay",.989}});add("thermal",{{"palette",10}});}
            else if(preset=="bass_quake"){add("impact_zoom",{{"zoom",1.72},{"offsetY",.08}});add("screen_shake",{{"amount",22},{"speed",38}});add("motion_amplify",{{"factor",5},{"radius",2},{"threshold",4}});}
            else if(preset=="time_machine"){add("frame_randomizer",{{"frames",15},{"seed",1985}});add("recursive_trails",{{"frames",10},{"decay",.79}});add("vertical_roll",{{"speed",-.12}});}
            else if(preset=="alien_abduction"){add("fisheye",{{"horizontalFov",175},{"verticalFov",130}});add("elastic_wave",{{"amount",28},{"spacing",16},{"speed",7}});add("color_cycle",{{"speed",.33}});}
            else if(preset=="security_breach"){add("oscilloscope",{{"size",.64},{"tilt",.14},{"opacity",1}});add("block_shuffle",{{"width",18},{"height",12},{"seed",404}});add("threshold",{{"level",.38}});}
            else if(preset=="comic_impact"){add("xray_edges",{{"strength",3.2},{"palette",14}});add("impact_zoom",{{"zoom",1.5}});add("pixel_bloom",{{"width",9},{"height",9},{"mode",2}});}
            else if(preset=="dream_melt"){add("elastic_wave",{{"amount",42},{"spacing",28},{"speed",4.5}});add("motion_burn",{{"decay",.982}});add("shear",{{"horizontal",.16},{"vertical",.09}});add("color_cycle",{{"speed",.12}});}
            else if(preset=="digital_shred"){add("block_shuffle",{{"width",24},{"height",18},{"seed",808}});add("glitch_bands",{{"amount",110},{"band",28},{"speed",260}});add("rgb_split",{{"offset",34},{"vertical",14}});}
            else {add("tiny_planet",{{"horizontalFov",260},{"verticalFov",190},{"roll",35}});add("spin",{{"speed",.9}});add("motion_burn",{{"decay",.98}});add("vignette",{{"strength",.82}});}
            changed.push_back(item->id);
        }else if(track->kind==TrackKind::Audio){
            changedAudio=true;
            if(preset=="feedback_scream"){add("frequency_shift",{{"shift",210},{"level",.86},{"order",14}});add("delay",{{"time",145},{"feedback",.72},{"mix",.52}});add("ring_mod",{{"frequency",74},{"mix",.48}});add("limiter",{{"ceiling",-1}});}
            else if(preset=="sorted_voice"){add("robotize",{{"window",128},{"overlap",.76}});add("bitcrush",{{"bits",3},{"rate",4600}});add("noisegate",{{"threshold",-27},{"release",22}});add("limiter",{{"ceiling",-1}});}
            else if(preset=="projector_ghost"){add("whisperize",{{"window",256},{"overlap",.82}});add("delay",{{"time",310},{"feedback",.48},{"mix",.42}});add("reverb",{{"room",.88},{"damping",.38},{"mix",.52}});}
            else if(preset=="analog_demon"){add("robotize",{{"window",512},{"overlap",.8}});add("telephone",{{"low",360},{"high",2450},{"drive",9}});add("tremolo",{{"rate",11},{"depth",.68}});add("frequency_shift",{{"shift",-115},{"level",.82}});}
            else if(preset=="lightwriter"){add("delay",{{"time",185},{"feedback",.64},{"mix",.55}});add("haas_spread",{{"leftDelay",3},{"rightDelay",29},{"sideGain",1.65}});add("crystalizer",{{"intensity",4.8},{"clipping",0}});}
            else if(preset=="clone_chorus"){add("chorus",{{"depth",1},{"speed",.52},{"mix",1}});add("haas_spread",{{"leftDelay",6},{"rightDelay",35},{"sideGain",1.9}});add("phaser",{{"speed",.34},{"decay",.7},{"depth",.8}});add("limiter",{{"ceiling",-1}});}
            else if(preset=="strip_screamer"){add("tremolo",{{"rate",18},{"depth",.74}});add("haas_spread",{{"leftDelay",2},{"rightDelay",26},{"sideGain",1.55}});add("ring_mod",{{"frequency",132},{"mix",.42}});}
            else if(preset=="mosaic_choir"){add("chorus",{{"depth",.92},{"speed",.36},{"mix",1}});add("haas_spread",{{"leftDelay",8},{"rightDelay",32},{"sideGain",1.8}});add("reverb",{{"room",.74},{"damping",.32},{"mix",.38}});}
            else if(preset=="gravity_bass"){add("virtual_bass",{{"cutoff",145},{"strength",3}});add("frequency_shift",{{"shift",-72},{"level",.92},{"order",14}});add("phaser",{{"speed",.22},{"decay",.8},{"depth",.88}});add("limiter",{{"ceiling",-1}});}
            else if(preset=="melting_voice"){add("whisperize",{{"window",96},{"overlap",.88}});add("frequency_shift",{{"shift",-130},{"level",.8}});add("reverb",{{"room",.92},{"damping",.15},{"mix",.58}});}
            else if(preset=="newsroom_possession"){add("robotize",{{"window",512},{"overlap",.8}});add("telephone",{{"low",440},{"high",2800},{"drive",7}});add("compressor",{{"threshold",-18},{"ratio",8},{"attack",2},{"release",70}});}
            else if(preset=="stop_motion_robot"){add("robotize",{{"window",256},{"overlap",.72}});add("tremolo",{{"rate",9},{"depth",.86}});add("noisegate",{{"threshold",-28},{"release",30}});}
            else if(preset=="cell_growth"){add("chorus",{{"depth",.95},{"speed",.2},{"mix",.96}});add("virtual_bass",{{"cutoff",210},{"strength",1.8}});add("reverb",{{"room",.66},{"damping",.5},{"mix",.34}});}
            else if(preset=="video_rot"){add("lowpass",{{"frequency",2400}});add("distortion",{{"drive",16},{"mix",.82}});add("bitcrush",{{"bits",6},{"rate",7600}});add("limiter",{{"ceiling",-1}});}
            else if(preset=="interlace_panic"){add("tremolo",{{"rate",15},{"depth",.88}});add("bitcrush",{{"bits",5},{"rate",8200}});add("noisegate",{{"threshold",-30},{"release",28}});}
            else if(preset=="chroma_phantom"){add("haas_spread",{{"leftDelay",1},{"rightDelay",37},{"sideGain",1.9}});add("phaser",{{"speed",.31},{"decay",.77},{"depth",.94}});add("whisperize",{{"window",256},{"overlap",.68}});}
            else if(preset=="sonic_shockwave"){add("virtual_bass",{{"cutoff",130},{"strength",3}});add("bass_boost",{{"gain",18},{"frequency",75}});add("compressor",{{"threshold",-22},{"ratio",12},{"attack",1},{"release",95}});add("limiter",{{"ceiling",-1}});}
            else if(preset=="heatstroke"){add("virtual_bass",{{"cutoff",190},{"strength",2.6}});add("distortion",{{"drive",18},{"mix",.86}});add("tremolo",{{"rate",5},{"depth",.32}});add("limiter",{{"ceiling",-1}});}
            else if(preset=="stained_memory"){add("delay",{{"time",420},{"feedback",.6},{"mix",.46}});add("reverb",{{"room",.94},{"damping",.18},{"mix",.6}});add("chorus",{{"depth",.46},{"speed",.18},{"mix",.52}});}
            else if(preset=="surveillance_entity"){add("robotize",{{"window",1024},{"overlap",.82}});add("telephone",{{"low",500},{"high",2500},{"drive",5}});add("noisegate",{{"threshold",-36},{"release",55}});}
            else if(preset=="cartoon_explosion"){add("crystalizer",{{"intensity",8.8},{"clipping",1}});add("compressor",{{"threshold",-20},{"ratio",10},{"attack",1},{"release",55}});add("treble_boost",{{"gain",11},{"frequency",4800}});add("limiter",{{"ceiling",-1}});}
            else if(preset=="reality_blender"){add("phaser",{{"speed",1.25},{"decay",.72},{"depth",.96}});add("stereo_widen",{{"delay",34},{"feedback",.54},{"width",1}});add("frequency_shift",{{"shift",55},{"level",.86}});}
            else if(preset=="buffer_underrun"){add("noisegate",{{"threshold",-24},{"release",18}});add("bitcrush",{{"bits",3},{"rate",4200}});add("tremolo",{{"rate",20},{"depth",.76}});add("limiter",{{"ceiling",-1}});}
            else if(preset=="cosmic_dialup"){add("robotize",{{"window",128},{"overlap",.75}});add("telephone",{{"low",620},{"high",3100},{"drive",6}});add("flanger",{{"delay",2},{"depth",8},{"speed",3.2}});add("frequency_shift",{{"shift",170},{"level",.82}});}
            else if(preset=="reality_collapse"){add("frequency_shift",{{"shift",-240},{"level",.9},{"order",14}});add("phaser",{{"speed",.38},{"decay",.76},{"depth",.9}});add("virtual_bass",{{"cutoff",180},{"strength",2.5}});}
            else if(preset=="brain_scrambler"){add("ring_mod",{{"frequency",117},{"mix",.78}});add("bitcrush",{{"bits",5},{"rate",7200}});add("robotize",{{"window",256},{"overlap",.8}});}
            else if(preset=="possessed_broadcast"){add("robotize",{{"window",512},{"overlap",.78}});add("telephone",{{"low",420},{"high",2900},{"drive",6}});add("tremolo",{{"rate",14},{"depth",.62}});}
            else if(preset=="vaporized"){add("whisperize",{{"window",128},{"overlap",.84}});add("reverb",{{"room",.92},{"damping",.18},{"mix",.64}});add("haas_spread",{{"leftDelay",4},{"rightDelay",24},{"sideGain",1.5}});}
            else if(preset=="bass_quake"){add("virtual_bass",{{"cutoff",150},{"strength",3}});add("bass_boost",{{"gain",16},{"frequency",82}});add("compressor",{{"threshold",-20},{"ratio",9},{"attack",3},{"release",110}});add("limiter",{{"ceiling",-1}});}
            else if(preset=="time_machine"){add("frequency_shift",{{"shift",-95},{"level",.88},{"order",10}});add("chorus",{{"depth",.72},{"speed",.42},{"mix",.8}});add("phaser",{{"speed",.25},{"decay",.65},{"depth",.82}});}
            else if(preset=="alien_abduction"){add("frequency_shift",{{"shift",330},{"level",.84},{"order",16}});add("chorus",{{"depth",.8},{"speed",1.45},{"mix",.9}});add("ring_mod",{{"frequency",43},{"mix",.36}});}
            else if(preset=="security_breach"){add("telephone",{{"low",520},{"high",2600},{"drive",8}});add("noisegate",{{"threshold",-32},{"release",45}});add("bitcrush",{{"bits",6},{"rate",9000}});}
            else if(preset=="comic_impact"){add("crystalizer",{{"intensity",7.5},{"clipping",1}});add("treble_boost",{{"gain",10},{"frequency",5200}});add("compressor",{{"threshold",-18},{"ratio",7},{"attack",2},{"release",70}});}
            else if(preset=="dream_melt"){add("chorus",{{"depth",.85},{"speed",.28},{"mix",.92}});add("reverb",{{"room",.96},{"damping",.25},{"mix",.62}});add("haas_spread",{{"leftDelay",7},{"rightDelay",31},{"sideGain",1.7}});}
            else if(preset=="digital_shred"){add("robotize",{{"window",256},{"overlap",.72}});add("bitcrush",{{"bits",4},{"rate",5000}});add("ring_mod",{{"frequency",96},{"mix",.72}});add("limiter",{{"ceiling",-1}});}
            else {add("phaser",{{"speed",.3},{"decay",.8},{"depth",.95}});add("haas_spread",{{"leftDelay",2},{"rightDelay",34},{"sideGain",1.8}});add("reverb",{{"room",.88},{"damping",.3},{"mix",.48}});add("frequency_shift",{{"shift",-60},{"level",.9}});}
            changed.push_back(item->id);
        }
    }
    if(changed.empty())throw std::invalid_argument("combined preset requires selected media");
    if(changedAudio)sequence.masterLimiter=true;
    return result(std::move(changed),{}, {},"Audio + visual preset: "+preset);
}

ToolkitResult YtpToolkit::sentenceMixer(Sequence& sequence,const std::vector<Id>& ids,const std::vector<int>& order){const auto block=selectionBlock(sequence,ids);std::vector<Rational> cuts{block.start};for(const auto& marker:sequence.markers)if(marker.time>block.start&&marker.time<block.start+block.duration)cuts.push_back(marker.time);cuts.push_back(block.start+block.duration);std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());const int chunks=static_cast<int>(cuts.size())-1;if(chunks<2)throw std::invalid_argument("place at least one marker inside the selected phrase");std::vector<int> actual=order;if(actual.empty())for(int i=0;i<chunks;++i)actual.push_back(i);for(const auto index:actual)if(index<0||index>=chunks)throw std::invalid_argument("sentence order references a missing chunk");std::vector<TimelineItem> generated;Rational cursor=block.start;for(const auto index:actual){const auto chunkStart=cuts[static_cast<std::size_t>(index)];const auto chunkDuration=cuts[static_cast<std::size_t>(index+1)]-chunkStart;const auto link=repetitionLink(block);for(const auto& source:block.items){const auto range=sourceSlice(source,chunkStart-block.start,chunkDuration);auto item=clone(source,cursor,chunkDuration,range.start(),range.duration(),link);generated.push_back(std::move(item));}cursor=cursor+chunkDuration;}const auto duration=cursor-block.start;auto created=replaceBlock(sequence,block,std::move(generated),duration);return result(std::move(created),block.start,duration,"Sentence Mixer: "+std::to_string(actual.size())+" chunks");}

ToolkitResult YtpToolkit::applyMacro(Sequence& sequence,std::vector<Id> ids,const YtpMacro& macro){
    if(macro.steps.empty())throw std::invalid_argument("macro has no steps");
    ToolkitResult last;
    for(const auto&step:macro.steps){
        switch(step.tool){
        case YtpTool::Stutter:last=stutter(sequence,ids,static_cast<int>(number(step,"repeats",4)),Rational{static_cast<std::int64_t>(number(step,"sliceMs",100)),1000},number(step,"alternate",0)!=0);break;
        case YtpTool::RapidReverse:last=rapidReverse(sequence,ids,static_cast<int>(number(step,"segments",6)),Rational{static_cast<std::int64_t>(number(step,"segmentMs",80)),1000});break;
        case YtpTool::FrameRepeat:last=frameRepeat(sequence,ids,Rational{static_cast<std::int64_t>(number(step,"frameRateDen",1001)),static_cast<std::int64_t>(number(step,"frameRateNum",30000))},static_cast<int>(number(step,"sourceFrames",2)),static_cast<int>(number(step,"repeats",4)));break;
        case YtpTool::SpeedLadder:last=speedLadder(sequence,ids,static_cast<int>(number(step,"steps",5)),number(step,"startSpeed",.5),number(step,"endSpeed",3),number(step,"pitchStep",2),number(step,"preservePitch",0)!=0);break;
        case YtpTool::SafeEarrape:last=safeEarrape(sequence,ids,number(step,"intensity",.65));break;
        case YtpTool::VisualPreset:last=applyVisualPreset(sequence,ids,text(step,"preset","deep_fried"));break;
        case YtpTool::AudioPreset:last=applyAudioPreset(sequence,ids,text(step,"preset","robot_radio"));break;
        case YtpTool::CombinedPreset:last=applyCombinedPreset(sequence,ids,text(step,"preset","reality_collapse"));break;
        case YtpTool::SentenceMixer:{std::vector<int> order;std::string value=text(step,"order");std::size_t begin=0;while(begin<value.size()){const auto end=value.find(',',begin);order.push_back(std::stoi(value.substr(begin,end-begin)));if(end==std::string::npos)break;begin=end+1;}last=sentenceMixer(sequence,ids,order);break;}
        case YtpTool::RhythmRepeat:last=rhythmRepeat(sequence,ids,Rational{static_cast<std::int64_t>(number(step,"atMs",0)),1000},number(step,"bpm",120),static_cast<int>(number(step,"beats",8)),Rational{static_cast<std::int64_t>(number(step,"gateMs",100)),1000},number(step,"markers",0)!=0);break;
        }
        ids=last.itemIds;
    }
    last.summary="Macro "+macro.name+": "+std::to_string(macro.steps.size())+" steps";return last;
}

RandomizationPlan YtpToolkit::previewRandomizer(const Sequence& sequence,const std::vector<Id>& ids,const RandomizerOptions& options) {
    if(!std::isfinite(options.minSpeed)||!std::isfinite(options.maxSpeed)||!std::isfinite(options.minPitch)||!std::isfinite(options.maxPitch)||!std::isfinite(options.reverseProbability)||!std::isfinite(options.effectProbability)||options.minSpeed<.05||options.maxSpeed<options.minSpeed||options.maxSpeed>16||options.minPitch< -48||options.maxPitch<options.minPitch||options.maxPitch>48||
       options.reverseProbability<0||options.reverseProbability>1||
       options.effectProbability<0||options.effectProbability>1)
        throw std::invalid_argument("invalid randomizer settings");
    RandomizationPlan plan{sequence,sequence,TimelineEditor::expandedSelection(sequence,ids),options.seed,{}};
    if(plan.selectedIds.empty()) throw std::invalid_argument("select events to randomize");
    std::mt19937_64 random(options.seed);
    std::uniform_real_distribution<double> unit(0,1),speed(options.minSpeed,options.maxSpeed),pitch(options.minPitch,options.maxPitch);
    std::map<std::string,std::vector<TimelineItem*>> groups;
    for(const auto& id:plan.selectedIds)if(auto* item=plan.after.findItem(id)){const auto*track=plan.after.findTrack(item->trackId);if(!track||track->locked)throw std::invalid_argument("a selected track is locked");groups[item->linkedGroupId.empty()?item->id:item->linkedGroupId].push_back(item);}
    if(options.shuffle&&groups.size()>1){
        std::vector<Rational> positions;for(const auto& [key,items]:groups){(void)key;positions.push_back(items.front()->timelineStart);}
        std::shuffle(positions.begin(),positions.end(),random);std::size_t index=0;
        for(auto& [key,items]:groups){(void)key;for(auto* item:items)item->timelineStart=positions[index];++index;}
        plan.changes.push_back("Shuffled "+std::to_string(groups.size())+" linked events");
    }
    static const std::vector<std::string> videoFx{"invert","pixelate","posterize","hue","threshold"};
    for(auto& [key,items]:groups){
        const bool reverse=unit(random)<options.reverseProbability;const auto newSpeed=speed(random);const auto newPitch=pitch(random);
        for(auto* item:items){if(reverse)item->reverse=!item->reverse;item->speed=newSpeed;item->duration=item->sourceRange.duration()/speedRatio(newSpeed);item->pitchSemitones=newPitch;}
        if(reverse)plan.changes.push_back("Reverse "+key.substr(0,8));
        plan.changes.push_back("Speed "+key.substr(0,8)+" = "+std::to_string(newSpeed));
        if(unit(random)<options.effectProbability){
            const auto& type=videoFx[static_cast<std::size_t>(random()%videoFx.size())];bool applied=false;
            for(auto* item:items){const auto* track=plan.after.findTrack(item->trackId);if(track&&track->kind==TrackKind::Video){auto generated=effect(type);generated.id=deterministicId(random);item->effects.push_back(std::move(generated));applied=true;}}
            if(applied)plan.changes.push_back("Added "+type);
        }
    }
    sortTracks(plan.after);if(const auto error=plan.after.validate())throw std::invalid_argument(*error);return plan;
}

void YtpToolkit::commitRandomizer(Sequence& sequence,const RandomizationPlan& plan){if(!(sequence==plan.before))throw std::invalid_argument("timeline changed after randomizer preview");sequence=plan.after;}

} // namespace ytp
