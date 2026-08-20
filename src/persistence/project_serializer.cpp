#include "persistence/project_serializer.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>
#include <exception>

namespace ytp {
namespace {

QJsonObject rationalToJson(const Rational& value) {
    return {{"numerator", QString::number(value.numerator())},
            {"denominator", QString::number(value.denominator())}};
}

Rational rationalFromJson(const QJsonValue& value) {
    const auto object = value.toObject();
    bool numeratorOk = false;
    bool denominatorOk = false;
    const auto numerator = object.value("numerator").toString().toLongLong(&numeratorOk);
    const auto denominator = object.value("denominator").toString().toLongLong(&denominatorOk);
    if (!numeratorOk || !denominatorOk) {
        throw std::invalid_argument("invalid rational value");
    }
    return {numerator, denominator};
}

QJsonObject settingsToJson(const ProjectSettings& settings) {
    return {{"width", settings.width},
            {"height", settings.height},
            {"frameRateNumerator", QString::number(settings.frameRateNumerator)},
            {"frameRateDenominator", QString::number(settings.frameRateDenominator)},
            {"audioSampleRate", settings.audioSampleRate}};
}

ProjectSettings settingsFromJson(const QJsonObject& object) {
    bool rateNumeratorOk = false;
    bool rateDenominatorOk = false;
    ProjectSettings settings;
    settings.width = object.value("width").toInt();
    settings.height = object.value("height").toInt();
    settings.frameRateNumerator = object.value("frameRateNumerator").toString().toLongLong(&rateNumeratorOk);
    settings.frameRateDenominator = object.value("frameRateDenominator").toString().toLongLong(&rateDenominatorOk);
    settings.audioSampleRate = object.value("audioSampleRate").toInt();
    if (!rateNumeratorOk || !rateDenominatorOk) {
        throw std::invalid_argument("invalid project frame rate");
    }
    return settings;
}

QJsonObject keyframeToJson(const Keyframe& key) { return {{"id",QString::fromStdString(key.id)},{"time",rationalToJson(key.time)},{"value",key.value},{"interpolation",static_cast<int>(key.interpolation)}}; }
QJsonArray effectsToJson(const std::vector<EffectInstance>& effects) {
    QJsonArray result; for(const auto&e:effects){QJsonArray parameters;for(const auto&p:e.parameters){QJsonArray keys;for(const auto&k:p.keyframes)keys.append(keyframeToJson(k));parameters.append(QJsonObject{{"name",QString::fromStdString(p.name)},{"value",p.value},{"minimum",p.minimum},{"maximum",p.maximum},{"unit",QString::fromStdString(p.unit)},{"keyframes",keys}});}result.append(QJsonObject{{"id",QString::fromStdString(e.id)},{"type",QString::fromStdString(e.type)},{"enabled",e.enabled},{"parameters",parameters}});}return result;
}
std::vector<EffectInstance> effectsFromJson(const QJsonArray& values) {
    std::vector<EffectInstance> result;for(const auto&value:values){const auto o=value.toObject();EffectInstance e{.id=o.value("id").toString().toStdString(),.type=o.value("type").toString().toStdString(),.enabled=o.value("enabled").toBool(true)};for(const auto&pv:o.value("parameters").toArray()){const auto po=pv.toObject();EffectParameter p{.name=po.value("name").toString().toStdString(),.value=po.value("value").toDouble(),.minimum=po.value("minimum").toDouble(),.maximum=po.value("maximum").toDouble(),.unit=po.value("unit").toString().toStdString()};for(const auto&kv:po.value("keyframes").toArray()){const auto ko=kv.toObject();p.keyframes.push_back({.id=ko.value("id").toString().toStdString(),.time=rationalFromJson(ko.value("time")),.value=ko.value("value").toDouble(),.interpolation=static_cast<KeyframeInterpolation>(ko.value("interpolation").toInt(1))});}e.parameters.push_back(std::move(p));}result.push_back(std::move(e));}return result;
}
QJsonObject transformToJson(const TransformSettings&t){QJsonArray animation;for(const auto&p:t.animation){QJsonArray keys;for(const auto&k:p.keyframes)keys.append(keyframeToJson(k));animation.append(QJsonObject{{"name",QString::fromStdString(p.name)},{"value",p.value},{"minimum",p.minimum},{"maximum",p.maximum},{"keyframes",keys}});}return{{"positionX",t.positionX},{"positionY",t.positionY},{"scaleX",t.scaleX},{"scaleY",t.scaleY},{"rotation",t.rotation},{"anchorX",t.anchorX},{"anchorY",t.anchorY},{"opacity",t.opacity},{"cropLeft",t.cropLeft},{"cropTop",t.cropTop},{"cropRight",t.cropRight},{"cropBottom",t.cropBottom},{"fit",t.fit},{"flipHorizontal",t.flipHorizontal},{"flipVertical",t.flipVertical},{"animation",animation}};}
TransformSettings transformFromJson(const QJsonObject&o){TransformSettings t;t.positionX=o.value("positionX").toDouble();t.positionY=o.value("positionY").toDouble();t.scaleX=o.value("scaleX").toDouble(1);t.scaleY=o.value("scaleY").toDouble(1);t.rotation=o.value("rotation").toDouble();t.anchorX=o.value("anchorX").toDouble(.5);t.anchorY=o.value("anchorY").toDouble(.5);t.opacity=o.value("opacity").toDouble(1);t.cropLeft=o.value("cropLeft").toDouble();t.cropTop=o.value("cropTop").toDouble();t.cropRight=o.value("cropRight").toDouble();t.cropBottom=o.value("cropBottom").toDouble();t.fit=o.value("fit").toBool(true);t.flipHorizontal=o.value("flipHorizontal").toBool();t.flipVertical=o.value("flipVertical").toBool();for(const auto&v:o.value("animation").toArray()){const auto po=v.toObject();EffectParameter p{.name=po.value("name").toString().toStdString(),.value=po.value("value").toDouble(),.minimum=po.value("minimum").toDouble(),.maximum=po.value("maximum").toDouble()};for(const auto&kv:po.value("keyframes").toArray()){const auto ko=kv.toObject();p.keyframes.push_back({.id=ko.value("id").toString().toStdString(),.time=rationalFromJson(ko.value("time")),.value=ko.value("value").toDouble(),.interpolation=static_cast<KeyframeInterpolation>(ko.value("interpolation").toInt(1))});}t.animation.push_back(std::move(p));}return t;}
QJsonObject audioToJson(const AudioSettings&a){QJsonArray gain,pan;for(const auto&k:a.gainEnvelope)gain.append(keyframeToJson(k));for(const auto&k:a.panEnvelope)pan.append(keyframeToJson(k));return{{"gainDb",a.gainDb},{"pan",a.pan},{"bypass",a.bypass},{"gainEnvelope",gain},{"panEnvelope",pan}};}
AudioSettings audioFromJson(const QJsonObject&o){AudioSettings a;a.gainDb=o.value("gainDb").toDouble();a.pan=o.value("pan").toDouble();a.bypass=o.value("bypass").toBool();const auto load=[](const QJsonArray&v){std::vector<Keyframe>r;for(const auto&x:v){const auto k=x.toObject();r.push_back({.id=k.value("id").toString().toStdString(),.time=rationalFromJson(k.value("time")),.value=k.value("value").toDouble(),.interpolation=static_cast<KeyframeInterpolation>(k.value("interpolation").toInt(1))});}return r;};a.gainEnvelope=load(o.value("gainEnvelope").toArray());a.panEnvelope=load(o.value("panEnvelope").toArray());return a;}

QJsonArray masksToJson(const std::vector<MaskSettings>& masks){QJsonArray result;for(const auto&m:masks){QJsonArray animation;for(const auto&p:m.animation){QJsonArray keys;for(const auto&k:p.keyframes)keys.append(keyframeToJson(k));animation.append(QJsonObject{{"name",QString::fromStdString(p.name)},{"value",p.value},{"minimum",p.minimum},{"maximum",p.maximum},{"keyframes",keys}});}result.append(QJsonObject{{"id",QString::fromStdString(m.id)},{"shape",static_cast<int>(m.shape)},{"x",m.x},{"y",m.y},{"width",m.width},{"height",m.height},{"feather",m.feather},{"opacity",m.opacity},{"inverted",m.inverted},{"animation",animation}});}return result;}
std::vector<MaskSettings> masksFromJson(const QJsonArray& values){std::vector<MaskSettings> result;for(const auto&v:values){const auto o=v.toObject();MaskSettings m{.id=o.value("id").toString().toStdString(),.shape=static_cast<MaskShape>(o.value("shape").toInt()),.x=o.value("x").toDouble(.1),.y=o.value("y").toDouble(.1),.width=o.value("width").toDouble(.8),.height=o.value("height").toDouble(.8),.feather=o.value("feather").toDouble(),.opacity=o.value("opacity").toDouble(1),.inverted=o.value("inverted").toBool()};for(const auto&pv:o.value("animation").toArray()){const auto po=pv.toObject();EffectParameter p{.name=po.value("name").toString().toStdString(),.value=po.value("value").toDouble(),.minimum=po.value("minimum").toDouble(),.maximum=po.value("maximum").toDouble()};for(const auto&kv:po.value("keyframes").toArray()){const auto ko=kv.toObject();p.keyframes.push_back({.id=ko.value("id").toString().toStdString(),.time=rationalFromJson(ko.value("time")),.value=ko.value("value").toDouble(),.interpolation=static_cast<KeyframeInterpolation>(ko.value("interpolation").toInt(1))});}m.animation.push_back(std::move(p));}result.push_back(std::move(m));}return result;}

QJsonObject itemToJson(const TimelineItem& item) {
    return {{"id", QString::fromStdString(item.id)},
            {"name", QString::fromStdString(item.name)},
            {"libraryClipId", QString::fromStdString(item.libraryClipId)},
            {"mediaAssetId", QString::fromStdString(item.mediaAssetId)},
            {"nestedSequenceId", QString::fromStdString(item.nestedSequenceId)},
            {"adjustmentClip", item.adjustmentClip},
            {"trackId", QString::fromStdString(item.trackId)},
            {"timelineStart", rationalToJson(item.timelineStart)},
            {"sourceStart", rationalToJson(item.sourceRange.start())},
            {"sourceDuration",rationalToJson(item.sourceRange.duration())},
            {"duration", rationalToJson(item.duration)},
            {"linkedGroupId", QString::fromStdString(item.linkedGroupId)},
            {"groupId", QString::fromStdString(item.groupId)},
            {"fadeIn", rationalToJson(item.fadeIn)},
            {"fadeOut", rationalToJson(item.fadeOut)},
            {"transform",transformToJson(item.transform)},{"audio",audioToJson(item.audio)},
            {"speed",item.speed},{"pitchSemitones",item.pitchSemitones},{"preservePitch",item.preservePitch},
            {"reverse",item.reverse},{"freezeFrame",item.freezeFrame},{"freezeSourceTime",rationalToJson(item.freezeSourceTime)},
            {"effects",effectsToJson(item.effects)},{"masks",masksToJson(item.masks)},
            {"captionEnabled",item.captionEnabled},{"captionText",QString::fromStdString(item.captionText)},
            {"captionSize",item.captionSize},{"captionColor",QString::fromStdString(item.captionColor)}};
}

TimelineItem itemFromJson(const QJsonObject& object) {
    const auto duration = rationalFromJson(object.value("duration"));
    const auto speed = object.value("speed").toDouble(1.0);
    const auto speedRatio = Rational{
        static_cast<std::int64_t>(std::llround(speed * 1'000'000.0)), 1'000'000};
    return TimelineItem{
        .id = object.value("id").toString().toStdString(),
        .libraryClipId = object.value("libraryClipId").toString().toStdString(),
        .mediaAssetId = object.value("mediaAssetId").toString().toStdString(),
        .nestedSequenceId = object.value("nestedSequenceId").toString().toStdString(),
        .adjustmentClip = object.value("adjustmentClip").toBool(),
        .trackId = object.value("trackId").toString().toStdString(),
        .timelineStart = rationalFromJson(object.value("timelineStart")),
        .sourceRange = TimeRange{rationalFromJson(object.value("sourceStart")), object.contains("sourceDuration")?rationalFromJson(object.value("sourceDuration")):duration*speedRatio},
        .duration = duration,
        .linkedGroupId = object.value("linkedGroupId").toString().toStdString(),
        .groupId = object.value("groupId").toString().toStdString(),
        .fadeIn = rationalFromJson(object.value("fadeIn")),
        .fadeOut = rationalFromJson(object.value("fadeOut")),
        .transform=transformFromJson(object.value("transform").toObject()),.audio=audioFromJson(object.value("audio").toObject()),
        .speed=speed,.pitchSemitones=object.value("pitchSemitones").toDouble(),.preservePitch=object.value("preservePitch").toBool(true),
        .reverse=object.value("reverse").toBool(),.freezeFrame=object.value("freezeFrame").toBool(),
        .freezeSourceTime=object.contains("freezeSourceTime")?rationalFromJson(object.value("freezeSourceTime")):Rational{},
        .effects=effectsFromJson(object.value("effects").toArray()),
        .masks=masksFromJson(object.value("masks").toArray()),
        .captionEnabled=object.value("captionEnabled").toBool(),
        .captionText=object.value("captionText").toString().toStdString(),
        .captionSize=object.value("captionSize").toDouble(54.0),
        .captionColor=object.value("captionColor").toString("white").toStdString(),
        .name=object.value("name").toString().toStdString()};
}

QJsonObject sequenceToJson(const Sequence& sequence) {
    QJsonArray tracks;
    for (const auto& track : sequence.tracks) {
        QJsonArray items;
        for (const auto& item : track.items) items.append(itemToJson(item));
        tracks.append(QJsonObject{{"id", QString::fromStdString(track.id)},
                                  {"name", QString::fromStdString(track.name)},
                                  {"kind", track.kind == TrackKind::Video ? "video" : "audio"},
                                  {"order", track.order}, {"locked", track.locked},
                                  {"muted", track.muted}, {"solo", track.solo},
                                  {"visible", track.visible}, {"height", track.height},
                                  {"color", QString::fromStdString(track.color)},
                                  {"audio",audioToJson(track.audio)},{"effects",effectsToJson(track.effects)},
                                  {"items", items}});
    }
    QJsonArray markers;
    for (const auto& marker : sequence.markers) {
        markers.append(QJsonObject{{"id", QString::fromStdString(marker.id)},
                                   {"time", rationalToJson(marker.time)},
                                   {"label", QString::fromStdString(marker.label)},
                                   {"color", QString::fromStdString(marker.color)}});
    }
    return {{"id", QString::fromStdString(sequence.id)},
            {"name", QString::fromStdString(sequence.name)},
            {"rippleMode", static_cast<int>(sequence.rippleMode)},
            {"rippleMarkers", sequence.rippleMarkers},
            {"automaticAudioFades", sequence.automaticAudioFades},
            {"previewQuality",static_cast<int>(sequence.previewQuality)},
            {"masterAudio",audioToJson(sequence.masterAudio)},{"masterEffects",effectsToJson(sequence.masterEffects)},
            {"masterLimiter",sequence.masterLimiter},
            {"beatGrid",QJsonObject{{"enabled",sequence.beatGrid.enabled},{"bpm",sequence.beatGrid.bpm},{"offset",rationalToJson(sequence.beatGrid.offset)},{"division",sequence.beatGrid.division}}},
            {"tracks", tracks}, {"markers", markers}};
}

Sequence sequenceFromJson(const QJsonObject& object) {
    Sequence sequence;
    sequence.id = object.value("id").toString().toStdString();
    sequence.name = object.value("name").toString().toStdString();
    const auto ripple = object.value("rippleMode").toInt(2);
    if (ripple < 0 || ripple > 2) throw std::invalid_argument("invalid ripple mode");
    sequence.rippleMode = static_cast<RippleMode>(ripple);
    sequence.rippleMarkers = object.value("rippleMarkers").toBool(true);
    sequence.automaticAudioFades = object.value("automaticAudioFades").toBool(true);
    sequence.previewQuality=static_cast<PreviewQuality>(object.value("previewQuality").toInt());
    sequence.masterAudio=audioFromJson(object.value("masterAudio").toObject());
    sequence.masterEffects=effectsFromJson(object.value("masterEffects").toArray());
    sequence.masterLimiter=object.value("masterLimiter").toBool(true);
    const auto beat=object.value("beatGrid").toObject();sequence.beatGrid.enabled=beat.value("enabled").toBool();sequence.beatGrid.bpm=beat.value("bpm").toDouble(120);sequence.beatGrid.offset=beat.contains("offset")?rationalFromJson(beat.value("offset")):Rational{};sequence.beatGrid.division=beat.value("division").toInt(4);
    for (const auto& trackValue : object.value("tracks").toArray()) {
        const auto trackObject = trackValue.toObject();
        Track track;
        track.id = trackObject.value("id").toString().toStdString();
        track.name = trackObject.value("name").toString().toStdString();
        const auto kind = trackObject.value("kind").toString();
        if (kind != "video" && kind != "audio") throw std::invalid_argument("invalid track kind");
        track.kind = kind == "audio" ? TrackKind::Audio : TrackKind::Video;
        track.order = trackObject.contains("order") ? trackObject.value("order").toInt()
                                                    : static_cast<int>(sequence.tracks.size());
        track.locked = trackObject.value("locked").toBool();
        track.muted = trackObject.value("muted").toBool();
        track.solo = trackObject.value("solo").toBool();
        track.visible = trackObject.value("visible").toBool(true);
        track.height = trackObject.value("height").toInt(72);
        track.color = trackObject.value("color").toString("#3b6f96").toStdString();
        track.audio=audioFromJson(trackObject.value("audio").toObject());
        track.effects=effectsFromJson(trackObject.value("effects").toArray());
        for (const auto& itemValue : trackObject.value("items").toArray())
            track.items.push_back(itemFromJson(itemValue.toObject()));
        sequence.tracks.push_back(std::move(track));
    }
    for (const auto& markerValue : object.value("markers").toArray()) {
        const auto markerObject = markerValue.toObject();
        sequence.markers.push_back(TimelineMarker{
            .id = markerObject.value("id").toString().toStdString(),
            .time = rationalFromJson(markerObject.value("time")),
            .label = markerObject.value("label").toString().toStdString(),
            .color = markerObject.value("color").toString("#ffd166").toStdString()});
    }
    return sequence;
}

void setError(QString* destination, const QString& message) {
    if (destination != nullptr) {
        *destination = message;
    }
}

} // namespace

QByteArray ProjectSerializer::serialize(const Project& project,
                                        const bool pretty,
                                        QString* errorMessage) {
    if (const auto error = project.validate()) {
        setError(errorMessage, QString::fromStdString(*error));
        return {};
    }

    QJsonArray media;
    for (const auto& asset : project.mediaAssets()) {
        QJsonArray transcript;
        for (const auto& word : asset.transcript) transcript.append(QJsonObject{
            {"id",QString::fromStdString(word.id)},{"start",rationalToJson(word.start)},
            {"duration",rationalToJson(word.duration)},{"text",QString::fromStdString(word.text)},
            {"confidence",word.confidence}});
        media.append(QJsonObject{
            {"id", QString::fromStdString(asset.id)},
            {"path", QString::fromStdString(asset.path)},
            {"displayName", QString::fromStdString(asset.displayName)},
            {"duration", rationalToJson(asset.duration)},
            {"frameRateNumerator", QString::number(asset.frameRateNumerator)},
            {"frameRateDenominator", QString::number(asset.frameRateDenominator)},
            {"width", asset.width},
            {"height", asset.height},
            {"audioSampleRate", asset.audioSampleRate},
            {"fingerprint", QString::fromStdString(asset.fingerprint)},
            {"proxyPath",QString::fromStdString(asset.proxyPath)},{"proxyReady",asset.proxyReady},
            {"bin", QString::fromStdString(asset.bin)},
            {"createdAtMs", QString::number(asset.createdAtMs)},
            {"lastUsedAtMs", QString::number(asset.lastUsedAtMs)},
            {"transcriptionLanguage",QString::fromStdString(asset.transcriptionLanguage)},
            {"transcriptionModel",QString::fromStdString(asset.transcriptionModel)},
            {"transcript",transcript}
        });
    }

    QJsonArray clips;
    for (const auto& clip : project.libraryClips()) {
        QJsonArray tags;
        for (const auto& tag : clip.tags) {
            tags.append(QString::fromStdString(tag));
        }
        clips.append(QJsonObject{
            {"id", QString::fromStdString(clip.id)},
            {"mediaAssetId", QString::fromStdString(clip.mediaAssetId)},
            {"sourceStart", rationalToJson(clip.sourceRange.start())},
            {"sourceDuration", rationalToJson(clip.sourceRange.duration())},
            {"name", QString::fromStdString(clip.name)},
            {"tags", tags},
            {"notes", QString::fromStdString(clip.notes)},
            {"color", QString::fromStdString(clip.color)},
            {"bin", QString::fromStdString(clip.bin)},
            {"favorite", clip.favorite},
            {"thumbnailTime", rationalToJson(clip.thumbnailTime)},
            {"createdAtMs", QString::number(clip.createdAtMs)},
            {"lastUsedAtMs", QString::number(clip.lastUsedAtMs)}
        });
    }

    QJsonArray sequences;
    for (const auto& sequence : project.sequences()) sequences.append(sequenceToJson(sequence));
    QJsonArray compounds;for(const auto&compound:project.compoundClips())compounds.append(QJsonObject{{"id",QString::fromStdString(compound.id)},{"sequenceId",QString::fromStdString(compound.sequenceId)},{"name",QString::fromStdString(compound.name)},{"color",QString::fromStdString(compound.color)},{"updateAllInstances",compound.updateAllInstances},{"createdAtMs",QString::number(compound.createdAtMs)}});
    const QJsonObject root{
        {"format", "ytp-editor-project"},
        {"formatVersion", project.formatVersion()},
        {"id", QString::fromStdString(project.id())},
        {"name", QString::fromStdString(project.name())},
        {"settings", settingsToJson(project.settings())},
        {"mediaAssets", media},
        {"libraryClips", clips},
        {"sequences", sequences},{"compoundClips",compounds}
    };

    return QJsonDocument(root).toJson(pretty ? QJsonDocument::Indented : QJsonDocument::Compact);
}

bool ProjectSerializer::save(const Project& project,
                             const QString& filePath,
                             QString* errorMessage) {
    const auto data = serialize(project, true, errorMessage);
    if (data.isEmpty()) return false;
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(errorMessage, file.errorString());
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        setError(errorMessage, file.errorString());
        return false;
    }
    return true;
}

std::optional<Project> ProjectSerializer::load(const QString& filePath,
                                               QString* errorMessage) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, file.errorString());
        return std::nullopt;
    }
    return deserialize(file.readAll(), QFileInfo(filePath).absolutePath(), errorMessage);
}

std::optional<Project> ProjectSerializer::deserialize(const QByteArray& data,
                                                      const QString& baseDirectory,
                                                      QString* errorMessage) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, parseError.errorString());
        return std::nullopt;
    }

    try {
        const auto root = document.object();
        if (root.value("format").toString() != "ytp-editor-project") {
            throw std::invalid_argument("not a YTP Editor project");
        }
        Project project;
        project.setIdentity(root.value("id").toString().toStdString(),
                            root.value("formatVersion").toInt());
        project.setName(root.value("name").toString().toStdString());
        project.setSettings(settingsFromJson(root.value("settings").toObject()));
        const QDir projectDirectory{baseDirectory};

        for (const auto& value : root.value("mediaAssets").toArray()) {
            const auto object = value.toObject();
            bool rateNumeratorOk = false;
            bool rateDenominatorOk = false;
            std::vector<TranscriptWord> transcript;
            for(const auto& wordValue:object.value("transcript").toArray()) { const auto word=wordValue.toObject(); transcript.push_back({
                .id=word.value("id").toString().toStdString(),.start=rationalFromJson(word.value("start")),
                .duration=rationalFromJson(word.value("duration")),.text=word.value("text").toString().toStdString(),
                .confidence=word.value("confidence").toDouble(1)}); }
            auto mediaPath=object.value("path").toString();
            if(QFileInfo(mediaPath).isRelative()){const auto candidate=projectDirectory.absoluteFilePath(mediaPath);if(QFileInfo::exists(candidate))mediaPath=QFileInfo(candidate).absoluteFilePath();}
            auto proxyPath=object.value("proxyPath").toString();
            if(!proxyPath.isEmpty()&&QFileInfo(proxyPath).isRelative()){const auto candidate=projectDirectory.absoluteFilePath(proxyPath);if(QFileInfo::exists(candidate))proxyPath=QFileInfo(candidate).absoluteFilePath();}
            MediaAsset asset{
                .id = object.value("id").toString().toStdString(),
                .path = mediaPath.toStdString(),
                .displayName = object.value("displayName").toString().toStdString(),
                .duration = rationalFromJson(object.value("duration")),
                .frameRateNumerator = object.value("frameRateNumerator").toString().toLongLong(&rateNumeratorOk),
                .frameRateDenominator = object.value("frameRateDenominator").toString().toLongLong(&rateDenominatorOk),
                .width = object.value("width").toInt(),
                .height = object.value("height").toInt(),
                .audioSampleRate = object.value("audioSampleRate").toInt(),
                .fingerprint = object.value("fingerprint").toString().toStdString(),
                .proxyPath=proxyPath.toStdString(),.proxyReady=object.value("proxyReady").toBool()&&!proxyPath.isEmpty(),
                .bin = object.value("bin").toString(QStringLiteral("Media")).toStdString(),
                .createdAtMs = object.value("createdAtMs").toString().toLongLong(),
                .lastUsedAtMs = object.value("lastUsedAtMs").toString().toLongLong(),
                .transcriptionLanguage=object.value("transcriptionLanguage").toString().toStdString(),
                .transcriptionModel=object.value("transcriptionModel").toString().toStdString(),
                .transcript=std::move(transcript)
            };
            if (!rateNumeratorOk || !rateDenominatorOk) {
                throw std::invalid_argument("invalid media frame rate");
            }
            project.addMediaAsset(std::move(asset));
        }

        for (const auto& value : root.value("libraryClips").toArray()) {
            const auto object = value.toObject();
            std::vector<std::string> tags;
            for (const auto& tag : object.value("tags").toArray()) {
                tags.push_back(tag.toString().toStdString());
            }
            LibraryClip clip{
                .id = object.value("id").toString().toStdString(),
                .mediaAssetId = object.value("mediaAssetId").toString().toStdString(),
                .sourceRange = TimeRange{rationalFromJson(object.value("sourceStart")),
                                         rationalFromJson(object.value("sourceDuration"))},
                .name = object.value("name").toString().toStdString(),
                .tags = std::move(tags),
                .notes = object.value("notes").toString().toStdString(),
                .color = object.value("color").toString().toStdString(),
                .bin = object.value("bin").toString(QStringLiteral("Clips")).toStdString(),
                .favorite = object.value("favorite").toBool(),
                .thumbnailTime = rationalFromJson(object.value("thumbnailTime")),
                .createdAtMs = object.value("createdAtMs").toString().toLongLong(),
                .lastUsedAtMs = object.value("lastUsedAtMs").toString().toLongLong()
            };
            project.addLibraryClip(std::move(clip));
        }

        if (const auto sequenceValues = root.value("sequences").toArray(); !sequenceValues.isEmpty()) {
            std::vector<Sequence> sequences;
            for (const auto& value : sequenceValues)
                sequences.push_back(sequenceFromJson(value.toObject()));
            project.setSequences(std::move(sequences));
        }
        for(const auto&value:root.value("compoundClips").toArray()){const auto object=value.toObject();project.addCompoundClip({.id=object.value("id").toString().toStdString(),.sequenceId=object.value("sequenceId").toString().toStdString(),.name=object.value("name").toString().toStdString(),.color=object.value("color").toString("#8a6fd1").toStdString(),.updateAllInstances=object.value("updateAllInstances").toBool(true),.createdAtMs=object.value("createdAtMs").toString().toLongLong()});}

        if (const auto validationError = project.validate()) {
            throw std::invalid_argument(*validationError);
        }
        project.setIdentity(project.id(), currentProjectFormatVersion);
        return project;
    } catch (const std::exception& exception) {
        setError(errorMessage, QString::fromUtf8(exception.what()));
        return std::nullopt;
    }
}

} // namespace ytp
