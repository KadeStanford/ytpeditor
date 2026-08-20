#include "ui/timeline_controller.h"

#include "media/media_cache.h"
#include "media/media_analysis.h"
#include "export/render_engine.h"
#include "ytp/remix_toolkit.h"
#include "timeline/timeline_editor.h"
#include "timeline/effects_editor.h"
#include "ui/project_controller.h"

#include <QFileInfo>
#include <QFile>
#include <QUrl>
#include <QSettings>
#include <QKeySequence>
#include <QSet>
#include <QHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFutureWatcher>
#include <QDateTime>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QtConcurrentRun>
#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <cmath>
#include <unordered_map>
#include <random>

namespace ytp {
namespace {
Rational seconds(qint64 ms) { return Rational{ms, 1000}; }
qint64 milliseconds(const Rational& time) {
    return static_cast<qint64>(time.asLongDouble() * 1000.0L);
}

class PreviewStreamDevice final : public QIODevice {
public:
    explicit PreviewStreamDevice(QObject* parent=nullptr):QIODevice(parent){open(QIODevice::ReadOnly);}

    void append(const QByteArray& data){
        if(data.isEmpty())return;
        {QMutexLocker lock(&mutex_);buffer_.append(data);}
        emit readyRead();
    }
    void finish(){
        {QMutexLocker lock(&mutex_);finished_=true;}
        emit readyRead();
    }
    bool isSequential() const override{return true;}
    bool atEnd() const override{QMutexLocker lock(&mutex_);return finished_&&readOffset_>=buffer_.size();}
    qint64 bytesAvailable() const override{QMutexLocker lock(&mutex_);return buffer_.size()-readOffset_+QIODevice::bytesAvailable();}

protected:
    qint64 readData(char* data,qint64 maximum) override{
        QMutexLocker lock(&mutex_);
        const auto remaining=buffer_.size()-readOffset_;
        if(remaining<=0)return finished_?-1:0;
        const auto count=std::min<qint64>(maximum,remaining);
        std::memcpy(data,buffer_.constData()+readOffset_,static_cast<std::size_t>(count));
        readOffset_+=static_cast<qsizetype>(count);
        if(readOffset_==buffer_.size()){buffer_.clear();readOffset_=0;}
        else if(readOffset_>1'048'576&&readOffset_>buffer_.size()/2){buffer_.remove(0,readOffset_);readOffset_=0;}
        return count;
    }
    qint64 writeData(const char*,qint64) override{return -1;}

private:
    mutable QMutex mutex_;
    QByteArray buffer_;
    qsizetype readOffset_{0};
    bool finished_{false};
};
}

TimelineController::TimelineController(ProjectController* controller, QObject* parent)
    : QObject(parent), projectController_(controller) {
    if(!controller->project().sequences().empty()) activeSequenceId_=QString::fromStdString(controller->project().sequences().front().id);
    connect(this,&TimelineController::selectionChanged,this,[this]{selectedIdSet_=QSet<QString>(selectedIds_.begin(),selectedIds_.end());inspectorCacheDirty_=true;});
    previewDebounce_.setSingleShot(true);previewDebounce_.setInterval(120);
    programCacheDebounce_.setSingleShot(true);programCacheDebounce_.setInterval(1500);
    connect(&programCacheDebounce_,&QTimer::timeout,this,[this]{if(continuousCacheEnabled_&&sequence()&&sequence()->duration()>Rational{}&&!previewRendering_&&!livePreviewActive())renderContinuousProgramCache();});
    connect(&previewDebounce_,&QTimer::timeout,this,[this]{
        if(livePreviewActive()){effectPreviewPending_=true;return;}
        if(effectPreviewRendering_){effectPreviewPending_=true;return;}
        effectPreviewPending_=false;
        const auto*seq=sequence();if(!seq)return;const auto time=seconds(playheadMs_);const TimelineItem* active=nullptr;
        for(const auto&track:seq->tracks){if(track.kind!=TrackKind::Video||!track.visible)continue;for(const auto&item:track.items)if(time>=item.timelineStart&&time<item.timelineEnd()){active=&item;break;}if(active)break;}
        if(!active)return;const auto*media=projectController_->project().findMediaAsset(active->mediaAssetId);if(!media)return;
        auto item=*active;const auto local=time-item.timelineStart;for(const auto&channel:item.transform.animation){const auto value=evaluateParameter(channel,local);if(channel.name=="positionX")item.transform.positionX=value;else if(channel.name=="positionY")item.transform.positionY=value;else if(channel.name=="scaleX")item.transform.scaleX=value;else if(channel.name=="scaleY")item.transform.scaleY=value;else if(channel.name=="rotation")item.transform.rotation=value;else if(channel.name=="opacity")item.transform.opacity=value;}for(auto&e:item.effects)for(auto&p:e.parameters)if(!p.keyframes.empty())p.value=evaluateParameter(p,local);const auto speedRatio=Rational{static_cast<std::int64_t>(std::llround(item.speed*1'000'000)),1'000'000};auto source=item.freezeFrame?item.freezeSourceTime:(item.reverse?item.sourceRange.end()-local*speedRatio:item.sourceRange.start()+local*speedRatio);
        auto path=QString::fromStdString(seq->previewQuality==PreviewQuality::Proxy&&media->proxyReady?media->proxyPath:media->path);auto quality=seq->previewQuality;const auto generation=previewGeneration_;const auto originalId=QString::fromStdString(item.id);const auto cacheId=originalId+QStringLiteral("-")+QString::number(generation);item.id=cacheId.toStdString();auto*watcher=new QFutureWatcher<bool>(this);effectPreviewRendering_=true;const auto cancellation=std::make_shared<std::atomic_bool>(false);effectPreviewCancellation_=cancellation;
        connect(watcher,&QFutureWatcher<bool>::finished,this,[this,watcher,generation,originalId,cacheId,cancellation]{const bool ok=watcher->result();watcher->deleteLater();effectPreviewRendering_=false;if(effectPreviewCancellation_==cancellation)effectPreviewCancellation_.reset();if(ok&&generation==previewGeneration_){const auto previous=renderedPreviewUrl_.toLocalFile();renderedPreviewItemId_=originalId;renderedPreviewUrl_=QUrl::fromLocalFile(MediaCache::effectPreviewPath(cacheId));renderedPreviewUrl_.setQuery(QString::number(generation));if(!previous.isEmpty()&&previous!=renderedPreviewUrl_.toLocalFile())QFile::remove(previous);emit programPreviewChanged();}else QFile::remove(MediaCache::effectPreviewPath(cacheId));if(effectPreviewPending_||generation!=previewGeneration_){effectPreviewPending_=false;previewDebounce_.start(0);}});
        watcher->setFuture(QtConcurrent::run([path,item,source,quality,cancellation]{return MediaCache::generateEffectPreview(path,item,source,quality,nullptr,cancellation.get());}));
    });
    connect(controller, &ProjectController::projectChanged, this, [this] {
        inspectorCacheDirty_=true;
        if(effectPreviewCancellation_)effectPreviewCancellation_->store(true);
        const auto&sequences=projectController_->project().sequences();
        if(!projectController_->project().findSequence(activeSequenceId_.toStdString()))
            activeSequenceId_=sequences.empty()?QString{}:QString::fromStdString(sequences.front().id);
        sanitizeSelection();
        programCacheStale_=true;invalidatePlaybackPreview();if(continuousCacheEnabled_)programCacheDebounce_.start();
        ++previewGeneration_;effectPreviewPending_=true;
        const auto bounded=std::clamp<qint64>(playheadMs_,0,std::max<qint64>(0,durationMs()));
        if(bounded!=playheadMs_){playheadMs_=bounded;emit playheadChanged();emit programPreviewChanged();}
        emit timelineChanged();
        previewDebounce_.start();
    });
    connect(controller,&ProjectController::mediaCacheChanged,this,[this]{emit timelineChanged();emit programPreviewChanged();});
    connect(controller,&ProjectController::sessionRestoreRequested,this,[this]{
        const auto state=projectController_->sessionState();
        const auto requestedSequence=state.value(QStringLiteral("activeSequenceId")).toString();
        if(!requestedSequence.isEmpty()&&projectController_->project().findSequence(requestedSequence.toStdString()))
            activeSequenceId_=requestedSequence;
        pixelsPerSecond_=std::clamp(state.value(QStringLiteral("pixelsPerSecond"),80.0).toDouble(),5.0,4000.0);
        playheadMs_=std::clamp<qint64>(state.value(QStringLiteral("playheadMs"),0).toLongLong(),0,std::max<qint64>(0,durationMs()));
        selectedIds_=state.value(QStringLiteral("selectedIds")).toStringList();
        sanitizeSelection();
        emit zoomChanged();
        emit playheadChanged();
        emit selectionChanged();
        emit timelineChanged();
        emit programPreviewChanged();
    });
}

TimelineController::~TimelineController(){disposeLivePreviewProcess();}

void TimelineController::disposeLivePreviewProcess(){
    if(livePreviewProcess_){
        auto*process=livePreviewProcess_;livePreviewProcess_=nullptr;process->disconnect(this);
        if(process->state()!=QProcess::NotRunning){
            process->terminate();
            if(!process->waitForFinished(150)){process->kill();process->waitForFinished(350);}
        }
        delete process;
    }
    if(livePreviewDevice_){auto*device=static_cast<PreviewStreamDevice*>(livePreviewDevice_);livePreviewDevice_=nullptr;device->finish();device->deleteLater();}
    livePreviewProgressBuffer_.clear();livePreviewDiagnostics_.clear();
    livePreviewEncodedUs_=0;livePreviewPublishThresholdUs_=0;livePreviewProducerFinished_=false;
}

void TimelineController::publishLivePreviewIfBuffered(quint64 generation,const QUrl&streamUrl){
    if(generation!=livePreviewGeneration_||!livePreviewUrl_.isEmpty()||!livePreviewDevice_)return;
    if(!livePreviewProducerFinished_&&livePreviewEncodedUs_<livePreviewPublishThresholdUs_)return;
    livePreviewUrl_=streamUrl;livePreviewStarting_=false;emit livePreviewChanged();
}

void TimelineController::resetPresentedFrame(){
    if(presentedFrameTimestampUs_==-1)return;
    presentedFrameTimestampUs_=-1;
    emit presentedFrameChanged();
}

void TimelineController::stopLivePreview(){
    ++livePreviewGeneration_;
    livePreviewUrl_=QUrl{};livePreviewStartMs_=0;livePreviewStarting_=false;resetPresentedFrame();
    disposeLivePreviewProcess();
    emit livePreviewChanged();
    ++previewGeneration_;effectPreviewPending_=true;previewDebounce_.start();
    startNextTimelineThumbnail();
    if(continuousCacheEnabled_)programCacheDebounce_.start();
}

bool TimelineController::startLivePreview(qint64 atMs){
    const auto*current=sequence();
    if(!current||current->duration()<=Rational{})return false;
    atMs=std::clamp<qint64>(atMs,0,std::max<qint64>(0,durationMs()-1));
    previewDebounce_.stop();programCacheDebounce_.stop();
    if(effectPreviewCancellation_)effectPreviewCancellation_->store(true);
    if(programCancellation_)programCancellation_->store(true);
    const auto generation=++livePreviewGeneration_;
    disposeLivePreviewProcess();
    livePreviewDevice_=new PreviewStreamDevice(this);resetPresentedFrame();
    livePreviewUrl_=QUrl{};livePreviewStartMs_=atMs;livePreviewStarting_=true;
    emit livePreviewChanged();
    int width=1280;
    if(current->previewQuality==PreviewQuality::Automatic||current->previewQuality==PreviewQuality::Proxy)width=640;
    else if(current->previewQuality==PreviewQuality::Half)width=960;
    else if(current->previewQuality==PreviewQuality::Quarter)width=480;
    const auto&settings=projectController_->project().settings();
    const auto projectWidth=std::max(1,settings.width),projectHeight=std::max(1,settings.height);
    int height=std::max(2,static_cast<int>(std::llround(static_cast<double>(width)*projectHeight/projectWidth)));
    if(height%2!=0)++height;
    const auto streamDuration=std::min(Rational{12,1},current->duration()-seconds(atMs));
    // A transport stream only needs a small decode cushion. Waiting for three
    // seconds made a basic Play operation feel broken on modest hardware.
    livePreviewPublishThresholdUs_=std::min<qint64>(1'500'000,std::max<qint64>(1,static_cast<qint64>(streamDuration.asLongDouble()*1'000'000.0L)));
    if(startLivePreviewChunk(generation,atMs,width,height))return true;
    livePreviewStarting_=false;emit livePreviewChanged();disposeLivePreviewProcess();return false;
}

bool TimelineController::startLivePreviewChunk(quint64 generation,qint64 chunkStartMs,int width,int height){
    const auto*current=sequence();if(!current||generation!=livePreviewGeneration_||!livePreviewDevice_)return false;
    const auto streamUrl=QUrl(QStringLiteral("ytp-preview:/program-%1.ts").arg(generation));
    const auto arguments=RenderEngine::previewStreamArguments(projectController_->project(),*current,width,height,seconds(chunkStartMs),QStringLiteral("pipe:1"),seconds(chunkStartMs-livePreviewStartMs_));
    if(arguments.isEmpty())return false;
    auto*process=new QProcess(this);livePreviewProcess_=process;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process,&QProcess::readyReadStandardOutput,this,[this,process,generation]{
        if(process!=livePreviewProcess_||generation!=livePreviewGeneration_)return;
        static_cast<PreviewStreamDevice*>(livePreviewDevice_)->append(process->readAllStandardOutput());
    });
    connect(process,&QProcess::readyReadStandardError,this,[this,process,generation,streamUrl,chunkStartMs]{
        if(process!=livePreviewProcess_||generation!=livePreviewGeneration_)return;
        const auto data=process->readAllStandardError();livePreviewDiagnostics_+=data;livePreviewProgressBuffer_+=data;
        qsizetype newline=0;
        while((newline=livePreviewProgressBuffer_.indexOf('\n'))>=0){
            const auto line=livePreviewProgressBuffer_.first(newline).trimmed();livePreviewProgressBuffer_.remove(0,newline+1);
            if(line.startsWith("out_time_us=")){bool ok=false;const auto value=line.mid(12).toLongLong(&ok);if(ok)livePreviewEncodedUs_=std::max(livePreviewEncodedUs_,(chunkStartMs-livePreviewStartMs_)*1000+value);}
        }
        if(livePreviewDiagnostics_.size()>64*1024)livePreviewDiagnostics_=livePreviewDiagnostics_.right(64*1024);
        publishLivePreviewIfBuffered(generation,streamUrl);
    });
    connect(process,&QProcess::errorOccurred,this,[this,process,generation](QProcess::ProcessError){
        if(process!=livePreviewProcess_||generation!=livePreviewGeneration_)return;
        const auto error=process->errorString();livePreviewUrl_=QUrl{};livePreviewStarting_=false;emit livePreviewChanged();
        projectController_->reportPlaybackError(QStringLiteral("Program stream failed to start: %1").arg(error));
    });
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,process,generation,streamUrl,chunkStartMs,width,height](int exitCode,QProcess::ExitStatus status){
        if(process!=livePreviewProcess_||generation!=livePreviewGeneration_){process->deleteLater();return;}
        static_cast<PreviewStreamDevice*>(livePreviewDevice_)->append(process->readAllStandardOutput());livePreviewDiagnostics_+=process->readAllStandardError();
        const auto error=QString::fromUtf8(livePreviewDiagnostics_).trimmed();
        livePreviewProcess_=nullptr;process->deleteLater();
        if(status!=QProcess::NormalExit||exitCode!=0){static_cast<PreviewStreamDevice*>(livePreviewDevice_)->finish();livePreviewUrl_=QUrl{};livePreviewStarting_=false;if(!error.isEmpty())projectController_->reportPlaybackError(QStringLiteral("Program stream stopped: %1").arg(error));emit livePreviewChanged();return;}
        const auto nextStartMs=chunkStartMs+12'000;
        if(nextStartMs<durationMs()){
            livePreviewProgressBuffer_.clear();livePreviewDiagnostics_.clear();
            if(startLivePreviewChunk(generation,nextStartMs,width,height))return;
            livePreviewUrl_=QUrl{};livePreviewStarting_=false;static_cast<PreviewStreamDevice*>(livePreviewDevice_)->finish();emit livePreviewChanged();return;
        }
        livePreviewProducerFinished_=true;publishLivePreviewIfBuffered(generation,streamUrl);
        static_cast<PreviewStreamDevice*>(livePreviewDevice_)->finish();
    });
    process->start(RenderEngine::ffmpegExecutable(),arguments);
    return true;
}

void TimelineController::invalidatePlaybackPreview() {
    stopLivePreview();
    QStringList stalePaths;
    for(const auto&entry:playbackWindowCache_)if(!entry.path.isEmpty())stalePaths.push_back(entry.path);
    const auto activePath=playbackPreviewUrl_.toLocalFile();
    if(!activePath.isEmpty()&&!stalePaths.contains(activePath))stalePaths.push_back(activePath);
    ++playbackRequestGeneration_;
    if(programCancellation_)programCancellation_->store(true);
    programTaskId_.clear();
    programCancellation_.reset();
    playbackPreviewUrl_=QUrl{};
    playbackPreviewStartMs_=0;
    playbackPreviewDurationMs_=0;
    previewRendering_=false;
    playbackRenderStartMs_=-1;
    playbackRenderForeground_=false;
    playbackWindowCache_.clear();
    emit playbackPreviewChanged();
    QTimer::singleShot(500,this,[stalePaths]{for(const auto&path:stalePaths)QFile::remove(path);});
}

const Sequence* TimelineController::sequence() const {
    const auto& values = projectController_->project().sequences();
    if(values.empty())return nullptr;
    if(const auto* active=projectController_->project().findSequence(activeSequenceId_.toStdString()))return active;
    return &values.front();
}

QVariantList TimelineController::tracks() const {
    QVariantList result;
    const auto* value = sequence();
    if (!value) return result;
    for (const auto& track : value->tracks) {
        result.push_back(QVariantMap{{"trackId", QString::fromStdString(track.id)},
          {"name", QString::fromStdString(track.name)}, {"kind", static_cast<int>(track.kind)},
          {"order", track.order}, {"locked", track.locked}, {"muted", track.muted},
          {"solo", track.solo}, {"visible", track.visible}, {"height", track.height},
          {"color", QString::fromStdString(track.color)}});
    }
    return result;
}

QVariantList TimelineController::items() const {
    QVariantList result;
    const auto* value = sequence();
    if (!value) return result;
    const auto modelStartMs = visibleStartMs_ < 0 ? qint64{-1} :
        (std::max<qint64>(0, visibleStartMs_ - 2'000) / 1'000) * 1'000;
    const auto modelEndMs = visibleEndMs_ < 0 ? qint64{-1} :
        ((visibleEndMs_ + 2'999) / 1'000) * 1'000;
    for (std::size_t trackIndex = 0; trackIndex < value->tracks.size(); ++trackIndex) {
        const auto& track = value->tracks[trackIndex];
        for (const auto& item : track.items) {
            if(modelStartMs>=0&&modelEndMs>=0&&(milliseconds(item.timelineEnd())<modelStartMs||milliseconds(item.timelineStart)>modelEndMs))continue;
            const auto* clip = projectController_->project().findLibraryClip(item.libraryClipId);
            const auto* media = projectController_->project().findMediaAsset(item.mediaAssetId);
            const auto visualKey = MediaCache::timelineVisualKey(item);
            const auto thumbnail = MediaCache::timelineThumbnailPath(visualKey);
            const auto waveform = MediaCache::timelineWaveformPath(visualKey);
            result.push_back(QVariantMap{
              {"itemId", QString::fromStdString(item.id)}, {"libraryClipId", QString::fromStdString(item.libraryClipId)},
              {"trackId", QString::fromStdString(track.id)},
              {"trackIndex", static_cast<int>(trackIndex)}, {"kind", static_cast<int>(track.kind)},
              {"name", !item.name.empty()?QString::fromStdString(item.name):(item.adjustmentClip?QStringLiteral("Adjustment"):(!item.nestedSequenceId.empty()?QStringLiteral("Nested sequence"):
                  (clip && !clip->name.empty() && clip->name != "Clip" ? QString::fromStdString(clip->name) :
                   (media ? QString::fromStdString(media->displayName) : QStringLiteral("Timeline clip")))))},
              {"mediaId", QString::fromStdString(item.mediaAssetId)},
              {"color", clip ? QString::fromStdString(clip->color) : QString::fromStdString(track.color)},
              {"startMs", milliseconds(item.timelineStart)}, {"durationMs", milliseconds(item.duration)},
              {"sourceStartMs", milliseconds(item.sourceRange.start())},
              {"sourceEndMs", milliseconds(item.sourceRange.end())}, {"speed", item.speed},
              {"reverse", item.reverse}, {"freeze", item.freezeFrame},
              {"fadeInMs", milliseconds(item.fadeIn)}, {"fadeOutMs", milliseconds(item.fadeOut)},
              {"linked", !item.linkedGroupId.empty()}, {"grouped", !item.groupId.empty()},
              {"selected", selectedIds_.contains(QString::fromStdString(item.id))},
              {"thumbnailUrl", QFileInfo::exists(thumbnail) ? QUrl::fromLocalFile(thumbnail) : QUrl{}},
              {"waveformUrl", QFileInfo::exists(waveform) ? QUrl::fromLocalFile(waveform) : QUrl{}},
              {"adjustment",item.adjustmentClip},{"nested",!item.nestedSequenceId.empty()}});
        }
    }
    return result;
}

QVariantList TimelineController::timelineRows() const {
    QHash<QString, QVariantList> itemsByTrack;
    for (const auto& value : items()) {
        const auto map = value.toMap();
        itemsByTrack[map.value(QStringLiteral("trackId")).toString()].push_back(map);
    }
    QVariantList result;
    for (const auto& value : tracks()) {
        auto row = value.toMap();
        row.insert(QStringLiteral("items"), itemsByTrack.value(row.value(QStringLiteral("trackId")).toString()));
        result.push_back(row);
    }
    return result;
}

QVariantList TimelineController::markers() const {
    QVariantList result;
    if (const auto* value = sequence()) for (const auto& marker : value->markers)
        result.push_back(QVariantMap{{"markerId", QString::fromStdString(marker.id)},
          {"timeMs", milliseconds(marker.time)}, {"label", QString::fromStdString(marker.label)},
          {"color", QString::fromStdString(marker.color)}});
    return result;
}

int TimelineController::rippleMode() const {
    return sequence() ? static_cast<int>(sequence()->rippleMode) : 0;
}
qint64 TimelineController::durationMs() const { return sequence() ? milliseconds(sequence()->duration()) : 0; }
double TimelineController::projectFrameDurationMs() const {const auto&settings=projectController_->project().settings();return static_cast<double>(settings.frameRateDenominator)*1000.0/static_cast<double>(settings.frameRateNumerator);}
QUrl TimelineController::programImageUrl() const {
    const auto* seq = sequence(); if (!seq) return {};
    const auto time = seconds(playheadMs_);
    for (const auto& track : seq->tracks) {
        if (track.kind != TrackKind::Video || !track.visible) continue;
        for (const auto& item : track.items) if (time >= item.timelineStart && time < item.timelineEnd()) {
            if(renderedPreviewItemId_==QString::fromStdString(item.id)&&!renderedPreviewUrl_.isEmpty())return renderedPreviewUrl_;
            const auto path = MediaCache::thumbnailPath(QString::fromStdString(item.libraryClipId));
            if (QFileInfo::exists(path)) return QUrl::fromLocalFile(path);
        }
    }
    return {};
}
QString TimelineController::programLabel() const {
    const auto* seq = sequence(); if (!seq) return {};
    const auto time = seconds(playheadMs_);
    for (const auto& track : seq->tracks) if (track.kind == TrackKind::Video && track.visible)
        for (const auto& item : track.items) if (time >= item.timelineStart && time < item.timelineEnd()) {
            if (const auto* clip = projectController_->project().findLibraryClip(item.libraryClipId)) return QString::fromStdString(clip->name);
        }
    return {};
}
QVariantMap TimelineController::instantPreview() const {
    const auto* seq = sequence();
    if (!seq) return {};
    const auto time = seconds(playheadMs_);
    const TimelineItem* videoItem = nullptr;
    const Track* videoTrack = nullptr;
    for (const auto& track : seq->tracks) {
        if (track.kind != TrackKind::Video || !track.visible) continue;
        for (const auto& item : track.items)
            if (time >= item.timelineStart && time < item.timelineEnd() &&
                    !item.adjustmentClip && item.nestedSequenceId.empty()) {
                videoItem = &item;
                videoTrack = &track;
                break;
            }
        if (videoItem) break;
    }
    if (!videoItem || videoItem->reverse || videoItem->freezeFrame || videoItem->speed <= 0) return {};
    const auto* media = projectController_->project().findMediaAsset(videoItem->mediaAssetId);
    if (!media || !QFileInfo::exists(QString::fromStdString(media->path))) return {};
    const auto ratio = Rational{static_cast<std::int64_t>(std::llround(videoItem->speed * 1'000'000.0)),1'000'000};
    const auto sourcePosition = videoItem->sourceRange.start() + (time - videoItem->timelineStart) * ratio;
    bool audioEnabled = false;
    bool audioExact = true;
    const bool anyAudioSolo = std::any_of(seq->tracks.begin(),seq->tracks.end(),[](const auto& track){return track.kind==TrackKind::Audio&&track.solo;});
    for (const auto& track : seq->tracks) {
        if (track.kind != TrackKind::Audio || track.muted || (anyAudioSolo && !track.solo)) continue;
        for (const auto& item : track.items) {
            if (time < item.timelineStart || time >= item.timelineEnd() ||
                    item.mediaAssetId != videoItem->mediaAssetId || item.linkedGroupId.empty() ||
                    item.linkedGroupId != videoItem->linkedGroupId || item.reverse || item.freezeFrame) continue;
            const auto audioRatio = Rational{static_cast<std::int64_t>(std::llround(item.speed * 1'000'000.0)),1'000'000};
            const auto audioPosition = item.sourceRange.start() + (time - item.timelineStart) * audioRatio;
            if (std::abs(static_cast<double>((audioPosition-sourcePosition).asLongDouble())) > .02) continue;
            audioEnabled = media->audioSampleRate > 0;
            audioExact = item.effects.empty() && track.effects.empty() && item.audio == AudioSettings{} &&
                         track.audio == AudioSettings{} && std::abs(item.pitchSemitones) < .000001 &&
                         std::abs(item.speed-1.0) < .000001;
            break;
        }
        if (audioEnabled) break;
    }
    // Direct source playback is fast for normal clips, but seeking the source player at every
    // boundary of a frame-repeat/stutter is both visibly discontinuous and prone to losing A/V
    // sync. Short structural fragments therefore use the rendered sequence preview instead.
    const bool longEnoughForDirectPlayback = videoItem->duration >= Rational{1,2};
    const bool videoExact = longEnoughForDirectPlayback && videoItem->effects.empty() && videoItem->masks.empty() &&
                            !videoItem->captionEnabled && videoItem->transform == TransformSettings{} &&
                            videoTrack && videoTrack->effects.empty() && std::abs(videoItem->speed-1.0) < .000001;
    const bool mixExact = seq->masterEffects.empty() && seq->masterAudio == AudioSettings{};
    return QVariantMap{{QStringLiteral("url"),QUrl::fromLocalFile(QString::fromStdString(media->path))},
                       {QStringLiteral("sourcePositionMs"),milliseconds(sourcePosition)},
                       {QStringLiteral("sourceStartMs"),milliseconds(videoItem->sourceRange.start())},
                       {QStringLiteral("timelineStartMs"),milliseconds(videoItem->timelineStart)},
                       {QStringLiteral("timelineEndMs"),milliseconds(videoItem->timelineEnd())},
                       {QStringLiteral("speed"),videoItem->speed},
                       {QStringLiteral("audioEnabled"),audioEnabled},
                       {QStringLiteral("draftSafe"),longEnoughForDirectPlayback},
                       {QStringLiteral("exact"),videoExact&&audioExact&&mixExact},
                       {QStringLiteral("itemId"),QString::fromStdString(videoItem->id)}};
}
void TimelineController::setPlayheadMs(qint64 value) {
    value = std::clamp<qint64>(value, 0, std::max<qint64>(0, durationMs())); if (playheadMs_ == value) return; playheadMs_ = value;++previewGeneration_;effectPreviewPending_=true;if(effectPreviewCancellation_)effectPreviewCancellation_->store(true); emit playheadChanged(); emit programPreviewChanged();if(!livePreviewActive())previewDebounce_.start();precacheUpcomingPlayback();
}
void TimelineController::setPlaybackPlayheadMs(qint64 value) {
    value=std::clamp<qint64>(value,0,std::max<qint64>(0,durationMs()));
    if(playheadMs_==value)return;
    playheadMs_=value;
    emit playheadChanged();
}
void TimelineController::setPixelsPerSecond(double value) {
    value = std::clamp(value, 5.0, 4000.0); if (pixelsPerSecond_ == value) return; pixelsPerSecond_ = value; emit zoomChanged();
}
void TimelineController::setSnapping(bool value) { if (snapping_ == value) return; snapping_ = value; emit snappingChanged(); }

bool TimelineController::mutate(const QString& description, const std::function<void(Sequence&)>& operation) {
    const auto* current = sequence();
    if (!current) {
        projectController_->reportTimelineStatus(description + QStringLiteral(" failed: no active sequence."));
        return false;
    }
    auto before = *current; auto after = before;
    try {
        operation(after);
    } catch (const std::exception& exception) {
        projectController_->reportTimelineStatus(description + QStringLiteral(" failed: ") +
                                                  QString::fromUtf8(exception.what()) + QStringLiteral("."));
        return false;
    }
    if (before == after) {
        projectController_->reportTimelineStatus(description +
                                                  QStringLiteral(" made no change. Select editable clips first."));
        return false;
    }
    return projectController_->applySequenceEdit(std::move(before), std::move(after), description.toStdString());
}

void TimelineController::setRippleMode(int value) {
    if (value < 0 || value > 2) return;
    mutate(QStringLiteral("Change ripple mode"), [value](Sequence& seq) { seq.rippleMode = static_cast<RippleMode>(value); });
}

bool TimelineController::insertClip(const QString& clipId, const QString& trackId, qint64 atMs,
                                    int editMode, const QString& replaceId) {
    std::vector<Id> inserted;
    const bool ok = mutate(QStringLiteral("Insert timeline clip"), [&](Sequence& seq) {
        const auto snapped = seconds(snap(atMs));
        const auto replacement = replaceId.isEmpty() ? std::optional<std::string_view>{} :
            std::optional<std::string_view>{replaceId.toStdString()};
        inserted = TimelineEditor::insertLibraryClip(projectController_->project(), seq,
            clipId.toStdString(), trackId.toStdString(), snapped,
            static_cast<EditMode>(std::clamp(editMode, 0, 2)), replacement).itemIds;
    });
    if (ok) { selectedIds_.clear(); for (const auto& id : inserted) selectedIds_.append(QString::fromStdString(id)); emit selectionChanged(); emit timelineChanged(); }
    return ok;
}

void TimelineController::select(const QString& itemId, bool additive) {
    if (!additive) selectedIds_.clear();
    if (additive && selectedIds_.contains(itemId)) selectedIds_.removeAll(itemId);
    else if (!selectedIds_.contains(itemId)) selectedIds_.append(itemId);
    if (const auto* seq = sequence()) {
        std::vector<Id> ids; for (const auto& id : selectedIds_) ids.push_back(id.toStdString());
        selectedIds_.clear(); for (const auto& id : TimelineEditor::expandedSelection(*seq, ids)) selectedIds_.append(QString::fromStdString(id));
    }
    emit selectionChanged();
}
void TimelineController::selectBox(qint64 startMs, qint64 endMs, int firstTrack, int lastTrack) {
    selectedIds_.clear(); if (startMs > endMs) std::swap(startMs, endMs); if (firstTrack > lastTrack) std::swap(firstTrack, lastTrack);
    if (const auto* seq = sequence()) for (int index = std::max(0, firstTrack); index <= lastTrack && index < static_cast<int>(seq->tracks.size()); ++index)
        for (const auto& item : seq->tracks[static_cast<std::size_t>(index)].items)
            if (milliseconds(item.timelineEnd()) >= startMs && milliseconds(item.timelineStart) <= endMs) selectedIds_.append(QString::fromStdString(item.id));
    if (const auto* seq = sequence()) {
        std::vector<Id> ids; for (const auto& id : selectedIds_) ids.push_back(id.toStdString());
        selectedIds_.clear();
        for (const auto& id : TimelineEditor::expandedSelection(*seq, ids))
            selectedIds_.append(QString::fromStdString(id));
    }
    emit selectionChanged();
}
void TimelineController::selectAll() { selectedIds_.clear(); if (const auto* seq = sequence()) for (const auto& track : seq->tracks) for (const auto& item : track.items) selectedIds_.append(QString::fromStdString(item.id)); emit selectionChanged(); }
void TimelineController::clearSelection() { if (selectedIds_.isEmpty()) return; selectedIds_.clear(); emit selectionChanged(); }

bool TimelineController::moveSelected(qint64 atMs, const QString& targetTrackId) {
    std::vector<Id> ids; for (const auto& id : selectedIds_) ids.push_back(id.toStdString());
    return mutate(QStringLiteral("Move timeline clips"), [&](Sequence& seq) { TimelineEditor::moveItems(seq, ids, seconds(snapMove(atMs)), targetTrackId.isEmpty() ? std::optional<std::string_view>{} : std::optional<std::string_view>{targetTrackId.toStdString()}); });
}
bool TimelineController::isRippleMoveFollower(const QString& itemId) const {
    const auto* seq=sequence();
    if(!seq||seq->rippleMode==RippleMode::Off||selectedIdSet_.contains(itemId))return false;
    Rational latest{};bool initialized=false;std::unordered_set<Id> affected;
    for(const auto&selectedId:selectedIds_){
        if(const auto*item=seq->findItem(selectedId.toStdString())){
            latest=initialized?std::max(latest,item->timelineEnd()):item->timelineEnd();
            affected.insert(item->trackId);initialized=true;
        }
    }
    const auto*item=seq->findItem(itemId.toStdString());
    const auto*track=item?seq->findTrack(item->trackId):nullptr;
    return initialized&&item&&track&&!track->locked&&item->timelineStart>=latest&&
        (seq->rippleMode==RippleMode::AllTracks||affected.contains(item->trackId));
}
bool TimelineController::renameItem(const QString& itemId,const QString& name){
    const auto id=itemId.toStdString();
    const auto value=name.trimmed().toStdString();
    return mutate(QStringLiteral("Rename timeline clip"),[&](Sequence&seq){
        const auto linkedItems=TimelineEditor::expandedSelection(seq,{id});
        if(linkedItems.empty())throw std::invalid_argument("timeline clip was not found");
        // A linked video/audio pair is one visible edit. Keep its title unified
        // so renaming either half replaces every generated "Clip #" label.
        for(const auto&linkedId:linkedItems)
            if(auto*item=seq.findItem(linkedId))item->name=value;
    });
}
bool TimelineController::splitSelected(qint64 atMs) { if (atMs < 0) atMs = playheadMs_; std::vector<Id> ids; for (const auto& id : selectedIds_) ids.push_back(id.toStdString()); return mutate(QStringLiteral("Split clips"), [&](Sequence& seq) { (void)TimelineEditor::splitItems(seq, ids, seconds(snap(atMs))); }); }
bool TimelineController::createLibraryClipFromSelection(const QString& name) {
    const auto* current=sequence();
    if(!current||selectedIds_.isEmpty()){
        projectController_->reportTimelineStatus(QStringLiteral("Select one cut timeline segment first."));
        return false;
    }
    const TimelineItem* selected=nullptr;
    for(const auto&id:selectedIds_){
        const auto*item=current->findItem(id.toStdString());
        if(!item||item->mediaAssetId.empty()||item->adjustmentClip||!item->nestedSequenceId.empty())continue;
        if(!selected){selected=item;continue;}
        if(item->mediaAssetId!=selected->mediaAssetId||item->sourceRange!=selected->sourceRange){
            projectController_->reportTimelineStatus(QStringLiteral("Select a single linked timeline segment before creating a clip."));
            return false;
        }
    }
    if(!selected){
        projectController_->reportTimelineStatus(QStringLiteral("The selection is not a source-media clip."));
        return false;
    }
    return projectController_->createLibraryClipForMedia(QString::fromStdString(selected->mediaAssetId),name,
        milliseconds(selected->sourceRange.start()),milliseconds(selected->sourceRange.end()));
}
bool TimelineController::deleteSelected() {
    std::vector<Id> ids;
    for (const auto& id : selectedIds_) ids.push_back(id.toStdString());

    const qint64 originalPlayhead = playheadMs_;
    qint64 adjustedPlayhead = playheadMs_;
    if (const auto* seq = sequence(); seq && seq->rippleMode != RippleMode::Off) {
        std::vector<std::pair<qint64, qint64>> ranges;
        for (const auto& id : ids) {
            if (const auto* item = seq->findItem(id))
                ranges.emplace_back(milliseconds(item->timelineStart), milliseconds(item->timelineEnd()));
        }
        std::sort(ranges.begin(), ranges.end());
        std::vector<std::pair<qint64, qint64>> merged;
        for (const auto& range : ranges) {
            if (merged.empty() || range.first > merged.back().second) merged.push_back(range);
            else merged.back().second = std::max(merged.back().second, range.second);
        }
        qint64 removedBefore = 0;
        for (const auto& [start, end] : merged) {
            if (playheadMs_ >= end) removedBefore += end - start;
            else if (playheadMs_ > start) {
                adjustedPlayhead = start - removedBefore;
                break;
            } else {
                adjustedPlayhead = playheadMs_ - removedBefore;
                break;
            }
            adjustedPlayhead = playheadMs_ - removedBefore;
        }
        adjustedPlayhead = std::max<qint64>(0, adjustedPlayhead);
    }

    const bool ok = mutate(QStringLiteral("Delete clips"), [&](Sequence& seq) {
        TimelineEditor::deleteItems(seq, ids);
    });
    if (ok) {
        clearSelection();
        const auto removedBeforePlayhead = std::max<qint64>(0, originalPlayhead - adjustedPlayhead);
        if (adjustedPlayhead != playheadMs_) setPlayheadMs(adjustedPlayhead);
        if (removedBeforePlayhead > 0) emit timelineCompacted(removedBeforePlayhead);
    }
    return ok;
}
bool TimelineController::duplicateSelected() { std::vector<Id> ids; for (const auto& id : selectedIds_) ids.push_back(id.toStdString()); std::vector<Id> result; const bool ok = mutate(QStringLiteral("Duplicate clips"), [&](Sequence& seq) { result = TimelineEditor::duplicateItems(seq, ids); }); if (ok) { selectedIds_.clear(); for (const auto& id : result) selectedIds_.append(QString::fromStdString(id)); emit selectionChanged(); emit timelineChanged(); } return ok; }
void TimelineController::copySelected() { clipboard_.clear(); clipboardOriginMs_ = 0; bool initialized = false; if (const auto* seq = sequence()) for (const auto& track : seq->tracks) for (const auto& item : track.items) if (selectedIds_.contains(QString::fromStdString(item.id))) { clipboard_.push_back(item); const auto ms = milliseconds(item.timelineStart); clipboardOriginMs_ = initialized ? std::min(clipboardOriginMs_, ms) : ms; initialized = true; } }
bool TimelineController::paste(qint64 atMs) {
    if (clipboard_.empty()) return false; if (atMs < 0) atMs = playheadMs_; std::vector<Id> pasted;
    const bool ok = mutate(QStringLiteral("Paste clips"), [&](Sequence& seq) { pasted = TimelineEditor::pasteItems(seq, clipboard_, seconds(snap(atMs))); });
    if (ok) { selectedIds_.clear(); for (const auto& id : pasted) selectedIds_.append(QString::fromStdString(id)); emit selectionChanged(); emit timelineChanged(); } return ok;
}
bool TimelineController::trimStart(const QString& id, qint64 at) { return mutate(QStringLiteral("Trim clip start"), [&](Sequence& seq){ TimelineEditor::trimItemStart(seq,id.toStdString(),seconds(snap(at))); }); }
bool TimelineController::trimEnd(const QString& id, qint64 at) { return mutate(QStringLiteral("Trim clip end"), [&](Sequence& seq){ TimelineEditor::trimItemEnd(seq,id.toStdString(),seconds(snap(at))); }); }
bool TimelineController::slip(const QString& id, qint64 at) { return mutate(QStringLiteral("Slip clip"), [&](Sequence& seq){ TimelineEditor::slipItem(projectController_->project(),seq,id.toStdString(),seconds(at)); }); }
bool TimelineController::roll(const QString& left, const QString& right, qint64 at) { return mutate(QStringLiteral("Roll edit"), [&](Sequence& seq){ TimelineEditor::rollEdit(seq,left.toStdString(),right.toStdString(),seconds(snap(at))); }); }
bool TimelineController::setFades(const QString& id,qint64 in,qint64 out) { return mutate(QStringLiteral("Set clip fades"), [&](Sequence& seq){ TimelineEditor::setItemFades(seq,id.toStdString(),seconds(in),seconds(out)); }); }
bool TimelineController::groupSelected(){ std::vector<Id> ids; for(auto&i:selectedIds_)ids.push_back(i.toStdString()); return mutate(QStringLiteral("Group clips"),[&](Sequence&s){TimelineEditor::groupItems(s,ids);}); }
bool TimelineController::ungroupSelected(){ std::vector<Id> ids; for(auto&i:selectedIds_)ids.push_back(i.toStdString()); return mutate(QStringLiteral("Ungroup clips"),[&](Sequence&s){TimelineEditor::ungroupItems(s,ids);}); }
bool TimelineController::linkSelected(){ std::vector<Id> ids; for(auto&i:selectedIds_)ids.push_back(i.toStdString()); return mutate(QStringLiteral("Link clips"),[&](Sequence&s){TimelineEditor::linkItems(s,ids);}); }
bool TimelineController::unlinkSelected(){ std::vector<Id> ids; for(auto&i:selectedIds_)ids.push_back(i.toStdString()); return mutate(QStringLiteral("Unlink clips"),[&](Sequence&s){TimelineEditor::unlinkItems(s,ids);}); }
bool TimelineController::addTrack(int kind,const QString& name){ return mutate(QStringLiteral("Add track"),[&](Sequence&s){(void)TimelineEditor::addTrack(s,kind==1?TrackKind::Audio:TrackKind::Video,name.isEmpty()?(kind==1?"Audio":"Video"):name.toStdString());}); }
bool TimelineController::removeTrack(const QString& id){ return mutate(QStringLiteral("Remove track"),[&](Sequence&s){TimelineEditor::removeTrack(s,id.toStdString());}); }
bool TimelineController::moveTrack(const QString&id,int direction){if(direction==0)return false;return mutate(QStringLiteral("Reorder track"),[&](Sequence&s){const auto found=std::find_if(s.tracks.begin(),s.tracks.end(),[&](const auto&track){return track.id==id.toStdString();});if(found==s.tracks.end())throw std::invalid_argument("track missing");const auto index=static_cast<int>(std::distance(s.tracks.begin(),found));const auto target=std::clamp(index+(direction<0?-1:1),0,static_cast<int>(s.tracks.size())-1);if(target==index)return;std::swap(s.tracks[static_cast<std::size_t>(index)],s.tracks[static_cast<std::size_t>(target)]);for(std::size_t order=0;order<s.tracks.size();++order)s.tracks[order].order=static_cast<int>(order);});}
bool TimelineController::setTrackState(const QString& id,const QString& property,const QVariant& value){ return mutate(QStringLiteral("Change track"),[&](Sequence&s){auto*t=s.findTrack(id.toStdString());if(!t)throw std::invalid_argument("track missing");if(property=="locked")t->locked=value.toBool();else if(property=="muted")t->muted=value.toBool();else if(property=="solo")t->solo=value.toBool();else if(property=="visible")t->visible=value.toBool();else if(property=="height")t->height=std::clamp(value.toInt(),36,180);else if(property=="color")t->color=value.toString().toStdString();else throw std::invalid_argument("unknown track property");}); }
bool TimelineController::addMarker(qint64 at,const QString& label){ return mutate(QStringLiteral("Add marker"),[&](Sequence&s){(void)TimelineEditor::addMarker(s,seconds(snap(at)),label.isEmpty()?"Marker":label.toStdString());}); }
bool TimelineController::removeMarker(const QString&id){ return mutate(QStringLiteral("Remove marker"),[&](Sequence&s){TimelineEditor::removeMarker(s,id.toStdString());}); }
qint64 TimelineController::snap(qint64 proposed) const { if(!snapping_||!sequence())return std::max<qint64>(0,proposed); return milliseconds(TimelineEditor::snapTime(*sequence(),seconds(proposed),seconds(playheadMs_),seconds(static_cast<qint64>(12000.0/pixelsPerSecond_)))); }
qint64 TimelineController::snapMove(qint64 proposedStart) const {
    proposedStart = std::max<qint64>(0, proposedStart);
    const auto* seq = sequence();
    if (!snapping_ || !seq || selectedIds_.isEmpty()) return proposedStart;

    QSet<QString> selected(selectedIds_.begin(), selectedIds_.end());
    qint64 currentStart = std::numeric_limits<qint64>::max();
    qint64 currentEnd = 0;
    for (const auto& id : selectedIds_) if (const auto* item = seq->findItem(id.toStdString())) {
        currentStart = std::min(currentStart, milliseconds(item->timelineStart));
        currentEnd = std::max(currentEnd, milliseconds(item->timelineEnd()));
    }
    if (currentStart == std::numeric_limits<qint64>::max()) return proposedStart;

    const auto proposedEnd = currentEnd + (proposedStart - currentStart);
    const auto tolerance = std::max<qint64>(1, static_cast<qint64>(std::llround(12'000.0 / pixelsPerSecond_)));
    qint64 bestAdjustment = tolerance + 1;
    const auto consider = [&](qint64 candidate) {
        for (const auto edge : {proposedStart, proposedEnd}) {
            const auto adjustment = candidate - edge;
            if (std::abs(adjustment) <= tolerance && std::abs(adjustment) < std::abs(bestAdjustment))
                bestAdjustment = adjustment;
        }
    };
    consider(0);
    consider(playheadMs_);
    for (const auto& marker : seq->markers) consider(milliseconds(marker.time));
    for (const auto& track : seq->tracks) for (const auto& item : track.items) {
        if (selected.contains(QString::fromStdString(item.id))) continue;
        consider(milliseconds(item.timelineStart));
        consider(milliseconds(item.timelineEnd()));
    }
    return std::max<qint64>(0, proposedStart + (std::abs(bestAdjustment) <= tolerance ? bestAdjustment : 0));
}
void TimelineController::sanitizeSelection(){ if(const auto*seq=sequence()){for(auto it=selectedIds_.begin();it!=selectedIds_.end();)if(!seq->findItem(it->toStdString()))it=selectedIds_.erase(it);else++it;}else selectedIds_.clear(); emit selectionChanged(); }

QString TimelineController::shortcut(const QString& action) const {
    static const QVariantMap defaults{{"split","S"},{"delete","Delete"},{"duplicate","D"},
        {"copy","Ctrl+C"},{"paste","Ctrl+V"},{"group","G"},{"unlink","U"}};
    QSettings settings;
    return settings.value(QStringLiteral("shortcuts/") + action, defaults.value(action)).toString();
}
bool TimelineController::configureShortcuts(const QVariantMap& values) {
    QSet<QString> used;
    for (auto it = values.begin(); it != values.end(); ++it) {
        const auto normalized = QKeySequence::fromString(it.value().toString(), QKeySequence::PortableText).toString(QKeySequence::PortableText);
        if (normalized.isEmpty() || used.contains(normalized)) return false;
        used.insert(normalized);
    }
    QSettings settings;
    for (auto it = values.begin(); it != values.end(); ++it)
        settings.setValue(QStringLiteral("shortcuts/") + it.key(), QKeySequence::fromString(it.value().toString(), QKeySequence::PortableText).toString(QKeySequence::PortableText));
    emit shortcutsChanged(); return true;
}
void TimelineController::useVegasShortcuts() {
    QSettings settings; settings.beginGroup(QStringLiteral("shortcuts")); settings.remove(QString{}); settings.endGroup(); emit shortcutsChanged();
}
void TimelineController::selectFollowing(const QString& itemId) {
    selectedIds_.clear(); if (const auto* seq=sequence()) if (const auto* source=seq->findItem(itemId.toStdString()))
        for (const auto& track:seq->tracks) for(const auto& item:track.items) if(item.timelineStart>=source->timelineStart) selectedIds_.append(QString::fromStdString(item.id));
    emit selectionChanged();
}
void TimelineController::selectSameSource(const QString& itemId) {
    selectedIds_.clear(); if (const auto* seq=sequence()) if (const auto* source=seq->findItem(itemId.toStdString()))
        for (const auto& track:seq->tracks) for(const auto& item:track.items) if(item.libraryClipId==source->libraryClipId) selectedIds_.append(QString::fromStdString(item.id));
    emit selectionChanged();
}

QVariantMap TimelineController::inspector() const {
    if(!inspectorCacheDirty_)return inspectorCache_;
    inspectorCacheDirty_=false;inspectorCache_.clear();
    if(selectedIds_.isEmpty()||!sequence())return inspectorCache_;
    const auto* item=sequence()->findItem(selectedIds_.front().toStdString());if(!item)return inspectorCache_;
    QVariantList effects;
    for(const auto&e:item->effects){QVariantList parameters;for(const auto&p:e.parameters){QVariantList keys;for(const auto&k:p.keyframes)keys.push_back(QVariantMap{{"id",QString::fromStdString(k.id)},{"timeMs",milliseconds(k.time)},{"value",k.value},{"interpolation",static_cast<int>(k.interpolation)}});parameters.push_back(QVariantMap{{"name",QString::fromStdString(p.name)},{"value",p.value},{"minimum",p.minimum},{"maximum",p.maximum},{"unit",QString::fromStdString(p.unit)},{"keyframes",keys}});}const auto*d=findEffectDescriptor(e.type);effects.push_back(QVariantMap{{"id",QString::fromStdString(e.id)},{"type",QString::fromStdString(e.type)},{"name",d?QString::fromStdString(d->name):QString::fromStdString(e.type)},{"enabled",e.enabled},{"parameters",parameters}});}
    QVariantList masks;for(const auto&m:item->masks)masks.push_back(QVariantMap{{"id",QString::fromStdString(m.id)},{"shape",static_cast<int>(m.shape)},{"x",m.x},{"y",m.y},{"width",m.width},{"height",m.height},{"feather",m.feather},{"opacity",m.opacity},{"inverted",m.inverted}});
    const auto&t=item->transform;inspectorCache_={{"itemId",QString::fromStdString(item->id)},{"trackId",QString::fromStdString(item->trackId)},{"startMs",milliseconds(item->timelineStart)},{"sourceStartMs",milliseconds(item->sourceRange.start())},
      {"positionX",t.positionX},{"positionY",t.positionY},{"scaleX",t.scaleX},{"scaleY",t.scaleY},{"rotation",t.rotation},{"anchorX",t.anchorX},{"anchorY",t.anchorY},{"opacity",t.opacity},
      {"cropLeft",t.cropLeft},{"cropTop",t.cropTop},{"cropRight",t.cropRight},{"cropBottom",t.cropBottom},{"fit",t.fit},{"flipHorizontal",t.flipHorizontal},{"flipVertical",t.flipVertical},
      {"gainDb",item->audio.gainDb},{"pan",item->audio.pan},{"speed",item->speed},{"pitch",item->pitchSemitones},{"preservePitch",item->preservePitch},{"reverse",item->reverse},{"freeze",item->freezeFrame},{"effects",effects},{"masks",masks},{"captionEnabled",item->captionEnabled},{"captionText",QString::fromStdString(item->captionText)},{"captionSize",item->captionSize},{"captionColor",QString::fromStdString(item->captionColor)},{"adjustment",item->adjustmentClip},{"nested",!item->nestedSequenceId.empty()}};
    return inspectorCache_;
}
QVariantList TimelineController::availableEffects() const {
    QVariantList result;
    const QSet<QString> colorEffects{"brightness_contrast","saturation","hue","invert","grayscale","tint","solarize","color_cycle","channel_swap"};
    const QSet<QString> detailEffects{"blur","sharpen","pixelate","posterize","threshold","emboss","neon_edges","cartoon_edges","vignette","edge_echo","pixel_bloom","xray_edges","oscilloscope","dilation_bloom","erosion_decay","halftone","ordered_dither","cmyk_halftone","clone_grid","edge_glow_native","soft_glow_native","ink_cartoon","film_projector"};
    const QSet<QString> signalEffects{"rgb_split","chromatic_aberration","scanlines","vhs_noise","bad_tv","datamosh","block_shuffle","glitch_bands","horizontal_shuffle","vertical_shuffle","field_corruption","chroma_planes","pixel_sort","analog_nosync","digital_glitch"};
    const QSet<QString> motionEffects{"wave_warp","lens_warp","kaleidoscope","screen_shake","vertical_roll","impact_zoom","spin","pendulum","perspective_tilt","elastic_wave","shear","fisheye","tiny_planet","funhouse","vortex","radial_ripple","melt","water_surface","elastic_scale"};
    const QSet<QString> timeEffects{"recursive_trails","time_smear","frame_blend","strobe","motion_burn","frame_randomizer","motion_amplify","frame_xor","temporal_mosaic","frame_skip","temporal_heat","temporal_stain","video_feedback","nervous_frames","light_graffiti","frame_delay","film_gate_weave"};
    const QSet<QString> keyEffects{"chroma_key"};
    const QSet<QString> toneEffects{"eq","highpass","lowpass","normalize","telephone","bass_boost","treble_boost","frequency_shift","virtual_bass"};
    const QSet<QString> dynamicsEffects{"compressor","limiter","noisegate"};
    const QSet<QString> destructionEffects{"distortion","bitcrush","tremolo"};
    const QSet<QString> modulationEffects{"vibrato","flanger","chorus","phaser","ring_mod","robotize","whisperize","haas_spread"};
    const QSet<QString> heavyEffects{"recursive_trails","time_smear","frame_blend","datamosh","bad_tv","elastic_wave","glitch_bands","motion_burn","frame_randomizer","motion_amplify","frame_xor","fisheye","tiny_planet","robotize","whisperize","temporal_mosaic","funhouse","vortex","radial_ripple","melt","temporal_heat","temporal_stain","video_feedback","pixel_sort","water_surface","elastic_scale","light_graffiti","digital_glitch","clone_grid","frame_delay"};
    const QSet<QString> ytpEffects{"invert","posterize","threshold","rgb_split","chromatic_aberration","wave_warp","kaleidoscope","edge_echo","recursive_trails","time_smear","frame_blend","screen_shake","datamosh","scanlines","vhs_noise","solarize","neon_edges","color_cycle","strobe","channel_swap","vertical_roll","bad_tv","cartoon_edges","impact_zoom","spin","pendulum","perspective_tilt","elastic_wave","glitch_bands","thermal","motion_burn","block_shuffle","shear","fisheye","tiny_planet","oscilloscope","frame_randomizer","motion_amplify","frame_xor","pixel_bloom","xray_edges","horizontal_shuffle","vertical_shuffle","temporal_mosaic","dilation_bloom","erosion_decay","field_corruption","chroma_planes","frame_skip","funhouse","vortex","radial_ripple","melt","halftone","temporal_heat","temporal_stain","video_feedback","pixel_sort","water_surface","elastic_scale","analog_nosync","film_gate_weave","nervous_frames","light_graffiti","digital_glitch","ordered_dither","cmyk_halftone","clone_grid","edge_glow_native","soft_glow_native","ink_cartoon","film_projector","frame_delay","telephone","tremolo","vibrato","flanger","distortion","bitcrush","bass_boost","treble_boost","chorus","phaser","stereo_widen","crystalizer","ring_mod","frequency_shift","robotize","whisperize","virtual_bass","haas_spread"};
    const QHash<QString,QString> descriptions{
        {"brightness_contrast","Balance exposure and punch."},{"saturation","Boost or drain color intensity."},{"hue","Rotate every color through the spectrum."},
        {"invert","Flip the image into a negative."},{"grayscale","Remove color while preserving detail."},{"blur","Soften detail with a fast box blur."},
        {"sharpen","Increase edge contrast and crispness."},{"pixelate","Break the picture into chunky blocks."},{"posterize","Reduce the image to bold color steps."},
        {"threshold","Crush the image to stark black and white."},{"rgb_split","Pull red and blue channels apart."},{"chromatic_aberration","Offset chroma for a lens-glitch fringe."},
        {"wave_warp","Continuously bend and scroll the frame."},{"screen_shake","Jolt the crop with rhythmic camera shake."},{"recursive_trails","Layer recent frames into decaying trails."},
        {"time_smear","Average a longer run of neighboring frames."},{"frame_blend","Blend each frame with the previous one."},{"datamosh","Difference-blend frames into codec-like decay."},
        {"scanlines","Overlay adjustable CRT raster lines."},{"vhs_noise","Add animated tape noise and chroma drift."},{"solarize","Invert highlights past a chosen threshold."},
        {"emboss","Carve luminance into raised metallic relief."},{"neon_edges","Turn contours into saturated neon lines."},{"vignette","Darken the frame edges dramatically."},
        {"color_cycle","Animate hue continuously over time."},{"strobe","Flash the frame at a controllable rhythm."},{"channel_swap","Cross-wire red and blue channels."},
        {"vertical_roll","Roll the picture like a slipping CRT."},{"bad_tv","Combine noise, scanlines, and vertical roll."},{"cartoon_edges","Crush color around graphic ink-like edges."},
        {"eq","Shape low, mid, and high frequency bands."},{"telephone","Band-limit and drive audio like a radio."},{"tremolo","Rhythmically chop volume without cutting clips."},
        {"vibrato","Wobble pitch smoothly over time."},{"flanger","Sweep a short delay for a jet-like comb effect."},{"bass_boost","Push the low end for impact."},
        {"treble_boost","Overstate high frequencies and bite."},{"distortion","Drive audio into controlled saturation."},{"bitcrush","Reduce digital resolution for crunchy artifacts."},
        {"impact_zoom","Punch hard into a chosen part of the frame."},{"spin","Rotate continuously at an adjustable speed."},{"pendulum","Rock the frame rhythmically from side to side."},
        {"perspective_tilt","Collapse the frame into a severe perspective plane."},{"elastic_wave","Pull individual color channels through animated elastic waves."},{"glitch_bands","Tear moving horizontal slices in opposing directions."},
        {"thermal","Remap luminance into a vivid false-color heat palette."},{"motion_burn","Hold bright moving shapes as persistent afterimages."},
        {"chorus","Multiply the voice into a detuned moving crowd."},{"phaser","Sweep combed phase notches through the sound."},{"stereo_widen","Explode mono or narrow audio across the stereo field."},
        {"crystalizer","Exaggerate transients into brittle glass-like attacks."},{"ring_mod","Multiply sound by an oscillator for metallic robotic tones."}
        ,{"block_shuffle","Reassemble the image from deterministic shuffled blocks."},{"shear","Skew horizontal and vertical space into a slanted plane."},{"fisheye","Wrap ordinary footage into an extreme circular lens."}
        ,{"tiny_planet","Project the frame into a stereographic miniature world."},{"oscilloscope","Overlay live component traces, grids, and signal statistics."},{"frame_randomizer","Pull frames from a randomized temporal cache."}
        ,{"motion_amplify","Magnify only the differences between neighboring frames."},{"frame_xor","XOR successive frames into violently changing digital silhouettes."},{"pixel_bloom","Replace regions with their brightest, darkest, or average pixel blocks."}
        ,{"xray_edges","Scan edges into a false-color X-ray map."},{"frequency_shift","Move every frequency by a fixed amount instead of ordinary pitch shifting."},{"robotize","Discard spectral phase to create a rigid synthetic voice."}
        ,{"whisperize","Randomize spectral phase until speech becomes breath and texture."},{"virtual_bass","Synthesize subharmonic weight below the original signal."},{"haas_spread","Create extreme stereo space from millisecond inter-channel delays."}
        ,{"horizontal_shuffle","Reorder horizontal image strips without resizing the frame."},{"vertical_shuffle","Reorder vertical image strips without resizing the frame."},{"temporal_mosaic","Display several consecutive moments simultaneously in an aspect-safe grid."}
        ,{"dilation_bloom","Grow bright regions outward through repeated morphological dilation."},{"erosion_decay","Let dark regions consume surrounding picture detail."},{"field_corruption","Interleave and exchange luma and chroma video fields."}
        ,{"chroma_planes","Reassign stored image planes for severe channel reconstruction."},{"frame_skip","Create low-cadence stop-motion while retaining timeline duration."},{"funhouse","Warp both image axes through independently moving sine glass."}
        ,{"vortex","Twist the center and edges at different animated angular speeds."},{"radial_ripple","Send aspect-preserving concentric waves through the source."},{"melt","Pull image columns downward at independently moving rates."}
        ,{"halftone","Convert moving imagery into adjustable printed dots."},{"temporal_heat","Fuse neighboring frames with a heat transfer blend."},{"temporal_stain","Let prior frames permanently stain new motion."}
        ,{"video_feedback","Feed transformed output recursively into following frames."},{"pixel_sort","Sort pixel runs by image luminance rather than merely shifting blocks."},{"water_surface","Refract the image through a continuously moving water surface."}
        ,{"elastic_scale","Stretch the source elastically around a movable center."},{"analog_nosync","Lose analog horizontal synchronization without applying a VHS skin."},{"film_gate_weave","Move the entire frame through a mechanical projector gate."}
        ,{"nervous_frames","Recall unstable nearby frames from a temporal memory buffer."},{"light_graffiti","Accumulate moving highlights into persistent luminous writing."},{"digital_glitch","Generate native block shifts and independent color corruption."}
        ,{"ordered_dither","Apply a real ordered dither matrix at the existing resolution."},{"cmyk_halftone","Separate print colors into independently angled dot screens."},{"clone_grid","Repeat the full source in a configurable aspect-preserving image grid."}
        ,{"edge_glow_native","Generate a dedicated luminous contour glow."},{"soft_glow_native","Bloom highlights optically while retaining underlying detail."},{"ink_cartoon","Simplify regions and create proper inked cartoon boundaries."}
        ,{"film_projector","Add mechanical grain, dust, weave, and exposure flicker."},{"frame_delay","Recall delayed source frames without cutting the timeline event."}
    };
    QSettings settings;
    for(const auto&descriptor:effectCatalog()){
        const auto type=QString::fromStdString(descriptor.type);
        QString category;
        if(!descriptor.audio)category=colorEffects.contains(type)?QStringLiteral("Video / Color"):
            detailEffects.contains(type)?QStringLiteral("Video / Detail & Style"):
            signalEffects.contains(type)?QStringLiteral("Video / Glitch & Signal"):
            motionEffects.contains(type)?QStringLiteral("Video / Distort & Motion"):
            timeEffects.contains(type)?QStringLiteral("Video / Time & Trails"):
            keyEffects.contains(type)?QStringLiteral("Video / Keying"):QStringLiteral("Video / Utility");
        else category=toneEffects.contains(type)?QStringLiteral("Audio / Tone"):
            dynamicsEffects.contains(type)?QStringLiteral("Audio / Dynamics"):
            destructionEffects.contains(type)?QStringLiteral("Audio / Destruction"):
            modulationEffects.contains(type)?QStringLiteral("Audio / Modulation"):QStringLiteral("Audio / Space");
        const auto description=descriptions.value(type,descriptor.audio?QStringLiteral("Shape the selected event's sound."):QStringLiteral("Transform the selected event's image."));
        result.push_back(QVariantMap{{"type",type},{"name",QString::fromStdString(descriptor.name)},
                                     {"audio",descriptor.audio},{"category",category},{"description",description},
                                     {"tags",QStringList{category,type,description}},{"ytp",ytpEffects.contains(type)},
                                     {"heavy",heavyEffects.contains(type)},
                                     {"favorite",settings.value(QStringLiteral("effectFavorites/")+type,false).toBool()},
                                     {"preset",false}});
    }
    return result;
}

bool TimelineController::toggleEffectFavorite(const QString& type){
    if(!findEffectDescriptor(type.toStdString()))return false;
    QSettings settings;const auto key=QStringLiteral("effectFavorites/")+type;
    settings.setValue(key,!settings.value(key,false).toBool());settings.sync();
    emit effectsBrowserChanged();return true;
}

QUrl TimelineController::sourceThumbnailUrl(const QString& mediaId, qint64 sourceTimeMs) {
    const auto* media = projectController_->project().findMediaAsset(mediaId.toStdString());
    if (!media || media->path.empty() || media->duration <= Rational{}) return {};
    const auto durationMs = std::max<qint64>(1, milliseconds(media->duration));
    const auto frameDurationMs = media->frameRateNumerator > 0
        ? 1000.0 * static_cast<double>(media->frameRateDenominator) /
              static_cast<double>(media->frameRateNumerator)
        : 40.0;
    const auto frame = std::llround(static_cast<double>(std::clamp<qint64>(sourceTimeMs, 0, durationMs - 1)) /
                                    frameDurationMs);
    const auto quantizedMs = std::clamp<qint64>(
        static_cast<qint64>(std::llround(static_cast<double>(frame) * frameDurationMs)), 0, durationMs - 1);
    const auto output = MediaCache::timelineFramePath(mediaId, quantizedMs);
    if (QFileInfo::exists(output)) return QUrl::fromLocalFile(output);
    if (!timelineThumbnailRequests_.contains(output)) {
        timelineThumbnailRequests_.insert(output);
        timelineThumbnailQueue_.prepend(TimelineThumbnailRequest{
            QString::fromStdString(media->path), mediaId, quantizedMs, output});
        while (timelineThumbnailQueue_.size() > 96) {
            const auto stale = timelineThumbnailQueue_.takeLast();
            timelineThumbnailRequests_.remove(stale.cachePath);
        }
        QTimer::singleShot(0, this, &TimelineController::startNextTimelineThumbnail);
    }
    return {};
}

void TimelineController::startNextTimelineThumbnail() {
    if (timelineThumbnailActive_ || timelineThumbnailQueue_.isEmpty() || livePreviewActive()) return;
    timelineThumbnailActive_ = true;
    const auto request = timelineThumbnailQueue_.dequeue();
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, request] {
        const auto success = watcher->result();
        watcher->deleteLater();
        timelineThumbnailActive_ = false;
        timelineThumbnailRequests_.remove(request.cachePath);
        if (success) {
            ++timelineThumbnailGeneration_;
            emit timelineThumbnailCacheChanged();
        }
        startNextTimelineThumbnail();
    });
    watcher->setFuture(QtConcurrent::run([request] {
        QString error;
        return MediaCache::generateTimelineFrame(
            request.mediaPath, seconds(request.sourceTimeMs), request.mediaId,
            request.sourceTimeMs, &error);
    }));
}
QVariantList TimelineController::mixerTracks() const {QVariantList r;if(const auto*s=sequence()){const auto pack=[](const auto&effects){QVariantList list;for(const auto&e:effects){const auto*d=findEffectDescriptor(e.type);QVariantList params;for(const auto&p:e.parameters)params.push_back(QVariantMap{{"name",QString::fromStdString(p.name)},{"value",p.value},{"minimum",p.minimum},{"maximum",p.maximum},{"unit",QString::fromStdString(p.unit)}});list.push_back(QVariantMap{{"id",QString::fromStdString(e.id)},{"name",d?QString::fromStdString(d->name):QString::fromStdString(e.type)},{"enabled",e.enabled},{"parameters",params}});}return list;};for(const auto&t:s->tracks)if(t.kind==TrackKind::Audio)r.push_back(QVariantMap{{"trackId",QString::fromStdString(t.id)},{"name",QString::fromStdString(t.name)},{"gainDb",t.audio.gainDb},{"pan",t.audio.pan},{"muted",t.muted},{"solo",t.solo},{"effects",pack(t.effects)},{"peak",t.muted?0.0:std::min(1.0,std::pow(10.0,t.audio.gainDb/20.0)*0.72)}});r.push_back(QVariantMap{{"trackId","master"},{"name","MASTER"},{"gainDb",s->masterAudio.gainDb},{"pan",s->masterAudio.pan},{"muted",false},{"solo",false},{"effects",pack(s->masterEffects)},{"limiter",s->masterLimiter},{"peak",std::min(1.0,std::pow(10.0,s->masterAudio.gainDb/20.0)*0.68)}});}return r;}
QStringList TimelineController::effectPresets() const {QSettings s;s.beginGroup("effectPresets");return s.childKeys();}

bool TimelineController::setTransformValue(const QString&property,double value){if(selectedIds_.isEmpty())return false;return mutate("Transform",[&](Sequence&s){auto*i=s.findItem(selectedIds_.front().toStdString());if(!i)throw std::invalid_argument("item missing");auto t=i->transform;if(property=="positionX")t.positionX=value;else if(property=="positionY")t.positionY=value;else if(property=="scaleX")t.scaleX=value;else if(property=="scaleY")t.scaleY=value;else if(property=="rotation")t.rotation=value;else if(property=="anchorX")t.anchorX=value;else if(property=="anchorY")t.anchorY=value;else if(property=="opacity")t.opacity=value;else if(property=="cropLeft")t.cropLeft=value;else if(property=="cropTop")t.cropTop=value;else if(property=="cropRight")t.cropRight=value;else if(property=="cropBottom")t.cropBottom=value;else throw std::invalid_argument("property missing");EffectsEditor::setTransform(s,i->id,t);});}
bool TimelineController::setTransformFlag(const QString&property,bool value){if(selectedIds_.isEmpty())return false;return mutate("Transform flag",[&](Sequence&s){auto*i=s.findItem(selectedIds_.front().toStdString());if(!i)throw std::invalid_argument("item missing");auto t=i->transform;if(property=="fit")t.fit=value;else if(property=="flipHorizontal")t.flipHorizontal=value;else if(property=="flipVertical")t.flipVertical=value;else throw std::invalid_argument("property missing");EffectsEditor::setTransform(s,i->id,t);});}
bool TimelineController::addTransformKeyframe(const QString&property,qint64 timeMs,double value,int interpolation){if(selectedIds_.isEmpty())return false;return mutate("Transform keyframe",[&](Sequence&s){(void)EffectsEditor::addTransformKeyframe(s,selectedIds_.front().toStdString(),property.toStdString(),seconds(timeMs),value,static_cast<KeyframeInterpolation>(std::clamp(interpolation,0,2)));});}
bool TimelineController::setClipAudio(double gain,double pan){if(selectedIds_.isEmpty())return false;return mutate("Clip audio",[&](Sequence&s){bool changed=false;for(const auto&id:selectedIds_){auto*i=s.findItem(id.toStdString());if(!i)continue;const auto*t=s.findTrack(i->trackId);if(!t||t->kind!=TrackKind::Audio)continue;auto a=i->audio;a.gainDb=gain;a.pan=pan;EffectsEditor::setClipAudio(s,i->id,a);changed=true;}if(!changed)throw std::invalid_argument("audio item missing");});}
bool TimelineController::setTrackAudio(const QString&id,double gain,double pan){return mutate("Track audio",[&](Sequence&s){auto*t=s.findTrack(id.toStdString());if(!t)throw std::invalid_argument("track missing");auto a=t->audio;a.gainDb=gain;a.pan=pan;EffectsEditor::setTrackAudio(s,t->id,a);});}
bool TimelineController::setMasterAudio(double gain,double pan){return mutate("Master audio",[&](Sequence&s){auto a=s.masterAudio;a.gainDb=gain;a.pan=pan;EffectsEditor::setMasterAudio(s,a);});}
bool TimelineController::setSpeed(double speed,bool preserve){if(selectedIds_.isEmpty())return false;return mutate("Speed",[&](Sequence&s){for(const auto&id:selectedIds_)TimelineEditor::setItemSpeed(s,id.toStdString(),speed,preserve);});}
bool TimelineController::setPitch(double value){if(selectedIds_.isEmpty()||value < -48||value>48)return false;return mutate("Pitch",[&](Sequence&s){bool changed=false;for(const auto&id:selectedIds_){auto*i=s.findItem(id.toStdString());if(!i)continue;const auto*t=s.findTrack(i->trackId);if(!t||t->kind!=TrackKind::Audio)continue;i->pitchSemitones=value;changed=true;}if(!changed)throw std::invalid_argument("audio item missing");});}
bool TimelineController::setReverse(bool value){if(selectedIds_.isEmpty())return false;return mutate("Reverse",[&](Sequence&s){for(const auto&id:selectedIds_)TimelineEditor::setItemReverse(s,id.toStdString(),value);});}
bool TimelineController::setFreeze(bool value,qint64 sourceMs){if(selectedIds_.isEmpty())return false;return mutate("Freeze frame",[&](Sequence&s){TimelineEditor::setFreezeFrame(s,selectedIds_.front().toStdString(),value,seconds(sourceMs));});}
bool TimelineController::addEffect(int target,const QString&id,const QString&type){return mutate("Add effect",[&](Sequence&s){(void)EffectsEditor::addEffect(s,static_cast<EffectTarget>(std::clamp(target,0,2)),id.toStdString(),type.toStdString());});}
bool TimelineController::addEffectToSelection(const QString& type){
    const auto* descriptor=findEffectDescriptor(type.toStdString());
    if(!descriptor||selectedIds_.isEmpty())return false;
    return mutate("Add effect",[&](Sequence&s){
        TimelineItem* target=nullptr;
        for(const auto&id:selectedIds_){
            auto*item=s.findItem(id.toStdString());const auto*track=item?s.findTrack(item->trackId):nullptr;
            if(item&&track&&descriptor->audio==(track->kind==TrackKind::Audio)){target=item;break;}
        }
        if(!target){
            for(const auto&id:selectedIds_){
                const auto*selected=s.findItem(id.toStdString());
                if(!selected||selected->linkedGroupId.empty())continue;
                for(auto&track:s.tracks)for(auto&item:track.items)
                    if(item.linkedGroupId==selected->linkedGroupId&&descriptor->audio==(track.kind==TrackKind::Audio)){
                        target=&item;break;
                    }
                if(target)break;
            }
        }
        if(!target)throw std::invalid_argument("no compatible selected event");
        (void)EffectsEditor::addEffect(s,EffectTarget::Item,target->id,descriptor->type);
        selectedIds_={QString::fromStdString(target->id)};
    });
}
bool TimelineController::removeEffect(int target,const QString&id,const QString&effectId){return mutate("Remove effect",[&](Sequence&s){EffectsEditor::removeEffect(s,static_cast<EffectTarget>(std::clamp(target,0,2)),id.toStdString(),effectId.toStdString());});}
bool TimelineController::moveEffect(int target,const QString&id,const QString&effectId,int offset){return mutate("Reorder effect",[&](Sequence&s){EffectsEditor::moveEffect(s,static_cast<EffectTarget>(std::clamp(target,0,2)),id.toStdString(),effectId.toStdString(),offset);});}
bool TimelineController::bypassEffect(int target,const QString&id,const QString&effectId,bool bypass){return mutate("Bypass effect",[&](Sequence&s){EffectsEditor::setEffectEnabled(s,static_cast<EffectTarget>(std::clamp(target,0,2)),id.toStdString(),effectId.toStdString(),!bypass);});}
bool TimelineController::resetEffect(int target,const QString&id,const QString&effectId){return mutate("Reset effect",[&](Sequence&s){EffectsEditor::resetEffect(s,static_cast<EffectTarget>(std::clamp(target,0,2)),id.toStdString(),effectId.toStdString());});}
bool TimelineController::setEffectParameter(int target,const QString&id,const QString&effectId,const QString&parameter,double value){return mutate("Effect parameter",[&](Sequence&s){EffectsEditor::setParameter(s,static_cast<EffectTarget>(std::clamp(target,0,2)),id.toStdString(),effectId.toStdString(),parameter.toStdString(),value);});}
bool TimelineController::addKeyframe(int target,const QString&id,const QString&effectId,const QString&parameter,qint64 timeMs,double value,int interpolation){return mutate("Add keyframe",[&](Sequence&s){(void)EffectsEditor::addKeyframe(s,static_cast<EffectTarget>(std::clamp(target,0,2)),id.toStdString(),effectId.toStdString(),parameter.toStdString(),seconds(timeMs),value,static_cast<KeyframeInterpolation>(std::clamp(interpolation,0,2)));});}
bool TimelineController::removeKeyframe(int target,const QString&id,const QString&effectId,const QString&parameter,const QString&keyId){return mutate("Remove keyframe",[&](Sequence&s){EffectsEditor::removeKeyframe(s,static_cast<EffectTarget>(std::clamp(target,0,2)),id.toStdString(),effectId.toStdString(),parameter.toStdString(),keyId.toStdString());});}
void TimelineController::copyAttributes(){if(!selectedIds_.isEmpty()&&sequence())if(const auto*i=sequence()->findItem(selectedIds_.front().toStdString()))attributeClipboard_=*i;}
bool TimelineController::pasteAttributes(bool transform,bool timing,bool audio,bool effects){if(!attributeClipboard_)return false;std::vector<Id>ids;for(const auto&id:selectedIds_)ids.push_back(id.toStdString());return mutate("Paste attributes",[&](Sequence&s){auto source=*attributeClipboard_;auto*track=s.findTrack(source.trackId);if(!track)throw std::invalid_argument("source track missing");source.id=createId();track->items.push_back(source);EffectsEditor::pasteItemAttributes(s,source.id,ids,transform,timing,audio,effects);std::erase_if(track->items,[&](const auto&i){return i.id==source.id;});});}

bool TimelineController::saveEffectPreset(const QString&name){if(name.trimmed().isEmpty()||selectedIds_.isEmpty()||!sequence())return false;const auto*i=sequence()->findItem(selectedIds_.front().toStdString());if(!i)return false;QJsonArray effects;for(const auto&e:i->effects){QJsonArray params;for(const auto&p:e.parameters)params.append(QJsonObject{{"name",QString::fromStdString(p.name)},{"value",p.value}});effects.append(QJsonObject{{"type",QString::fromStdString(e.type)},{"enabled",e.enabled},{"parameters",params}});}QJsonObject root{{"effects",effects},{"speed",i->speed},{"preservePitch",i->preservePitch},{"pitch",i->pitchSemitones},{"gain",i->audio.gainDb},{"pan",i->audio.pan}};QSettings s;s.setValue("effectPresets/"+name.trimmed(),QJsonDocument(root).toJson(QJsonDocument::Compact));emit presetsChanged();return true;}
bool TimelineController::applyEffectPreset(const QString&name){QSettings settings;const auto doc=QJsonDocument::fromJson(settings.value("effectPresets/"+name).toByteArray());if(!doc.isObject()||selectedIds_.isEmpty())return false;const auto root=doc.object();return mutate("Apply preset",[&](Sequence&s){auto*i=s.findItem(selectedIds_.front().toStdString());if(!i)throw std::invalid_argument("item missing");i->effects.clear();for(const auto&v:root.value("effects").toArray()){const auto o=v.toObject();auto e=createEffect(o.value("type").toString().toStdString());e.enabled=o.value("enabled").toBool(true);for(const auto&pv:o.value("parameters").toArray()){const auto po=pv.toObject();if(auto*p=findParameter(e,po.value("name").toString().toStdString()))p->value=po.value("value").toDouble();}if(const auto error=validateEffect(e))throw std::invalid_argument(*error);i->effects.push_back(std::move(e));}TimelineEditor::setItemSpeed(s,i->id,root.value("speed").toDouble(1),root.value("preservePitch").toBool(true));i=s.findItem(selectedIds_.front().toStdString());if(!i)throw std::invalid_argument("item missing");i->pitchSemitones=root.value("pitch").toDouble();i->audio.gainDb=root.value("gain").toDouble();i->audio.pan=root.value("pan").toDouble();if(validateAudio(i->audio)||!std::isfinite(i->pitchSemitones)||i->pitchSemitones < -48||i->pitchSemitones>48)throw std::invalid_argument("preset audio values invalid");});}
bool TimelineController::removeEffectPreset(const QString&name){QSettings s;const auto key="effectPresets/"+name;if(!s.contains(key))return false;s.remove(key);emit presetsChanged();return true;}
void TimelineController::setPreviewQuality(int quality){mutate("Preview quality",[&](Sequence&s){s.previewQuality=static_cast<PreviewQuality>(std::clamp(quality,0,4));});}
bool TimelineController::addAudioKeyframe(int target,const QString&id,const QString&parameter,qint64 timeMs,double value,int interpolation){return mutate("Audio envelope keyframe",[&](Sequence&s){(void)EffectsEditor::addAudioKeyframe(s,static_cast<EffectTarget>(std::clamp(target,0,2)),id.toStdString(),parameter.toStdString(),seconds(timeMs),value,static_cast<KeyframeInterpolation>(std::clamp(interpolation,0,2)));});}
bool TimelineController::setMasterLimiter(bool enabled){return mutate("Master limiter",[&](Sequence&s){s.masterLimiter=enabled;});}
bool TimelineController::activatePlaybackWindow(qint64 startMs) {
    const auto found=std::find_if(playbackWindowCache_.begin(),playbackWindowCache_.end(),[startMs](const auto&entry){return entry.startMs==startMs&&QFileInfo::exists(entry.path);});
    if(found==playbackWindowCache_.end())return false;
    found->lastUsed=++playbackWindowUseCounter_;
    if(playbackPreviewUrl_.toLocalFile()==found->path&&playbackPreviewStartMs_==found->startMs&&
            playbackPreviewDurationMs_==found->durationMs)return true;
    playbackPreviewUrl_=QUrl::fromLocalFile(found->path);
    playbackPreviewStartMs_=found->startMs;
    playbackPreviewDurationMs_=found->durationMs;
    ++previewGeneration_;
    emit playbackPreviewChanged();
    return true;
}

void TimelineController::prunePlaybackWindowCache() {
    // Keep every rendered window for the unchanged timeline. Only apply LRU eviction when the
    // playback-window files themselves exceed a generous disk budget; a clip count limit makes
    // long projects unexpectedly re-render effects even when ample cache space is available.
    constexpr qint64 maximumPlaybackCacheBytes=1LL*1024*1024*1024;
    auto cacheBytes=[this]{qint64 total=0;for(const auto&entry:playbackWindowCache_)total+=std::max<qint64>(0,QFileInfo(entry.path).size());return total;};
    auto bytes=cacheBytes();
    while(bytes>maximumPlaybackCacheBytes){
        const auto active=playbackPreviewUrl_.toLocalFile();
        auto oldest=playbackWindowCache_.end();
        for(auto entry=playbackWindowCache_.begin();entry!=playbackWindowCache_.end();++entry)
            if(entry->path!=active&&(oldest==playbackWindowCache_.end()||entry->lastUsed<oldest->lastUsed))oldest=entry;
        if(oldest==playbackWindowCache_.end())break;
        const auto path=oldest->path;const auto fileBytes=std::max<qint64>(0,QFileInfo(path).size());
        playbackWindowCache_.erase(oldest);
        QFile::remove(path);
        bytes=std::max<qint64>(0,bytes-fileBytes);
    }
}

bool TimelineController::renderPlaybackWindow(qint64 requestedMs,bool foreground) {
    const auto*current=sequence();if(!current||current->duration()<=Rational{})return false;
    requestedMs=std::clamp<qint64>(requestedMs,0,std::max<qint64>(0,durationMs()-1));
    constexpr qint64 windowStrideMs=2'000;
    auto startMs=(requestedMs/windowStrideMs)*windowStrideMs;
    if(foreground&&!playbackPreviewUrl_.isEmpty()&&requestedMs>=playbackPreviewStartMs_&&
            requestedMs<playbackPreviewStartMs_+playbackPreviewDurationMs_&&
            playbackPreviewStartMs_+playbackPreviewDurationMs_-requestedMs<350)
        startMs=std::min(playbackPreviewStartMs_+windowStrideMs,std::max<qint64>(0,durationMs()-1));
    startMs=(startMs/windowStrideMs)*windowStrideMs;
    const auto cached=std::find_if(playbackWindowCache_.begin(),playbackWindowCache_.end(),[startMs](const auto&entry){return entry.startMs==startMs&&QFileInfo::exists(entry.path);});
    if(cached!=playbackWindowCache_.end()){
        cached->lastUsed=++playbackWindowUseCounter_;
        return foreground?activatePlaybackWindow(startMs):true;
    }
    if(previewRendering_&&playbackRenderStartMs_==startMs){playbackRenderForeground_=playbackRenderForeground_||foreground;return true;}
    if(previewRendering_&&!foreground)return false;
    if(programCancellation_)programCancellation_->store(true);
    const auto start=seconds(startMs);if(start>=current->duration())return false;
    const auto request=++playbackRequestGeneration_;
    const auto cacheId=QString::fromStdString(current->id)+QStringLiteral("-window-")+QString::number(startMs)+QStringLiteral("-")+QString::number(request);
    const auto output=MediaCache::sequencePreviewPath(cacheId);
    // Chunks used to be eight seconds long on a four-second stride. That made
    // every expensive frame in the overlap render twice and delayed the first
    // usable frame set. Two-second chunks are non-overlapping and prefetched.
    const auto duration=std::min(Rational{2,1},current->duration()-start);
    const auto durationMs=std::max<qint64>(1,milliseconds(duration));
    const auto project=projectController_->project();
    int width=1280;
    if(current->previewQuality==PreviewQuality::Automatic)width=640;
    else if(current->previewQuality==PreviewQuality::Half)width=960;
    else if(current->previewQuality==PreviewQuality::Quarter)width=480;
    const auto projectWidth=std::max(1,project.settings().width),projectHeight=std::max(1,project.settings().height);
    int height=std::max(2,static_cast<int>(std::llround(static_cast<double>(width)*projectHeight/projectWidth)));
    if(height%2!=0)++height;
    const auto sequenceCopy=*current;
    const auto cancellation=std::make_shared<std::atomic_bool>(false);programCancellation_=cancellation;
    playbackRenderStartMs_=startMs;playbackRenderForeground_=foreground;
    previewRendering_=true;emit playbackPreviewChanged();
    auto*watcher=new QFutureWatcher<RenderResult>(this);
    connect(watcher,&QFutureWatcher<RenderResult>::finished,this,[this,watcher,output,request,cancellation,startMs,durationMs]{
        const auto result=watcher->result();watcher->deleteLater();
        if(request!=playbackRequestGeneration_){QFile::remove(output);return;}
        const bool foreground=playbackRenderForeground_;
        previewRendering_=false;playbackRenderStartMs_=-1;playbackRenderForeground_=false;
        if(programCancellation_==cancellation)programCancellation_.reset();
        if(result.success){
            playbackWindowCache_.erase(std::remove_if(playbackWindowCache_.begin(),playbackWindowCache_.end(),[startMs](const auto&entry){return entry.startMs==startMs;}),playbackWindowCache_.end());
            playbackWindowCache_.push_back({startMs,durationMs,output,++playbackWindowUseCounter_});
            prunePlaybackWindowCache();
            const bool playheadInside=playheadMs_>=startMs&&playheadMs_<startMs+durationMs;
            const bool directPreviewExact=instantPreview().value(QStringLiteral("exact")).toBool();
            if((foreground&&playheadInside)||(playheadInside&&!directPreviewExact))activatePlaybackWindow(startMs);
            else emit playbackPreviewChanged();
            if(foreground&&startMs+2'000<this->durationMs())
                QTimer::singleShot(0,this,[this,startMs]{renderPlaybackWindow(startMs+2'000,false);});
        }else{
            QFile::remove(output);
            if(!result.cancelled)projectController_->reportPlaybackError(result.error);
            emit playbackPreviewChanged();
        }
    });
    watcher->setFuture(QtConcurrent::run([project,sequenceCopy,output,width,height,start,duration,cancellation]{return RenderEngine::renderPreviewWindow(project,sequenceCopy,output,width,height,start,duration,*cancellation);}));
    return true;
}

void TimelineController::cancelPlaybackPreviewRendering(){
    if(programCancellation_)programCancellation_->store(true);
    playbackRenderForeground_=false;
}

void TimelineController::precacheUpcomingPlayback() {
    const auto*current=sequence();if(!current||previewRendering_||livePreviewActive()||current->duration()<=Rational{})return;
    const auto now=seconds(playheadMs_);const auto horizon=now+Rational{4,1};
    std::optional<Rational> upcoming;
    const bool masterComplex=!current->masterEffects.empty()||current->masterAudio!=AudioSettings{};
    if(masterComplex)upcoming=now;
    for(const auto&track:current->tracks){
        const bool trackComplex=!track.effects.empty()||track.audio!=AudioSettings{};
        for(const auto&item:track.items){
            if(item.timelineEnd()<=now||item.timelineStart>horizon)continue;
            const bool itemComplex=item.duration<Rational{1,2}||item.reverse||item.freezeFrame||
                std::abs(item.speed-1.0)>.000001||!item.effects.empty()||!item.masks.empty()||
                item.captionEnabled||item.transform!=TransformSettings{}||item.audio!=AudioSettings{}||
                std::abs(item.pitchSemitones)>.000001||trackComplex;
            if(itemComplex&&(!upcoming||item.timelineStart<*upcoming))upcoming=std::max(now,item.timelineStart);
        }
    }
    if(upcoming)renderPlaybackWindow(milliseconds(*upcoming),false);
}

bool TimelineController::renderPlaybackPreview(){return renderPlaybackWindow(playheadMs_,true);}

qint64 TimelineController::stepFrame(qint64 currentPositionMs,int direction) const {currentPositionMs=std::clamp<qint64>(currentPositionMs,0,durationMs());if(direction==0)return currentPositionMs;const auto&settings=projectController_->project().settings();const auto frame=frameDuration(settings.frameRateNumerator,settings.frameRateDenominator);const auto scaled=seconds(currentPositionMs)/frame;std::int64_t index;if(direction>0){index=static_cast<std::int64_t>(std::floor(scaled.asLongDouble()))+1;while(milliseconds(timeAtFrame(index,settings.frameRateNumerator,settings.frameRateDenominator))<=currentPositionMs)++index;}else{index=static_cast<std::int64_t>(std::ceil(scaled.asLongDouble()))-1;while(index>0&&milliseconds(timeAtFrame(index,settings.frameRateNumerator,settings.frameRateDenominator))>=currentPositionMs)--index;}const auto stepped=milliseconds(timeAtFrame(std::max<std::int64_t>(0,index),settings.frameRateNumerator,settings.frameRateDenominator));return std::clamp<qint64>(stepped,0,durationMs());}

std::vector<Id> TimelineController::selectedItemIds() const {
    std::vector<Id> result;
    for (const auto& id : selectedIds_) result.push_back(id.toStdString());
    return result;
}

bool TimelineController::runToolkit(const QString& description, const MacroStep& step,
                                    const std::function<ToolkitResult(Sequence&)>& operation) {
    ToolkitResult toolkitResult;
    const bool ok = mutate(description, [&](Sequence& value) { toolkitResult = operation(value); });
    if (!ok) return false;
    selectedIds_.clear();
    for (const auto& id : toolkitResult.itemIds) selectedIds_.append(QString::fromStdString(id));
    if (macroRecording_) recordedMacroSteps_.push_back(step);
    randomizerPlan_.reset();
    emit selectionChanged(); emit timelineChanged(); emit toolkitChanged();
    return true;
}

QVariantList TimelineController::ytpVisualPresets() const {
    QVariantList result;
    const QSet<QString> dynamic{
        "feedback_void","pixel_sort_crush","liquid_memory","projector_break","analog_freefall",
        "graffiti_ghost","nervous_breakdown","cmyk_attack","clone_army","dither_game",
        "elastic_reaction","native_kaleido",
        "strip_tornado","nine_lives","liquid_lens","gravity_well","video_melt","newspaper_riot",
        "cellular_bloom","rotting_film","interlace_demon","stop_motion_panic","shockwave","heat_memory",
        "oil_stain","morph_monster","strip_mine","tunnel_vision","crt_surgery","comic_freeze","dream_print",
        "fisheye_panic","tiny_planet_spin","scope_creep","time_scramble","motion_detector",
        "xor_nightmare","pixel_bloom_pack","xray_fever","slanted_universe",
        "vhs_breakdown","mirror_hell","acid_trip","crt_meltdown","neon_pulse",
        "memory_leak","vertical_sync","cartoon_panic","solar_flare","rgb_quake",
        "data_fever","prism_tunnel","ghost_echo","channel_surfer"};
    const QSet<QString> temporal{"feedback_void","liquid_memory","projector_break","graffiti_ghost","nervous_breakdown","native_kaleido","nine_lives","video_melt","rotting_film","stop_motion_panic","heat_memory","oil_stain","comic_freeze","dream_print","time_scramble","motion_detector","xor_nightmare","pixel_bloom_pack","tiny_planet_spin","memory_leak","data_fever","ghost_echo"};
    for (const auto& preset : YtpToolkit::visualPresets()) result.push_back(QVariantMap{
        {"id", QString::fromStdString(preset.id)}, {"name", QString::fromStdString(preset.name)},
        {"description", QString::fromStdString(preset.description)},
        {"dynamic",dynamic.contains(QString::fromStdString(preset.id))},
        {"temporal",temporal.contains(QString::fromStdString(preset.id))}});
    return result;
}

QVariantList TimelineController::ytpAudioPresets() const {
    QVariantList result;
    for(const auto&preset:YtpToolkit::audioPresets())result.push_back(QVariantMap{
        {"id",QString::fromStdString(preset.id)},{"name",QString::fromStdString(preset.name)},
        {"description",QString::fromStdString(preset.description)}});
    return result;
}

QVariantList TimelineController::ytpCombinedPresets() const {
    QVariantList result;
    for(const auto&preset:YtpToolkit::combinedPresets())result.push_back(QVariantMap{
        {"id",QString::fromStdString(preset.id)},{"name",QString::fromStdString(preset.name)},
        {"description",QString::fromStdString(preset.description)},{"dynamic",true},{"combined",true}});
    return result;
}

bool TimelineController::buildStutter(int repeats, qint64 sliceMs, bool alternate) {
    MacroStep step{YtpTool::Stutter, {{"repeats",static_cast<double>(repeats)}, {"sliceMs",static_cast<double>(sliceMs)}, {"alternate",alternate?1.0:0.0}}, {}};
    const auto ids=selectedItemIds(); return runToolkit("YTP Stutter Builder",step,[&](Sequence& s){return YtpToolkit::stutter(s,ids,repeats,seconds(sliceMs),alternate);});
}
bool TimelineController::buildRapidReverse(int segments, qint64 segmentMs) {
    MacroStep step{YtpTool::RapidReverse, {{"segments",static_cast<double>(segments)}, {"segmentMs",static_cast<double>(segmentMs)}}, {}};
    const auto ids=selectedItemIds(); return runToolkit("YTP Rapid Reverse",step,[&](Sequence& s){return YtpToolkit::rapidReverse(s,ids,segments,seconds(segmentMs));});
}
bool TimelineController::buildFrameRepeat(int sourceFrames, int repeats) {
    const auto& settings=projectController_->project().settings();
    MacroStep step{YtpTool::FrameRepeat, {{"sourceFrames",static_cast<double>(sourceFrames)}, {"repeats",static_cast<double>(repeats)}, {"frameRateNum",static_cast<double>(settings.frameRateNumerator)}, {"frameRateDen",static_cast<double>(settings.frameRateDenominator)}}, {}};
    const auto ids=selectedItemIds(); return runToolkit("YTP Frame Repeat",step,[&](Sequence& s){return YtpToolkit::frameRepeat(s,ids,frameDuration(settings.frameRateNumerator,settings.frameRateDenominator),sourceFrames,repeats);});
}
bool TimelineController::buildRhythmRepeat(qint64 atMs,double bpm,int beats,qint64 gateMs,bool markers) {
    MacroStep step{YtpTool::RhythmRepeat, {{"atMs",static_cast<double>(atMs)}, {"bpm",bpm}, {"beats",static_cast<double>(beats)}, {"gateMs",static_cast<double>(gateMs)}, {"markers",markers?1.0:0.0}}, {}};
    const auto ids=selectedItemIds(); return runToolkit("YTP Rhythm Repeat",step,[&](Sequence& s){return YtpToolkit::rhythmRepeat(s,ids,seconds(atMs),bpm,beats,seconds(gateMs),markers);});
}
bool TimelineController::buildSpeedLadder(int steps,double first,double last,double pitch,bool preserve) {
    MacroStep step{YtpTool::SpeedLadder, {{"steps",static_cast<double>(steps)}, {"startSpeed",first}, {"endSpeed",last}, {"pitchStep",pitch}, {"preservePitch",preserve?1.0:0.0}}, {}};
    const auto ids=selectedItemIds(); return runToolkit("YTP Speed Ladder",step,[&](Sequence& s){return YtpToolkit::speedLadder(s,ids,steps,first,last,pitch,preserve);});
}
bool TimelineController::applySafeEarrape(double intensity) {
    MacroStep step{YtpTool::SafeEarrape, {{"intensity",intensity}}, {}};
    const auto ids=selectedItemIds(); return runToolkit("YTP Safe Earrape",step,[&](Sequence& s){return YtpToolkit::safeEarrape(s,ids,intensity);});
}
bool TimelineController::applyYtpVisualPreset(const QString& preset) {
    MacroStep step{YtpTool::VisualPreset, {}, {{"preset",preset.toStdString()}}};
    const auto ids=selectedItemIds(); return runToolkit("YTP Visual Preset",step,[&](Sequence& s){return YtpToolkit::applyVisualPreset(s,ids,preset.toStdString());});
}
bool TimelineController::applyYtpAudioPreset(const QString& preset) {
    MacroStep step{YtpTool::AudioPreset, {}, {{"preset",preset.toStdString()}}};
    const auto ids=selectedItemIds();return runToolkit("YTP Audio Preset",step,[&](Sequence&s){return YtpToolkit::applyAudioPreset(s,ids,preset.toStdString());});
}
bool TimelineController::applyYtpCombinedPreset(const QString& preset) {
    MacroStep step{YtpTool::CombinedPreset, {}, {{"preset",preset.toStdString()}}};
    const auto ids=selectedItemIds();return runToolkit("YTP Audio + Visual Preset",step,[&](Sequence&s){return YtpToolkit::applyCombinedPreset(s,ids,preset.toStdString());});
}
bool TimelineController::buildSentenceMixer(const QString& orderText) {
    std::vector<int> order;
    for (const auto& value : orderText.split(',',Qt::SkipEmptyParts)) { bool ok=false; const int index=value.trimmed().toInt(&ok); if(!ok)return false; order.push_back(index); }
    MacroStep step{YtpTool::SentenceMixer, {}, {{"order",orderText.toStdString()}}};
    const auto ids=selectedItemIds(); return runToolkit("YTP Sentence Mixer",step,[&](Sequence& s){return YtpToolkit::sentenceMixer(s,ids,order);});
}

namespace {
QJsonObject macroStepJson(const MacroStep& step) {
    QJsonObject numbers, strings;
    for (const auto& [key,value] : step.numbers) numbers.insert(QString::fromStdString(key),value);
    for (const auto& [key,value] : step.strings) strings.insert(QString::fromStdString(key),QString::fromStdString(value));
    return {{"tool",static_cast<int>(step.tool)},{"numbers",numbers},{"strings",strings}};
}
MacroStep macroStepFromJson(const QJsonObject& object) {
    MacroStep step; step.tool=static_cast<YtpTool>(std::clamp(object.value("tool").toInt(),0,7));
    const auto numbers=object.value("numbers").toObject();
    for(auto it=numbers.begin();it!=numbers.end();++it)step.numbers[it.key().toStdString()]=it.value().toDouble();
    const auto strings=object.value("strings").toObject();
    for(auto it=strings.begin();it!=strings.end();++it)step.strings[it.key().toStdString()]=it.value().toString().toStdString();
    return step;
}
std::vector<YtpMacro> builtInMacros() {
    return {
        {"Classic Stutter Fry",{{YtpTool::Stutter,{{"repeats",6},{"sliceMs",90},{"alternate",1}},{}},{YtpTool::VisualPreset,{},{{"preset","deep_fried"}}}}},
        {"Reverse Meltdown",{{YtpTool::RapidReverse,{{"segments",8},{"segmentMs",70}},{}},{YtpTool::VisualPreset,{},{{"preset","acid_trip"}}}}},
        {"Impact Spam",{{YtpTool::FrameRepeat,{{"sourceFrames",2},{"repeats",5},{"frameRateNum",30000},{"frameRateDen",1001}},{}},{YtpTool::SafeEarrape,{{"intensity",.5}},{}}}}
    };
}
}

QStringList TimelineController::ytpMacros() const {
    QStringList result; for(const auto& macro:builtInMacros())result.append(QString::fromStdString(macro.name));
    QSettings settings; settings.beginGroup("ytpMacros"); result.append(settings.childKeys()); result.removeDuplicates(); result.sort(Qt::CaseInsensitive); return result;
}
std::optional<YtpMacro> TimelineController::loadYtpMacro(const QString& name) const {
    for(const auto& macro:builtInMacros())if(QString::fromStdString(macro.name)==name)return macro;
    QSettings settings;const auto document=QJsonDocument::fromJson(settings.value("ytpMacros/"+name).toByteArray());if(!document.isObject())return std::nullopt;
    YtpMacro macro;macro.name=name.toStdString();for(const auto& value:document.object().value("steps").toArray())macro.steps.push_back(macroStepFromJson(value.toObject()));
    return macro.steps.empty()?std::nullopt:std::optional<YtpMacro>{macro};
}
void TimelineController::saveYtpMacro(const YtpMacro& macro) {
    QJsonArray steps;for(const auto& step:macro.steps)steps.append(macroStepJson(step));QSettings settings;settings.setValue("ytpMacros/"+QString::fromStdString(macro.name),QJsonDocument(QJsonObject{{"steps",steps}}).toJson(QJsonDocument::Compact));settings.sync();
}
void TimelineController::startMacroRecording(){macroRecording_=true;recordedMacroSteps_.clear();emit toolkitChanged();}
void TimelineController::cancelMacroRecording(){macroRecording_=false;recordedMacroSteps_.clear();emit toolkitChanged();}
bool TimelineController::saveRecordedMacro(const QString& name){const auto clean=name.trimmed();if(!macroRecording_||recordedMacroSteps_.empty()||clean.isEmpty()||clean.contains('/')||clean.contains('\\'))return false;saveYtpMacro(YtpMacro{clean.toStdString(),recordedMacroSteps_});macroRecording_=false;recordedMacroSteps_.clear();emit toolkitChanged();return true;}
bool TimelineController::applyYtpMacro(const QString& name){const auto macro=loadYtpMacro(name);if(!macro)return false;const auto ids=selectedItemIds();const bool wasRecording=macroRecording_;macroRecording_=false;MacroStep marker;const bool ok=runToolkit("Apply YTP macro",marker,[&](Sequence& s){return YtpToolkit::applyMacro(s,ids,*macro);});macroRecording_=wasRecording;if(wasRecording&&ok)recordedMacroSteps_.insert(recordedMacroSteps_.end(),macro->steps.begin(),macro->steps.end());emit toolkitChanged();return ok;}
bool TimelineController::removeYtpMacro(const QString& name){for(const auto& builtIn:builtInMacros())if(QString::fromStdString(builtIn.name)==name)return false;QSettings settings;const auto key="ytpMacros/"+name;if(!settings.contains(key))return false;settings.remove(key);settings.sync();emit toolkitChanged();return true;}
QVariantList TimelineController::macroEditorSteps() const {static const QStringList names{"Stutter","Rapid Reverse","Frame Repeat","Rhythm Repeat","Speed Ladder","Safe Earrape","Visual Preset","Sentence Mixer","Audio Preset"};QVariantList result;for(std::size_t i=0;i<macroEditorSteps_.size();++i){const auto&step=macroEditorSteps_[i];result.push_back(QVariantMap{{"index",static_cast<int>(i)},{"tool",static_cast<int>(step.tool)},{"name",names.value(static_cast<int>(step.tool))},{"numbers",static_cast<int>(step.numbers.size())},{"strings",static_cast<int>(step.strings.size())}});}return result;}
bool TimelineController::loadMacroEditor(const QString&name){const auto macro=loadYtpMacro(name);if(!macro)return false;macroEditorSteps_=macro->steps;emit toolkitChanged();return true;}
void TimelineController::addMacroEditorStep(int tool){MacroStep step;step.tool=static_cast<YtpTool>(std::clamp(tool,0,8));switch(step.tool){case YtpTool::Stutter:step.numbers={{"repeats",6},{"sliceMs",90},{"alternate",1}};break;case YtpTool::RapidReverse:step.numbers={{"segments",8},{"segmentMs",70}};break;case YtpTool::FrameRepeat:step.numbers={{"sourceFrames",2},{"repeats",5},{"frameRateNum",30000},{"frameRateDen",1001}};break;case YtpTool::RhythmRepeat:step.numbers={{"atMs",0},{"bpm",120},{"beats",8},{"gateMs",100},{"markers",1}};break;case YtpTool::SpeedLadder:step.numbers={{"steps",6},{"startSpeed",.5},{"endSpeed",3},{"pitchStep",2},{"preservePitch",0}};break;case YtpTool::SafeEarrape:step.numbers={{"intensity",.65}};break;case YtpTool::VisualPreset:step.strings={{"preset","deep_fried"}};break;case YtpTool::SentenceMixer:step.strings={{"order","0,1"}};break;case YtpTool::AudioPreset:step.strings={{"preset","robot_radio"}};break;}macroEditorSteps_.push_back(std::move(step));emit toolkitChanged();}
bool TimelineController::moveMacroEditorStep(int index,int offset){const int target=index+offset;if(index<0||target<0||index>=static_cast<int>(macroEditorSteps_.size())||target>=static_cast<int>(macroEditorSteps_.size()))return false;std::swap(macroEditorSteps_[index],macroEditorSteps_[target]);emit toolkitChanged();return true;}
bool TimelineController::removeMacroEditorStep(int index){if(index<0||index>=static_cast<int>(macroEditorSteps_.size()))return false;macroEditorSteps_.erase(macroEditorSteps_.begin()+index);emit toolkitChanged();return true;}
bool TimelineController::saveVisualMacro(const QString&name){if(name.trimmed().isEmpty()||macroEditorSteps_.empty())return false;saveYtpMacro({name.trimmed().toStdString(),macroEditorSteps_});emit toolkitChanged();return true;}
bool TimelineController::applyMacroScope(const QString&name,int scope,double probability,qulonglong seed){const auto macro=loadYtpMacro(name);const auto*seq=sequence();if(!macro||!seq)return false;std::vector<Id>ids;if(scope==0)ids=selectedItemIds();else for(const auto&track:seq->tracks)if(track.kind==TrackKind::Video)for(const auto&item:track.items){bool include=scope==1;if(scope==2)for(const auto&marker:seq->markers)if(marker.time>=item.timelineStart&&marker.time<item.timelineEnd()){include=true;break;}if(scope==3)if(const auto*media=projectController_->project().findMediaAsset(item.mediaAssetId))for(const auto&word:media->transcript)if(word.start<item.sourceRange.end()&&word.start+word.duration>item.sourceRange.start()){include=true;break;}if(include)ids.push_back(item.id);}std::mt19937_64 rng(seed);std::bernoulli_distribution keep(std::clamp(probability,0.0,1.0));std::erase_if(ids,[&](const auto&){return !keep(rng);});if(ids.empty())return false;ToolkitResult result;const bool ok=mutate("Apply conditional macro",[&](Sequence&s){result=YtpToolkit::applyMacro(s,ids,*macro);});if(ok){selectedIds_.clear();for(const auto&id:result.itemIds)selectedIds_.append(QString::fromStdString(id));emit selectionChanged();}return ok;}
bool TimelineController::previewMacroVariations(const QString&name,int count,qulonglong seed){const auto macro=loadYtpMacro(name);const auto*seq=sequence();if(!macro||!seq||selectedIds_.isEmpty())return false;macroVariationPreviews_.clear();std::mt19937_64 rng(seed);for(int i=0;i<std::clamp(count,1,12);++i){auto copy=*seq;auto ids=selectedItemIds();std::shuffle(ids.begin(),ids.end(),rng);try{const auto result=YtpToolkit::applyMacro(copy,ids,*macro);macroVariationPreviews_.append(QString("Variation %1: %2 events, %3 ms").arg(i+1).arg(result.itemIds.size()).arg(milliseconds(result.duration)));}catch(...){macroVariationPreviews_.append(QString("Variation %1: incompatible selection").arg(i+1));}}emit toolkitChanged();return true;}

bool TimelineController::previewRandomizer(qulonglong seed,double reverse,double effects,double minSpeed,double maxSpeed,double minPitch,double maxPitch,bool shuffle){const auto* seq=sequence();if(!seq)return false;try{randomizerPlan_=YtpToolkit::previewRandomizer(*seq,selectedItemIds(),RandomizerOptions{seed,reverse,effects,minSpeed,maxSpeed,minPitch,maxPitch,shuffle});}catch(const std::exception&){randomizerPlan_.reset();emit toolkitChanged();return false;}emit toolkitChanged();return true;}
QVariantMap TimelineController::randomizerPreview() const {if(!randomizerPlan_)return {};QStringList changes;for(const auto& value:randomizerPlan_->changes)changes.append(QString::fromStdString(value));return {{"seed",QVariant::fromValue<qulonglong>(randomizerPlan_->seed)},{"changeCount",static_cast<int>(changes.size())},{"summary",changes.mid(0,12).join("\n")+(changes.size()>12?QStringLiteral("\n… and %1 more").arg(changes.size()-12):QString{})}};}
bool TimelineController::commitRandomizer(){if(!randomizerPlan_||!sequence()||!(*sequence()==randomizerPlan_->before))return false;const auto before=randomizerPlan_->before;const auto after=randomizerPlan_->after;const auto ids=randomizerPlan_->selectedIds;const bool ok=projectController_->applySequenceEdit(before,after,"Commit seeded YTP randomizer");if(ok){selectedIds_.clear();for(const auto& id:ids)selectedIds_.append(QString::fromStdString(id));randomizerPlan_.reset();emit selectionChanged();emit timelineChanged();emit toolkitChanged();}return ok;}
void TimelineController::cancelRandomizer(){if(!randomizerPlan_)return;randomizerPlan_.reset();emit toolkitChanged();}

QVariantList TimelineController::sequences() const {QVariantList result;for(const auto&s:projectController_->project().sequences())result.push_back(QVariantMap{{"id",QString::fromStdString(s.id)},{"name",QString::fromStdString(s.name)},{"durationMs",milliseconds(s.duration())}});return result;}

bool TimelineController::createSequence(const QString& name){auto clean=name.trimmed();if(clean.isEmpty())return false;auto before=projectController_->project().sequences();auto after=before;auto created=createDefaultSequence();created.name=clean.toStdString();const auto id=created.id;after.push_back(std::move(created));if(!projectController_->applySequencesEdit(std::move(before),std::move(after),"Create sequence"))return false;activeSequenceId_=QString::fromStdString(id);selectedIds_.clear();emit selectionChanged();emit timelineChanged();return true;}
bool TimelineController::removeSequence(const QString& id){auto before=projectController_->project().sequences();if(before.size()<=1)return false;for(const auto&s:before)for(const auto&t:s.tracks)for(const auto&i:t.items)if(i.nestedSequenceId==id.toStdString())return false;auto after=before;std::erase_if(after,[&](const auto&s){return s.id==id.toStdString();});if(after.size()==before.size())return false;if(!projectController_->applySequencesEdit(std::move(before),std::move(after),"Remove sequence"))return false;if(activeSequenceId_==id)activeSequenceId_=QString::fromStdString(projectController_->project().sequences().front().id);selectedIds_.clear();emit selectionChanged();emit timelineChanged();return true;}
bool TimelineController::switchSequence(const QString& id){if(!projectController_->project().findSequence(id.toStdString()))return false;activeSequenceId_=id;selectedIds_.clear();playheadMs_=0;programCacheStale_=true;invalidatePlaybackPreview();emit selectionChanged();emit playheadChanged();emit timelineChanged();return true;}
bool TimelineController::insertNestedSequence(const QString& childId,const QString& trackId,qint64 atMs){const auto* child=projectController_->project().findSequence(childId.toStdString());if(!child||child->id==activeSequenceId_.toStdString()||child->duration()<=Rational{})return false;Id inserted;const bool ok=mutate("Insert nested sequence",[&](Sequence&s){auto*t=s.findTrack(trackId.toStdString());if(!t||t->kind!=TrackKind::Video)throw std::invalid_argument("video track required");TimelineItem item{.id=createId(),.trackId=t->id,.timelineStart=seconds(std::max<qint64>(0,atMs)),.sourceRange=TimeRange{Rational{},child->duration()},.duration=child->duration()};item.nestedSequenceId=child->id;inserted=item.id;t->items.push_back(std::move(item));});if(ok){selectedIds_={QString::fromStdString(inserted)};emit selectionChanged();}return ok;}
bool TimelineController::createAdjustmentClip(const QString& trackId,qint64 atMs,qint64 durationMs){Id inserted;const bool ok=mutate("Create adjustment clip",[&](Sequence&s){auto*t=s.findTrack(trackId.toStdString());if(!t||t->kind!=TrackKind::Video||durationMs<=0)throw std::invalid_argument("video track and duration required");const auto duration=seconds(durationMs);TimelineItem item{.id=createId(),.trackId=t->id,.timelineStart=seconds(std::max<qint64>(0,atMs)),.sourceRange=TimeRange{Rational{},duration},.duration=duration};item.adjustmentClip=true;inserted=item.id;t->items.push_back(std::move(item));});if(ok){selectedIds_={QString::fromStdString(inserted)};emit selectionChanged();}return ok;}
bool TimelineController::addMask(int shape){if(selectedIds_.isEmpty())return false;return mutate("Add mask",[&](Sequence&s){auto*i=s.findItem(selectedIds_.front().toStdString());if(!i)throw std::invalid_argument("item missing");i->masks.push_back(MaskSettings{.id=createId(),.shape=static_cast<MaskShape>(std::clamp(shape,0,1))});});}
bool TimelineController::updateMask(const QString&id,double x,double y,double width,double height,double feather,double opacity,bool inverted){if(selectedIds_.isEmpty())return false;return mutate("Update mask",[&](Sequence&s){auto*i=s.findItem(selectedIds_.front().toStdString());if(!i)throw std::invalid_argument("item missing");auto found=std::find_if(i->masks.begin(),i->masks.end(),[&](const auto&m){return m.id==id.toStdString();});if(found==i->masks.end())throw std::invalid_argument("mask missing");found->x=x;found->y=y;found->width=width;found->height=height;found->feather=feather;found->opacity=opacity;found->inverted=inverted;});}
bool TimelineController::removeMask(const QString&id){if(selectedIds_.isEmpty())return false;return mutate("Remove mask",[&](Sequence&s){auto*i=s.findItem(selectedIds_.front().toStdString());if(!i)throw std::invalid_argument("item missing");const auto size=i->masks.size();std::erase_if(i->masks,[&](const auto&m){return m.id==id.toStdString();});if(size==i->masks.size())throw std::invalid_argument("mask missing");});}
bool TimelineController::detectBeats(){if(selectedIds_.isEmpty()||!sequence())return false;const auto* item=sequence()->findItem(selectedIds_.front().toStdString());if(!item)return false;const auto* media=projectController_->project().findMediaAsset(item->mediaAssetId);if(!media)return false;QString error;const auto onsets=MediaAnalysis::detectOnsets(QString::fromStdString(media->path),&error);if(onsets.empty())return false;return mutate("Detect beat/onset markers",[&](Sequence&s){for(const auto& source:onsets){if(source<item->sourceRange.start()||source>=item->sourceRange.end())continue;const auto local=(source-item->sourceRange.start())/Rational{static_cast<std::int64_t>(std::llround(item->speed*1000000)),1000000};const auto time=item->timelineStart+local;if(time>=item->timelineEnd())continue;s.markers.push_back({.id=createId(),.time=time,.label="Beat",.color="#00d4ff"});}});}
void TimelineController::setVisibleRange(qint64 startMs, qint64 endMs) {
    startMs = std::max<qint64>(0, startMs);
    endMs = std::max(startMs, endMs);
    if (startMs == visibleStartMs_ && endMs == visibleEndMs_) return;
    const auto modelWindow = [](qint64 start, qint64 end) {
        if (start < 0 || end < 0) return std::pair<qint64, qint64>{-1, -1};
        return std::pair<qint64, qint64>{
            (std::max<qint64>(0, start - 2'000) / 1'000) * 1'000,
            ((end + 2'999) / 1'000) * 1'000};
    };
    const auto previousWindow = modelWindow(visibleStartMs_, visibleEndMs_);
    visibleStartMs_ = startMs;
    visibleEndMs_ = endMs;
    if (modelWindow(visibleStartMs_, visibleEndMs_) != previousWindow) emit timelineChanged();
}

QVariantMap TimelineController::cacheStats() const {const auto info=MediaCache::cacheInfo();return{{"files",info.files},{"bytes",info.bytes},{"megabytes",static_cast<double>(info.bytes)/1024.0/1024.0}};}
QVariantList TimelineController::compoundClips() const {QVariantList result;for(const auto&clip:projectController_->project().compoundClips()){const auto*seq=projectController_->project().findSequence(clip.sequenceId);result.push_back(QVariantMap{{"id",QString::fromStdString(clip.id)},{"sequenceId",QString::fromStdString(clip.sequenceId)},{"name",QString::fromStdString(clip.name)},{"color",QString::fromStdString(clip.color)},{"durationMs",seq?milliseconds(seq->duration()):0},{"live",clip.updateAllInstances}});}return result;}
QVariantMap TimelineController::beatGrid() const {if(const auto*s=sequence())return{{"enabled",s->beatGrid.enabled},{"bpm",s->beatGrid.bpm},{"offsetMs",milliseconds(s->beatGrid.offset)},{"division",s->beatGrid.division}};return{};}

bool TimelineController::buildSentenceV2(const QVariantList&values,const QString&name,qint64 paddingMs,qint64 crossfadeMs,const QString&trackId,qint64 atMs){if(values.isEmpty())return false;std::vector<WordMatch> words;for(const auto&value:values){const auto map=value.toMap();const auto start=map.value("startMs").toLongLong(),end=map.value("endMs").toLongLong();if(end<=start)continue;words.push_back({.mediaAssetId=map.value("mediaId").toString().toStdString(),.range=TimeRange{seconds(start),seconds(end-start)},.text=map.value("text").toString().toStdString(),.score=map.value("score",1).toDouble()});}if(words.empty())return false;SentenceBuild build;try{build=RemixToolkit::sentenceSequence(projectController_->project(),words,name.trimmed().toStdString(),seconds(std::max<qint64>(0,paddingMs)),seconds(std::max<qint64>(0,crossfadeMs)));}catch(...){return false;}auto before=projectController_->project();auto after=before;auto sequences=after.sequences();const auto compoundId=createId();const auto childId=build.sequence.id;sequences.push_back(build.sequence);auto active=std::find_if(sequences.begin(),sequences.end(),[&](const auto&s){return s.id==activeSequenceId_.toStdString();});if(active==sequences.end())return false;auto*track=active->findTrack(trackId.toStdString());if(!track||track->kind!=TrackKind::Video)return false;TimelineItem nested{.id=createId(),.trackId=track->id,.timelineStart=seconds(std::max<qint64>(0,atMs)),.sourceRange=TimeRange{Rational{},build.sequence.duration()},.duration=build.sequence.duration()};nested.nestedSequenceId=childId;track->items.push_back(std::move(nested));try{after.setSequences(std::move(sequences));after.addCompoundClip({.id=compoundId,.sequenceId=childId,.name=name.trimmed().isEmpty()?"Sentence":name.trimmed().toStdString(),.color="#5aa9e6",.updateAllInstances=true,.createdAtMs=QDateTime::currentMSecsSinceEpoch()});}catch(...){return false;}return projectController_->applyProjectEdit(std::move(before),std::move(after),"Build Sentence Mixer v2 compound");}

bool TimelineController::createCompoundFromSelection(const QString&name){const auto*source=sequence();if(!source||selectedIds_.isEmpty()||name.trimmed().isEmpty())return false;std::vector<Id>ids=selectedItemIds();Rational origin=source->duration();for(const auto&id:ids)if(const auto*item=source->findItem(id))origin=std::min(origin,item->timelineStart);auto compound=*source;compound.id=createId();compound.name=name.trimmed().toStdString();compound.markers.clear();std::unordered_map<std::string,std::string> trackIds;for(auto&track:compound.tracks){const auto old=track.id;track.id=createId();trackIds[old]=track.id;std::erase_if(track.items,[&](const auto&i){return std::find(ids.begin(),ids.end(),i.id)==ids.end();});for(auto&item:track.items){item.id=createId();item.trackId=track.id;item.timelineStart-=origin;}}auto sourceEdited=*source;for(auto&track:sourceEdited.tracks)std::erase_if(track.items,[&](const auto&i){return std::find(ids.begin(),ids.end(),i.id)!=ids.end();});auto target=std::find_if(sourceEdited.tracks.begin(),sourceEdited.tracks.end(),[](const auto&t){return t.kind==TrackKind::Video&&!t.locked;});if(target==sourceEdited.tracks.end()||compound.duration()<=Rational{})return false;TimelineItem nested{.id=createId(),.trackId=target->id,.timelineStart=origin,.sourceRange=TimeRange{Rational{},compound.duration()},.duration=compound.duration()};nested.nestedSequenceId=compound.id;target->items.push_back(nested);auto before=projectController_->project();auto after=before;auto sequences=after.sequences();for(auto&seq:sequences)if(seq.id==sourceEdited.id)seq=sourceEdited;sequences.push_back(compound);after.setSequences(std::move(sequences));after.addCompoundClip({.id=createId(),.sequenceId=compound.id,.name=name.trimmed().toStdString(),.color="#8a6fd1",.updateAllInstances=true,.createdAtMs=QDateTime::currentMSecsSinceEpoch()});const bool ok=projectController_->applyProjectEdit(std::move(before),std::move(after),"Create compound clip");if(ok){selectedIds_={QString::fromStdString(nested.id)};emit selectionChanged();}return ok;}

bool TimelineController::insertCompound(const QString &compoundId,
                                        const QString &trackId, qint64 atMs,
                                        bool independent) {
  const auto *definition =
      projectController_->project().findCompoundClip(compoundId.toStdString());
  if (!definition)
    return false;
  auto before = projectController_->project();
  auto after = before;
  auto sequences = after.sequences();
  auto childId = definition->sequenceId;
  if (independent) {
    const auto *original = after.findSequence(childId);
    if (!original)
      return false;
    auto clone = *original;
    clone.id = createId();
    clone.name += " Copy";
    std::unordered_map<std::string, std::string> linkedGroups;
    std::unordered_map<std::string, std::string> groups;
    for (auto &track : clone.tracks) {
      track.id = createId();
      for (auto &item : track.items) {
        item.id = createId();
        item.trackId = track.id;
        if (!item.linkedGroupId.empty()) {
          const auto old = item.linkedGroupId;
          if (!linkedGroups.contains(old))
            linkedGroups.emplace(old, createId());
          item.linkedGroupId = linkedGroups.at(old);
        }
        if (!item.groupId.empty()) {
          const auto old = item.groupId;
          if (!groups.contains(old))
            groups.emplace(old, createId());
          item.groupId = groups.at(old);
        }
      }
    }
    for (auto &marker : clone.markers)
      marker.id = createId();
    childId = clone.id;
    sequences.push_back(clone);
    after.setSequences(sequences);
    after.addCompoundClip({.id = createId(),
                           .sequenceId = childId,
                           .name = definition->name + " Copy",
                           .color = definition->color,
                           .updateAllInstances = false,
                           .createdAtMs = QDateTime::currentMSecsSinceEpoch()});
    sequences = after.sequences();
  }
  auto active =
      std::find_if(sequences.begin(), sequences.end(), [&](const auto &s) {
        return s.id == activeSequenceId_.toStdString();
      });
  const auto *child = after.findSequence(childId);
  if (active == sequences.end() || !child)
    return false;
  auto *track = active->findTrack(trackId.toStdString());
  if (!track || track->kind != TrackKind::Video)
    return false;
  TimelineItem item{.id = createId(),
                    .trackId = track->id,
                    .timelineStart = seconds(std::max<qint64>(0, atMs)),
                    .sourceRange = TimeRange{Rational{}, child->duration()},
                    .duration = child->duration()};
  item.nestedSequenceId = childId;
  track->items.push_back(item);
  after.setSequences(std::move(sequences));
  return projectController_->applyProjectEdit(
      std::move(before), std::move(after),
      independent ? "Insert independent compound" : "Insert live compound");
}
bool TimelineController::removeCompound(const QString&id){const auto*definition=projectController_->project().findCompoundClip(id.toStdString());if(!definition)return false;for(const auto&seq:projectController_->project().sequences())for(const auto&track:seq.tracks)for(const auto&item:track.items)if(item.nestedSequenceId==definition->sequenceId)return false;auto before=projectController_->project();auto after=before;if(!after.removeCompoundClip(id.toStdString()))return false;return projectController_->applyProjectEdit(std::move(before),std::move(after),"Remove unused compound");}

bool TimelineController::estimateBeatGrid(int division){if(selectedIds_.isEmpty()||!sequence())return false;const auto*item=sequence()->findItem(selectedIds_.front().toStdString());const auto*media=item?projectController_->project().findMediaAsset(item->mediaAssetId):nullptr;if(!item||!media)return false;QString error;const auto onsets=MediaAnalysis::detectOnsets(QString::fromStdString(media->path),&error);if(onsets.size()<2)return false;BeatGrid grid;try{grid=RemixToolkit::estimateBeatGrid(onsets,division);grid.offset=item->timelineStart+(grid.offset-item->sourceRange.start())/Rational{static_cast<std::int64_t>(std::llround(item->speed*1000000)),1000000};}catch(...){return false;}return mutate("Estimate beat grid",[&](Sequence&s){s.beatGrid=grid;});}
bool TimelineController::configureBeatGrid(double bpm,qint64 offsetMs,int division,bool enabled){return mutate("Configure beat grid",[&](Sequence&s){s.beatGrid={.enabled=enabled,.bpm=bpm,.offset=seconds(std::max<qint64>(0,offsetMs)),.division=division};if(s.validate())throw std::invalid_argument("invalid beat grid");});}
bool TimelineController::applyBeatTool(int mode,const QString&effectType,const QString&parameter){if(selectedIds_.isEmpty())return false;std::vector<Id>result=selectedItemIds();const bool ok=mutate("Beat-aware edit",[&](Sequence&s){if(mode==0)RemixToolkit::snapItemsToBeats(s,result);else if(mode==1)result=RemixToolkit::cutItemsToBeats(s,result);else if(mode==2)RemixToolkit::addAudioReactiveKeys(s,result,effectType.toStdString(),parameter.toStdString(),0,100);else throw std::invalid_argument("unknown beat tool");});if(ok){selectedIds_.clear();for(const auto&id:result)selectedIds_.append(QString::fromStdString(id));emit selectionChanged();}return ok;}
bool TimelineController::trackMask(const QString&maskId){if(selectedIds_.isEmpty()||!sequence())return false;const auto itemId=selectedIds_.front();const auto*item=sequence()->findItem(itemId.toStdString());const auto*media=item?projectController_->project().findMediaAsset(item->mediaAssetId):nullptr;if(!item||!media)return false;const auto found=std::find_if(item->masks.begin(),item->masks.end(),[&](const auto&m){return m.id==maskId.toStdString();});if(found==item->masks.end())return false;const auto mask=*found;const auto path=QString::fromStdString(media->path);const auto start=item->sourceRange.start(),duration=item->sourceRange.duration();const int width=media->width,height=media->height;const auto taskId=QString::fromStdString(createId());backgroundTasks_.push_back(QVariantMap{{"id",taskId},{"name","Motion tracking"},{"status","Running"},{"progress",0.0},{"canCancel",false}});emit backgroundTasksChanged();auto*watcher=new QFutureWatcher<std::pair<std::vector<TrackingPoint>,QString>>(this);connect(watcher,&QFutureWatcherBase::finished,this,[this,watcher,taskId,itemId,maskId]{auto result=watcher->result();watcher->deleteLater();bool applied=false;if(!result.first.empty())applied=mutate("Apply tracked mask",[&](Sequence&s){auto*i=s.findItem(itemId.toStdString());if(!i)throw std::invalid_argument("tracked item missing");auto m=std::find_if(i->masks.begin(),i->masks.end(),[&](const auto&value){return value.id==maskId.toStdString();});if(m==i->masks.end())throw std::invalid_argument("tracked mask missing");EffectParameter x{.name="x",.value=m->x,.minimum=0,.maximum=1},y{.name="y",.value=m->y,.minimum=0,.maximum=1};for(const auto&point:result.first){x.keyframes.push_back({.id=createId(),.time=point.time,.value=point.x,.interpolation=KeyframeInterpolation::Smooth});y.keyframes.push_back({.id=createId(),.time=point.time,.value=point.y,.interpolation=KeyframeInterpolation::Smooth});}std::erase_if(m->animation,[](const auto&p){return p.name=="x"||p.name=="y";});m->animation.push_back(std::move(x));m->animation.push_back(std::move(y));});for(auto&task:backgroundTasks_){auto map=task.toMap();if(map.value("id").toString()==taskId){map["status"]=applied?"Complete":"Failed: "+result.second;map["progress"]=applied?1.0:0.0;task=map;}}emit backgroundTasksChanged();});watcher->setFuture(QtConcurrent::run([path,start,duration,width,height,mask]{QString error;auto points=MediaAnalysis::trackRegion(path,start,duration,width,height,mask.x,mask.y,mask.width,mask.height,&error);return std::make_pair(std::move(points),error);}));return true;}
bool TimelineController::renderContinuousProgramCache(){const auto*current=sequence();if(!current||current->duration()<=Rational{}||previewRendering_)return false;if(programCancellation_)programCancellation_->store(true);++playbackRequestGeneration_;const auto id=QString::fromStdString(createId());programTaskId_=id;programCancellation_=std::make_shared<std::atomic_bool>(false);const auto cancellation=programCancellation_;const auto project=projectController_->project();const auto sequenceCopy=*current;const auto output=MediaCache::sequencePreviewPath(QString::fromStdString(current->id)+QStringLiteral("-")+id);const auto previewDuration=milliseconds(current->duration());int width=1280,height=720;if(current->previewQuality==PreviewQuality::Quarter){width=480;height=270;}else if(current->previewQuality==PreviewQuality::Half||current->previewQuality==PreviewQuality::Automatic){width=960;height=540;}previewRendering_=true;playbackPreviewUrl_=QUrl{};playbackPreviewStartMs_=0;playbackPreviewDurationMs_=previewDuration;programCacheProgress_=0;backgroundTasks_.push_back(QVariantMap{{"id",id},{"name","Program playback cache"},{"status","Running"},{"progress",0.0},{"canCancel",true}});emit backgroundTasksChanged();emit playbackPreviewChanged();auto*watcher=new QFutureWatcher<RenderResult>(this);connect(watcher,&QFutureWatcher<RenderResult>::finished,this,[this,watcher,id,output,cancellation,previewDuration]{const auto result=watcher->result();watcher->deleteLater();for(auto&task:backgroundTasks_){auto map=task.toMap();if(map.value("id").toString()==id){map["status"]=result.success?"Complete":result.cancelled?"Cancelled":"Failed: "+result.error;map["progress"]=result.success?1.0:map.value("progress");map["canCancel"]=false;task=map;}}if(id==programTaskId_){previewRendering_=false;if(result.success){programCacheStale_=false;playbackPreviewUrl_=QUrl::fromLocalFile(output);++previewGeneration_;playbackPreviewDurationMs_=previewDuration;programCacheProgress_=1;}else{playbackPreviewDurationMs_=0;if(!result.cancelled)projectController_->reportPlaybackError(result.error);}programCancellation_.reset();}else QFile::remove(output);emit backgroundTasksChanged();emit playbackPreviewChanged();});watcher->setFuture(QtConcurrent::run([this,project,sequenceCopy,output,width,height,cancellation,id]{return RenderEngine::renderPreviewCache(project,sequenceCopy,output,width,height,*cancellation,[this,id](double progress,const QString&){QMetaObject::invokeMethod(this,[this,id,progress]{if(id==programTaskId_)programCacheProgress_=progress;for(auto&task:backgroundTasks_){auto map=task.toMap();if(map.value("id").toString()==id){map["progress"]=progress;task=map;}}emit backgroundTasksChanged();emit playbackPreviewChanged();},Qt::QueuedConnection);});}));return true;}
void TimelineController::cancelBackgroundTask(const QString&id){if(id==programTaskId_&&programCancellation_)programCancellation_->store(true);}
bool TimelineController::setCaption(bool enabled,const QString&text,double size,const QString&color){if(selectedIds_.isEmpty())return false;return mutate("Edit caption",[&](Sequence&s){auto*item=s.findItem(selectedIds_.front().toStdString());if(!item)throw std::invalid_argument("caption item missing");item->captionEnabled=enabled;item->captionText=text.trimmed().toStdString();item->captionSize=std::clamp(size,12.0,200.0);item->captionColor=color.trimmed().isEmpty()?"white":color.trimmed().toStdString();});}
bool TimelineController::applyTrackedMotion(const QString&maskId,int mode){if(selectedIds_.isEmpty()||!sequence()||(mode!=0&&mode!=1))return false;return mutate(mode==0?"Attach clip to tracked motion":"Stabilize with tracked motion",[&](Sequence&s){auto*item=s.findItem(selectedIds_.front().toStdString());if(!item)throw std::invalid_argument("tracked item missing");const auto mask=std::find_if(item->masks.begin(),item->masks.end(),[&](const auto&m){return m.id==maskId.toStdString();});if(mask==item->masks.end())throw std::invalid_argument("tracked mask missing");const auto x=std::find_if(mask->animation.begin(),mask->animation.end(),[](const auto&p){return p.name=="x";});const auto y=std::find_if(mask->animation.begin(),mask->animation.end(),[](const auto&p){return p.name=="y";});if(x==mask->animation.end()||y==mask->animation.end()||x->keyframes.empty()||y->keyframes.empty())throw std::invalid_argument("track the mask first");const double sign=mode==0?1.0:-1.0;const double baseX=x->keyframes.front().value,baseY=y->keyframes.front().value;EffectParameter px{.name="positionX",.value=item->transform.positionX,.minimum=-100000,.maximum=100000},py{.name="positionY",.value=item->transform.positionY,.minimum=-100000,.maximum=100000};for(const auto&key:x->keyframes)px.keyframes.push_back({.id=createId(),.time=key.time,.value=item->transform.positionX+sign*(key.value-baseX)*projectController_->project().settings().width,.interpolation=KeyframeInterpolation::Smooth});for(const auto&key:y->keyframes)py.keyframes.push_back({.id=createId(),.time=key.time,.value=item->transform.positionY+sign*(key.value-baseY)*projectController_->project().settings().height,.interpolation=KeyframeInterpolation::Smooth});std::erase_if(item->transform.animation,[](const auto&p){return p.name=="positionX"||p.name=="positionY";});item->transform.animation.push_back(std::move(px));item->transform.animation.push_back(std::move(py));});}
bool TimelineController::clearMediaCache(){invalidatePlaybackPreview();QString error;const bool ok=MediaCache::clearGenerated(&error);if(ok){programCacheStale_=true;backgroundTasks_.clear();emit backgroundTasksChanged();}return ok;}

} // namespace ytp
