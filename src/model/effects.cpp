#include "model/effects.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ytp {
namespace {
EffectParameter p(std::string name, double value, double low, double high, std::string unit = {}) {
    return {.name=std::move(name),.value=value,.minimum=low,.maximum=high,.unit=std::move(unit),.keyframes={}};
}
const std::vector<EffectDescriptor> catalog{
    {"brightness_contrast","Brightness / Contrast",false,{p("brightness",0,-1,1),p("contrast",1,0,3)}},
    {"saturation","Saturation",false,{p("amount",1,0,3)}},
    {"hue","Hue Shift",false,{p("degrees",0,-180,180,"deg")}},
    {"invert","Invert",false,{p("amount",1,0,1)}},
    {"grayscale","Grayscale",false,{p("amount",1,0,1)}},
    {"blur","Blur",false,{p("radius",5,0,50,"px")}},
    {"sharpen","Sharpen",false,{p("amount",1,0,5)}},
    {"pixelate","Pixelate",false,{p("block",12,2,100,"px")}},
    {"posterize","Posterize",false,{p("levels",6,2,32)}},
    {"threshold","Threshold",false,{p("level",0.5,0,1)}},
    {"tint","Color Tint",false,{p("red",1,0,2),p("green",1,0,2),p("blue",1,0,2),p("mix",0.5,0,1)}},
    {"rgb_split","RGB Separation",false,{p("offset",12,-100,100,"px"),p("vertical",0,-100,100,"px")}},
    {"chromatic_aberration","Chromatic Aberration",false,{p("offset",8,-100,100,"px")}},
    {"wave_warp","Wave Warp",false,{p("amount",.08,-1,1),p("speed",.15,-1,1)}},
    {"lens_warp","Bulge / Pinch",false,{p("amount",.35,-1,1),p("secondary",0,-1,1)}},
    {"kaleidoscope","Kaleidoscope",false,{p("strength",.7,-1,1)}},
    {"edge_echo","Edge Echo",false,{p("strength",.4,0,1)}},
    {"recursive_trails","Recursive Frame Trails",false,{p("frames",6,2,30),p("decay",.8,0,1)}},
    {"time_smear","Time Smear",false,{p("frames",8,2,30)}},
    {"frame_blend","Frame Blend",false,{p("mode",1,0,1)}},
    {"screen_shake","Screen Shake",false,{p("amount",12,0,100,"px"),p("speed",18,1,60,"Hz")}},
    {"chroma_key","Chroma Key",false,{p("similarity",.15,.01,1),p("blend",.05,0,1)}},
    {"datamosh","Datamosh Style",false,{p("decay",.9,0,1)}},
    {"scanlines","CRT Scanlines",false,{p("spacing",4,2,16,"px"),p("opacity",.28,0,1)}},
    {"vhs_noise","VHS Noise",false,{p("strength",18,0,60),p("chroma",5,0,30,"px")}},
    {"solarize","Solarize",false,{p("threshold",.5,0,1)}},
    {"emboss","Emboss",false,{p("amount",1,0,3)}},
    {"neon_edges","Neon Edges",false,{p("strength",.35,.01,1),p("saturation",2.4,0,3)}},
    {"vignette","Hard Vignette",false,{p("strength",.65,.05,1)}},
    {"color_cycle","Color Cycle",false,{p("speed",.6,-4,4,"Hz")}},
    {"strobe","Strobe / Flash",false,{p("rate",8,1,30,"Hz"),p("duty",.45,.05,.95)}},
    {"channel_swap","Channel Swap",false,{p("mix",1,0,1)}},
    {"vertical_roll","Vertical Roll",false,{p("speed",.18,-1,1)}},
    {"bad_tv","Bad TV",false,{p("noise",16,0,60),p("scanlines",.25,0,1),p("roll",.03,-.5,.5)}},
    {"cartoon_edges","Cartoon Crush",false,{p("edge",.28,.01,1),p("saturation",2,0,3)}},
    {"impact_zoom","Impact Zoom",false,{p("zoom",1.4,1,3),p("offsetX",0,-1,1),p("offsetY",0,-1,1)}},
    {"spin","Continuous Spin",false,{p("speed",1.2,-8,8,"rad/s"),p("background",0,0,1)}},
    {"pendulum","Pendulum Rock",false,{p("angle",14,1,90,"deg"),p("speed",1.6,.1,10,"Hz")}},
    {"perspective_tilt","Perspective Collapse",false,{p("amount",.18,-.45,.45),p("vertical",.10,-.4,.4)}},
    {"elastic_wave","Elastic Reality",false,{p("amount",24,1,100,"px"),p("spacing",18,4,100,"px"),p("speed",8,-30,30)}},
    {"glitch_bands","Glitch Slices",false,{p("amount",42,1,180,"px"),p("band",48,8,160,"px"),p("speed",120,-400,400)}},
    {"thermal","Thermal Camera",false,{p("palette",7,0,20)}},
    {"motion_burn","Motion Burn",false,{p("decay",.96,.5,.999)}},
    {"block_shuffle","Block Shuffler",false,{p("width",32,2,256,"px"),p("height",24,2,256,"px"),p("seed",13,0,65535)}},
    {"shear","Reality Shear",false,{p("horizontal",.35,-2,2),p("vertical",-.12,-2,2)}},
    {"fisheye","Spherical Bubble",false,{p("strength",.72,-1.5,2)}},
    {"tiny_planet","Planet Vortex",false,{p("strength",2.4,-8,8),p("radius",.75,.1,2),p("speed",.35,-5,5)}},
    {"oscilloscope","Video Oscilloscope",false,{p("size",.72,.1,1),p("tilt",.22,0,1),p("opacity",.9,0,1)}},
    {"frame_randomizer","Frame Scrambler",false,{p("frames",8,2,60),p("seed",23,0,65535)}},
    {"motion_amplify","Motion Amplifier",false,{p("factor",7,1,40),p("radius",2,1,12),p("threshold",3,0,100)}},
    {"frame_xor","Temporal XOR",false,{p("opacity",1,.05,1)}},
    {"pixel_bloom","Pixel Bloom",false,{p("width",24,2,128,"px"),p("height",18,2,128,"px"),p("mode",2,0,2)}},
    {"xray_edges","X-Ray Scanner",false,{p("strength",2,.1,10),p("palette",7,0,20)}},
    {"horizontal_shuffle","Horizontal Strip Shuffle",false,{p("strip",14,2,256,"px"),p("seed",41,0,65535)}},
    {"vertical_shuffle","Vertical Strip Shuffle",false,{p("strip",14,2,256,"px"),p("seed",73,0,65535)}},
    {"temporal_mosaic","Temporal Mosaic",false,{p("grid",3,2,5),p("border",1,0,12,"px")}},
    {"dilation_bloom","Dilation Bloom",false,{p("iterations",2,1,8)}},
    {"erosion_decay","Erosion Decay",false,{p("iterations",2,1,8)}},
    {"field_corruption","Field Corruption",false,{p("swapLuma",1,0,1),p("swapChroma",1,0,1)}},
    {"chroma_planes","Chroma Plane Exchange",false,{p("mode",1,0,2)}},
    {"frame_skip","Frame Skip Motion",false,{p("step",5,2,20)}},
    {"funhouse","Two-Axis Funhouse",false,{p("amount",28,1,100,"px"),p("spacing",20,4,100,"px"),p("speed",7,-30,30)}},
    {"vortex","Animated Vortex",false,{p("strength",1.7,-8,8),p("radius",.45,.1,2),p("speed",2,-12,12)}},
    {"radial_ripple","Radial Shockwave",false,{p("amount",.18,-.8,.8),p("spacing",14,3,80,"px"),p("speed",8,-30,30)}},
    {"melt","Vertical Melt",false,{p("amount",34,1,160,"px"),p("spacing",18,4,100,"px"),p("speed",5,-30,30)}},
    {"halftone","Halftone Newspaper",false,{p("cell",3,1,16,"px"),p("contrast",55,0,127)}},
    {"temporal_heat","Temporal Heat Blend",false,{p("opacity",.8,.05,1)}},
    {"temporal_stain","Temporal Stain Blend",false,{p("opacity",.8,.05,1)}},
    {"video_feedback","Recursive Video Feedback",false,{p("phase",.18,0,1),p("zoom",.72,0,1)}},
    {"pixel_sort","Luminance Pixel Sort",false,{p("threshold",.5,0,1),p("direction",0,0,1)}},
    {"water_surface","Water Surface",false,{p("amplitude",.5,0,1),p("frequency",.5,0,1)}},
    {"elastic_scale","Elastic Center Stretch",false,{p("centerX",.5,0,1),p("centerY",.5,0,1),p("strength",.65,0,1)}},
    {"analog_nosync","Analog No-Sync",false,{p("strength",.6,0,1)}},
    {"film_gate_weave","Film Gate Weave",false,{p("interval",.35,0,1),p("horizontal",.5,0,1),p("vertical",.35,0,1)}},
    {"nervous_frames","Nervous Frame Memory",false,{p("strength",.65,0,1)}},
    {"light_graffiti","Light Graffiti",false,{p("sensitivity",.55,0,1),p("decay",.5,0,1)}},
    {"digital_glitch","Digital Block Corruption",false,{p("frequency",.55,0,1),p("blockHeight",.28,0,1),p("shift",.8,0,1),p("color",.65,0,1)}},
    {"ordered_dither","Ordered Dither",false,{p("levels",.5,0,1),p("pattern",.6,0,1)}},
    {"cmyk_halftone","CMYK Print Screen",false,{p("dotSize",.45,0,1),p("angle",.45,0,1)}},
    {"clone_grid","Clone Grid",false,{p("columns",.35,0,1),p("rows",.35,0,1)}},
    {"edge_glow_native","Luminous Edge Glow",false,{p("strength",.7,0,1)}},
    {"soft_glow_native","Optical Soft Glow",false,{p("blur",.6,0,1),p("brightness",.75,0,1),p("sharpness",.45,0,1)}},
    {"ink_cartoon","Ink Cartoon",false,{p("triplevel",.55,0,1),p("edgeSpace",.4,0,1)}},
    {"film_projector","Film Projector",false,{p("grain",.35,0,1),p("dust",.25,0,1),p("flicker",.3,0,1)}},
    {"frame_delay","Delayed Frame Memory",false,{p("delay",.55,0,1)}},
    {"eq","Parametric EQ",true,{p("lowDb",0,-24,24,"dB"),p("midDb",0,-24,24,"dB"),p("highDb",0,-24,24,"dB")}},
    {"highpass","High-pass",true,{p("frequency",80,20,20000,"Hz")}},
    {"lowpass","Low-pass",true,{p("frequency",16000,20,20000,"Hz")}},
    {"compressor","Compressor",true,{p("threshold",-18,-60,0,"dB"),p("ratio",4,1,20),p("attack",10,0.1,200,"ms"),p("release",100,5,2000,"ms")}},
    {"limiter","Limiter",true,{p("ceiling",-1,-20,0,"dB"),p("release",50,1,1000,"ms")}},
    {"normalize","Normalize",true,{p("target",-14,-30,-1,"LUFS")}},
    {"reverb","Reverb",true,{p("room",0.5,0,1),p("damping",0.5,0,1),p("mix",0.25,0,1)}},
    {"delay","Delay / Echo",true,{p("time",250,1,2000,"ms"),p("feedback",0.35,0,0.95),p("mix",0.3,0,1)}},
    {"distortion","Distortion",true,{p("drive",6,0,30,"dB"),p("mix",1,0,1)}},
    {"bitcrush","Bit Crush",true,{p("bits",8,2,16),p("rate",12000,1000,48000,"Hz")}},
    {"noisegate","Noise Gate",true,{p("threshold",-45,-80,0,"dB"),p("release",100,1,2000,"ms")}},
    {"telephone","Telephone / Radio",true,{p("low",320,100,1200,"Hz"),p("high",3200,1200,8000,"Hz"),p("drive",3,0,12,"dB")}},
    {"tremolo","Tremolo Chopper",true,{p("rate",10,.1,30,"Hz"),p("depth",.8,0,1)}},
    {"vibrato","Vibrato",true,{p("rate",6,.1,20,"Hz"),p("depth",.5,0,1)}},
    {"flanger","Jet Flanger",true,{p("delay",5,0,30,"ms"),p("depth",3,0,10,"ms"),p("speed",.5,.1,10,"Hz")}},
    {"bass_boost","Bass Boost",true,{p("gain",10,-20,20,"dB"),p("frequency",120,20,500,"Hz")}},
    {"treble_boost","Treble Shred",true,{p("gain",10,-20,20,"dB"),p("frequency",6000,1000,16000,"Hz")}}
    ,{"chorus","Crowd Chorus",true,{p("depth",.55,0,1),p("speed",1.2,.1,5,"Hz"),p("mix",.7,0,1)}}
    ,{"phaser","Phase Tunnel",true,{p("speed",.8,.1,2,"Hz"),p("decay",.55,0,.95),p("depth",.8,0,1)}}
    ,{"stereo_widen","Stereo Detonator",true,{p("delay",18,1,100,"ms"),p("feedback",.35,0,.9),p("width",.7,0,1)}}
    ,{"crystalizer","Crystal Shatter",true,{p("intensity",4.5,-10,10),p("clipping",1,0,1)}}
    ,{"ring_mod","Ring Modulator",true,{p("frequency",70,5,2000,"Hz"),p("mix",1,0,1)}}
    ,{"frequency_shift","Frequency Shifter",true,{p("shift",180,-2000,2000,"Hz"),p("level",.85,0,1),p("order",12,1,16)}}
    ,{"robotize","FFT Robotizer",true,{p("window",512,32,4096),p("overlap",.75,0,.95)}}
    ,{"whisperize","Spectral Whisper",true,{p("window",128,32,2048),p("overlap",.8,0,.95)}}
    ,{"virtual_bass","Virtual Subharmonic Bass",true,{p("cutoff",220,100,500,"Hz"),p("strength",2.2,.5,3)}}
    ,{"haas_spread","Haas Space",true,{p("leftDelay",3,0,40,"ms"),p("rightDelay",14,0,40,"ms"),p("sideGain",1.35,.1,4)}}
};
bool finite(double v) { return std::isfinite(v); }
bool validInterpolation(KeyframeInterpolation value){const auto raw=static_cast<int>(value);return raw>=0&&raw<=2;}
}

const std::vector<EffectDescriptor>& effectCatalog() { return catalog; }
const EffectDescriptor* findEffectDescriptor(std::string_view type) {
    const auto it=std::find_if(catalog.begin(),catalog.end(),[type](const auto& d){return d.type==type;});
    return it==catalog.end()?nullptr:&*it;
}
EffectInstance createEffect(std::string_view type) {
    const auto* descriptor=findEffectDescriptor(type); if(!descriptor) throw std::invalid_argument("unknown effect type");
    return {.id=createId(),.type=descriptor->type,.enabled=true,.parameters=descriptor->parameters};
}
EffectParameter* findParameter(EffectInstance& effect,std::string_view name) { auto it=std::find_if(effect.parameters.begin(),effect.parameters.end(),[name](auto& p){return p.name==name;});return it==effect.parameters.end()?nullptr:&*it; }
const EffectParameter* findParameter(const EffectInstance& effect,std::string_view name) { auto it=std::find_if(effect.parameters.begin(),effect.parameters.end(),[name](const auto& p){return p.name==name;});return it==effect.parameters.end()?nullptr:&*it; }
double evaluateParameter(const EffectParameter& parameter,Rational time) {
    if(parameter.keyframes.empty()) return parameter.value;
    auto frames=parameter.keyframes; std::stable_sort(frames.begin(),frames.end(),[](const auto&a,const auto&b){return a.time<b.time;});
    if(time<=frames.front().time)return frames.front().value;
    if(time>=frames.back().time)return frames.back().value;
    for(std::size_t i=1;i<frames.size();++i) if(time<=frames[i].time){const auto&a=frames[i-1];const auto&b=frames[i];if(a.interpolation==KeyframeInterpolation::Hold)return a.value;double t=static_cast<double>(((time-a.time)/(b.time-a.time)).asLongDouble());if(a.interpolation==KeyframeInterpolation::Smooth)t=t*t*(3-2*t);return a.value+(b.value-a.value)*t;}
    return parameter.value;
}
std::optional<std::string> validateEffect(const EffectInstance& effect) {
    if(!isValidId(effect.id)||!findEffectDescriptor(effect.type))return "Effect identity or type is invalid.";
    for(const auto& parameter:effect.parameters){if(parameter.name.empty()||!finite(parameter.value)||!finite(parameter.minimum)||!finite(parameter.maximum)||parameter.minimum>parameter.maximum||parameter.value<parameter.minimum||parameter.value>parameter.maximum)return "Effect parameter is invalid.";Rational previous;bool first=true;for(const auto& key:parameter.keyframes){if(!isValidId(key.id)||key.time<Rational{}||!finite(key.value)||!validInterpolation(key.interpolation)||key.value<parameter.minimum||key.value>parameter.maximum||(!first&&key.time<=previous))return "Effect keyframe is invalid.";previous=key.time;first=false;}}
    return std::nullopt;
}
std::optional<std::string> validateTransform(const TransformSettings& t) { if(!finite(t.positionX)||!finite(t.positionY)||!finite(t.scaleX)||!finite(t.scaleY)||!finite(t.rotation)||!finite(t.anchorX)||!finite(t.anchorY)||!finite(t.opacity)||!finite(t.cropLeft)||!finite(t.cropTop)||!finite(t.cropRight)||!finite(t.cropBottom)||t.scaleX<=0||t.scaleY<=0||t.anchorX<0||t.anchorX>1||t.anchorY<0||t.anchorY>1||t.opacity<0||t.opacity>1||t.cropLeft<0||t.cropTop<0||t.cropRight<0||t.cropBottom<0||t.cropLeft+t.cropRight>=1||t.cropTop+t.cropBottom>=1)return "Transform is invalid.";for(const auto&p:t.animation){Rational prior;bool first=true;const bool supported=p.name=="positionX"||p.name=="positionY"||p.name=="scaleX"||p.name=="scaleY"||p.name=="rotation"||p.name=="opacity";if(!supported||!finite(p.value)||!finite(p.minimum)||!finite(p.maximum)||p.minimum>p.maximum||p.value<p.minimum||p.value>p.maximum)return "Transform animation is invalid.";for(const auto&k:p.keyframes){if(!isValidId(k.id)||k.time<Rational{}||!finite(k.value)||!validInterpolation(k.interpolation)||k.value<p.minimum||k.value>p.maximum||(!first&&k.time<=prior))return "Transform animation is invalid.";prior=k.time;first=false;}}return std::nullopt; }
std::optional<std::string> validateAudio(const AudioSettings& a) { if(!finite(a.gainDb)||!finite(a.pan)||a.gainDb < -96||a.gainDb>24||a.pan < -1||a.pan>1)return "Audio settings are invalid.";const auto valid=[](const std::vector<Keyframe>&keys,double low,double high){Rational prior;bool first=true;for(const auto&k:keys){if(!isValidId(k.id)||k.time<Rational{}||!finite(k.value)||!validInterpolation(k.interpolation)||k.value<low||k.value>high||(!first&&k.time<=prior))return false;prior=k.time;first=false;}return true;};if(!valid(a.gainEnvelope,-96,24)||!valid(a.panEnvelope,-1,1))return "Audio envelope is invalid.";return std::nullopt; }

} // namespace ytp
