#include "timeline/timeline_editor.h"
#include "persistence/project_serializer.h"
#include "media/media_cache.h"
#include "media/native_media_player.h"
#include "ui/export_controller.h"
#include "ui/project_controller.h"
#include "ui/timeline_controller.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <qqml.h>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>

namespace {
int failures = 0;
QStringList bindingErrors;

void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& message) {
    if (type < QtWarningMsg) return;
    static const QStringList serious{"ReferenceError", "TypeError", "is not a function",
                                     "Cannot assign", "Unable to assign", "Cannot read property",
                                     "Binding loop"};
    for (const auto& token : serious) if (message.contains(token)) {
        bindingErrors.push_back(message);
        break;
    }
}

void check(const bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void sendMouse(QQuickWindow* window, const QEvent::Type type, const QPointF position,
               const Qt::MouseButton button, const Qt::MouseButtons buttons) {
    QMouseEvent event(type, position, window->mapToGlobal(position.toPoint()),
                      button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &event);
    QCoreApplication::processEvents();
}

void sendKey(QQuickWindow* window,const int key,const QString& text={}){
    QKeyEvent press(QEvent::KeyPress,key,Qt::NoModifier,text);QCoreApplication::sendEvent(window,&press);
    QKeyEvent release(QEvent::KeyRelease,key,Qt::NoModifier,text);QCoreApplication::sendEvent(window,&release);
    QCoreApplication::processEvents();
}

bool waitUntil(const std::function<bool()>& condition,int timeoutMs=20'000){QElapsedTimer timer;timer.start();while(!condition()&&timer.elapsed()<timeoutMs){QCoreApplication::processEvents(QEventLoop::AllEvents,25);QThread::msleep(10);}return condition();}

QQuickItem* findVisualItem(QQuickItem* parent, const std::function<bool(QQuickItem*)>& predicate) {
    if (!parent) return nullptr;
    if (predicate(parent)) return parent;
    for (auto* child : parent->childItems())
        if (auto* match = findVisualItem(child, predicate)) return match;
    return nullptr;
}

int countVisualItems(QQuickItem* parent, const std::function<bool(QQuickItem*)>& predicate) {
    if (!parent) return 0;
    int count = predicate(parent) ? 1 : 0;
    for (auto* child : parent->childItems()) count += countVisualItems(child, predicate);
    return count;
}

bool createFixture(const QString&path){QProcess process;process.start(QStringLiteral("C:/msys64/ucrt64/bin/ffmpeg.exe"),{"-hide_banner","-loglevel","error","-y","-f","lavfi","-i","testsrc2=size=320x180:rate=30:duration=14","-f","lavfi","-i","sine=frequency=440:sample_rate=48000:duration=14","-c:v","libx264","-preset","ultrafast","-pix_fmt","yuv420p","-c:a","aac","-shortest",path});return process.waitForStarted(5'000)&&process.waitForFinished(30'000)&&process.exitCode()==0;}
}

int main(int argc, char** argv) {
    qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Basic"));
    QGuiApplication application(argc, argv);
    qmlRegisterType<ytp::NativeMediaPlayer>("YTPEditor.Native", 1, 0, "NativeMediaPlayer");
    QTemporaryDir settingsDirectory;
    check(settingsDirectory.isValid(), "temporary settings directory is available");
    qputenv("YTP_CACHE_DIR",settingsDirectory.filePath("cache").toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    const auto mediaPath=settingsDirectory.filePath("ui-fixture.mp4");
    check(createFixture(mediaPath),"QML playback fixture is created");
    QFile mainQml(QStringLiteral(YTP_QML_MAIN_PATH));
    check(mainQml.open(QIODevice::ReadOnly),"Main QML source is readable for playback-route checks");
    const auto mainQmlSource=mainQml.readAll();
    check(!mainQmlSource.contains("startLivePreview("),
          "the UI Play path cannot invoke the FFmpeg live-preview process");

    ytp::ProjectController projectController;
    auto seeded = projectController.project();
    ytp::MediaAsset media{.id=ytp::createId(), .path=mediaPath.toStdString(),
        .displayName="UI Fixture", .duration=ytp::Rational{14,1},
        .frameRateNumerator=30, .frameRateDenominator=1,
        .width=640, .height=360, .audioSampleRate=48'000};
    seeded.addMediaAsset(media);
    ytp::LibraryClip clip{.id=ytp::createId(), .mediaAssetId=media.id,
        .sourceRange=ytp::TimeRange{ytp::Rational{}, ytp::Rational{6,1}},
        .name="UI Fixture", .thumbnailTime=ytp::Rational{3,1}};
    const auto clipId = clip.id;
    seeded.addLibraryClip(std::move(clip));
    auto sequence = seeded.sequences().front();
    const auto inserted = ytp::TimelineEditor::insertLibraryClip(seeded, sequence, clipId,
        sequence.tracks[1].id, ytp::Rational{}, ytp::EditMode::Overwrite);
    QString videoItemId;
    QString audioItemId;
    QString videoTrackId;
    for (const auto& track : sequence.tracks) {
        for (const auto& item : track.items) {
            if (std::find(inserted.itemIds.begin(), inserted.itemIds.end(), item.id) != inserted.itemIds.end()) {
                if (track.kind == ytp::TrackKind::Video) {
                    videoItemId = QString::fromStdString(item.id);
                    videoTrackId = QString::fromStdString(track.id);
                } else if (track.kind == ytp::TrackKind::Audio) {
                    audioItemId = QString::fromStdString(item.id);
                }
            }
        }
    }
    seeded.updateSequence(std::move(sequence));
    const auto projectPath=settingsDirectory.filePath("ui-project.ytp.json");QString saveError;
    check(ytp::ProjectSerializer::save(seeded,projectPath,&saveError),"QML fixture project saves");
    check(projectController.openProject(QUrl::fromLocalFile(projectPath)),"QML fixture project opens");
    projectController.dismissFirstRunTutorial();

    ytp::TimelineController timelineController(&projectController);
    ytp::ExportController exportController(&projectController, &timelineController);
    const auto previousHandler = qInstallMessageHandler(messageHandler);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("projectController"), &projectController);
    engine.rootContext()->setContextProperty(QStringLiteral("timelineController"), &timelineController);
    engine.rootContext()->setContextProperty(QStringLiteral("exportController"), &exportController);
    engine.load(QUrl::fromLocalFile(QStringLiteral(YTP_QML_MAIN_PATH)));
    check(!engine.rootObjects().isEmpty(), "Main.qml loads for interaction testing");
    if (engine.rootObjects().isEmpty()) {
        for (const auto& error : bindingErrors)
            std::cerr << "QML: " << error.toStdString() << '\n';
        return 1;
    }
    auto* rootObject=engine.rootObjects().front();
    const auto previousInspectorContent=rootObject->property("inspectorContentIndex");
    auto* inspectorTabsForYtp=rootObject->findChild<QObject*>(QStringLiteral("inspectorTabs"));
    const auto previousInspectorTab=inspectorTabsForYtp?inspectorTabsForYtp->property("currentIndex"):QVariant{};
    rootObject->setProperty("inspectorContentIndex",3);
    if(inspectorTabsForYtp)inspectorTabsForYtp->setProperty("currentIndex",2);
    QCoreApplication::processEvents();
    auto* ytpToolbox=rootObject->findChild<QQuickItem*>(QStringLiteral("ytpToolbox"));
    auto* ytpSearch=rootObject->findChild<QQuickItem*>(QStringLiteral("ytpSearchField"));
    auto* ytpQuickHeader=rootObject->findChild<QQuickItem*>(QStringLiteral("ytpQuickHeader"));
    auto* ytpWindow=qobject_cast<QQuickWindow*>(rootObject);
    const auto ytpRowCount=ytpWindow?countVisualItems(ytpWindow->contentItem(),[](const auto*item){return item->objectName()==QStringLiteral("ytpToolRow");}):0;
    const auto ytpPackCount=ytpWindow?countVisualItems(ytpWindow->contentItem(),[](const auto*item){return item->objectName()==QStringLiteral("ytpPackRow");}):0;
    auto* representativeYtpRow=ytpWindow?findVisualItem(ytpWindow->contentItem(),[](const auto*item){return item->objectName()==QStringLiteral("ytpToolRow");}):nullptr;
    auto* representativeYtpPack=ytpWindow?findVisualItem(ytpWindow->contentItem(),[](const auto*item){return item->objectName()==QStringLiteral("ytpPackRow");}):nullptr;
    check(ytpToolbox&&ytpSearch&&ytpQuickHeader,"dense YTP toolbox, search, and compact category header load");
    check(ytpSearch&&ytpSearch->height()>=28&&ytpSearch->height()<=32,"YTP search field stays compact");
    check(ytpRowCount>=16&&representativeYtpRow&&representativeYtpRow->implicitHeight()>=30&&representativeYtpRow->implicitHeight()<=34,
          "YTP commands use dense 30-34 px rows instead of cards");
    check(ytpPackCount>=18&&representativeYtpPack&&representativeYtpPack->implicitHeight()>=44&&representativeYtpPack->implicitHeight()<=56,
          "all visual FX packs use readable single-row presentation");
    if(ytpQuickHeader){const auto expanded=ytpQuickHeader->property("expanded").toBool();ytpQuickHeader->setProperty("expanded",!expanded);check(ytpQuickHeader->property("expanded").toBool()!=expanded,"YTP categories are collapsible");ytpQuickHeader->setProperty("expanded",expanded);}
    if(ytpSearch&&ytpWindow){ytpSearch->setProperty("text",QStringLiteral("threshold vision"));QCoreApplication::processEvents();const auto visibleMatches=countVisualItems(ytpWindow->contentItem(),[](const auto*item){return item->objectName()==QStringLiteral("ytpPackRow")&&item->isVisible();});check(visibleMatches==1,"YTP search filters the toolbox to its matching FX pack");ytpSearch->setProperty("text",QString{});QCoreApplication::processEvents();}
    rootObject->setProperty("inspectorContentIndex",previousInspectorContent);
    if(inspectorTabsForYtp)inspectorTabsForYtp->setProperty("currentIndex",previousInspectorTab);
    QCoreApplication::processEvents();

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    auto* timeline = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("timelineFlick"));
    auto* tabs = engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("viewerTabs"));
    auto* playhead = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("timelinePlayhead"));
    auto* ruler = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("timelineRuler"));
    auto* marquee = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("timelineMarquee"));
    auto* programPlayer=engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("programPlayer"));
    auto* instantPlayer=engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("instantProgramPlayer"));
    auto* instantVideo=engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("instantProgramVideoOutput"));
    auto* zoomSlider=engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("timelineZoomSlider"));
    auto* zoomIn=engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("timelineZoomInButton"));
    auto* zoomOut=engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("timelineZoomOutButton"));
    auto* zoomFit=engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("timelineZoomFitButton"));
    auto* middlePan=engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("timelineMiddlePan"));
    auto* horizontalScroll=engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("timelineHorizontalScrollBar"));
    auto* commandPalette=engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("commandPalette"));
    auto* nleTopBar=engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("nleTopBar"));
    auto* statusStrip=engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("statusStrip"));
    check(window && timeline && tabs && playhead && ruler && marquee && programPlayer && instantPlayer && instantVideo, "timeline interaction targets exist");
    rootObject->setProperty("markInMs",500);
    rootObject->setProperty("markOutMs",1'500);
    projectController.sourceWaveformChanged();
    QCoreApplication::processEvents();
    check(rootObject->property("markInMs").toLongLong()==500&&rootObject->property("markOutMs").toLongLong()==1'500,
          "background source-analysis refreshes preserve the user's marked In/Out range");
    check(zoomSlider && zoomIn && zoomOut && zoomFit, "timeline exposes detailed zoom controls and fit-to-window");
    check(middlePan, "timeline exposes middle-button horizontal panning");
    check(horizontalScroll && horizontalScroll->property("interactive").toBool(),
          "timeline exposes an interactive persistent horizontal scrollbar");
    check(commandPalette && nleTopBar && statusStrip,
          "the refactored shell exposes its top bar, command palette, and compact status strip components");
    if (!window || !timeline || !tabs || !playhead || !ruler || !marquee || !programPlayer || !instantPlayer || !instantVideo) return 1;
    window->setWidth(1280);
    window->setHeight(720);
    window->show();
    window->requestActivate();
    QCoreApplication::processEvents();
    check(horizontalScroll && horizontalScroll->isVisible(),
          "horizontal timeline scrollbar remains visible in the editor workspace");
    auto* mediaPanel = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("mediaPanel"));
    auto* inspectorPanel = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("inspectorPanel"));
    auto* timelinePanel = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("timelinePanel"));
    auto* programMonitor = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("programMonitor"));
    check(mediaPanel && inspectorPanel && timelinePanel && mediaPanel->width() >= 220 && mediaPanel->width() <= 340 &&
              inspectorPanel->width() >= 260 && inspectorPanel->width() <= 380 &&
              timelinePanel->height() >= window->height() * .35,
          "the default NLE layout uses bounded sidebars and a timeline-dominant lower workspace");
    const auto viewerWidthBeforeCollapse = programMonitor ? programMonitor->width() : 0.0;
    QMetaObject::invokeMethod(nleTopBar, "toggleLibraryRequested");
    waitUntil([&]{return mediaPanel && mediaPanel->isVisible() && mediaPanel->width() >= 220;},2'000);
    check(mediaPanel && !mediaPanel->isVisible() && programMonitor && programMonitor->width() > viewerWidthBeforeCollapse,
          "collapsing the media panel gives its space to the viewer");
    QMetaObject::invokeMethod(nleTopBar, "toggleLibraryRequested");
    QCoreApplication::processEvents();
    auto* libraryTabs = engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("libraryTabs"));
    if (libraryTabs) libraryTabs->setProperty("currentIndex", 1);
    QCoreApplication::processEvents();
    QQuickItem* libraryCard = nullptr;
    QQuickItem* timelineDropArea = nullptr;
    waitUntil([&] {
        libraryCard = findVisualItem(window->contentItem(), [&](QQuickItem* item) {
            return item->objectName() == QStringLiteral("libraryClip_") + QString::fromStdString(clipId) &&
                   item->isVisible() && item->width() >= 150 && item->mapToScene(QPointF{}).y() > 90;
        });
        timelineDropArea = findVisualItem(window->contentItem(), [&](QQuickItem* item) {
            return item->objectName() == QStringLiteral("timelineDropArea_") + videoTrackId;
        });
        return libraryCard && timelineDropArea;
    }, 2'000);
    if(!libraryTabs||!libraryCard||!timelineDropArea)std::cerr<<"drag targets diag tabs="<<bool(libraryTabs)
        <<" card="<<bool(libraryCard)<<" drop="<<bool(timelineDropArea)
        <<" tabIndex="<<(libraryTabs?libraryTabs->property("currentIndex").toInt():-1)
        <<" videoTrack="<<videoTrackId.toStdString()<<'\n';
    check(libraryTabs && libraryCard && timelineDropArea,
          "the reusable-clips page exposes a draggable card and keyed video-track drop target");
    if (libraryCard && timelineDropArea) {
        const auto itemCountBeforeDrop = timelineController.items().size();
        const auto dragStart = libraryCard->mapToScene(QPointF(libraryCard->width() / 2,
                                                               libraryCard->height() / 2));
        const auto dropOrigin = timelineDropArea->mapToScene(QPointF(0, 0));
        const auto visibleDropX = std::max(20.0, std::min(timelineDropArea->width() - 30,
                                                          window->width() - dropOrigin.x() - 30));
        const auto dropPoint = timelineDropArea->mapToScene(
            QPointF(visibleDropX, timelineDropArea->height() / 2));
        sendMouse(window, QEvent::MouseButtonPress, dragStart, Qt::LeftButton, Qt::LeftButton);
        sendMouse(window, QEvent::MouseMove, dragStart + (dropPoint - dragStart) * .35,
                  Qt::NoButton, Qt::LeftButton);
        sendMouse(window, QEvent::MouseMove, dragStart + (dropPoint - dragStart) * .7,
                  Qt::NoButton, Qt::LeftButton);
        auto* dragProxy = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("libraryClipDragProxy"));
        check(rootObject->property("libraryClipDragActive").toBool() && dragProxy &&
                  dragProxy->isVisible() && dragProxy->z() >= 10000,
              "a library drag escapes the clipped media grid and renders above workspace panels");
        sendMouse(window, QEvent::MouseMove, dropPoint, Qt::NoButton, Qt::LeftButton);
        if(!timelineDropArea->property("containsDrag").toBool())std::cerr<<"drop hover diag targetScene="
            <<dropPoint.x()<<','<<dropPoint.y()<<" proxyScene="
            <<(dragProxy?dragProxy->mapToScene(QPointF(dragProxy->width()/2,dragProxy->height()/2)).x():-1)<<','
            <<(dragProxy?dragProxy->mapToScene(QPointF(dragProxy->width()/2,dragProxy->height()/2)).y():-1)
            <<" targetSize="<<timelineDropArea->width()<<'x'<<timelineDropArea->height()<<'\n';
        check(timelineDropArea->property("containsDrag").toBool(),
              "the keyed timeline target accepts the active library drag before release");
        sendMouse(window, QEvent::MouseButtonRelease, dropPoint, Qt::LeftButton, Qt::NoButton);
        const bool clipDropped = waitUntil([&] { return timelineController.items().size() >= itemCountBeforeDrop + 2; }, 2'000);
        if(!clipDropped)std::cerr<<"drop result diag expected="<<clipId
            <<" before="<<itemCountBeforeDrop<<" after="<<timelineController.items().size()<<'\n';
        check(clipDropped,
              "dropping a reusable clip onto the timeline inserts its linked video and audio ranges");
        if (clipDropped) {
            projectController.undo();
            QCoreApplication::processEvents();
        }
    }
    if (libraryTabs) libraryTabs->setProperty("currentIndex", 0);
    QCoreApplication::processEvents();
    QMetaObject::invokeMethod(commandPalette, "openWithQuery", Q_ARG(QVariant, QVariant{QStringLiteral("ytp")}));
    QCoreApplication::processEvents();
    check(commandPalette->property("visible").toBool(), "the command palette opens as a global editor surface");
    QMetaObject::invokeMethod(commandPalette, "close");
    QCoreApplication::processEvents();
    auto* previewOptions = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("programPreviewOptions"));
    auto* sourceWaveform = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("sourceWaveformStrip"));
    check(!tabs->property("visible").toBool() && previewOptions && !previewOptions->isVisible() &&
              sourceWaveform && !sourceWaveform->isVisible() && programMonitor && programMonitor->height() > 0,
          "Program is the chrome-free default while advanced preview controls remain hidden");
    tabs->setProperty("currentIndex", 0);
    QCoreApplication::processEvents();
    check(tabs->property("visible").toBool() && sourceWaveform->isVisible(),
          "opening Source reveals contextual monitor modes and source controls");
    tabs->setProperty("currentIndex", 1);
    QCoreApplication::processEvents();
    const bool thumbnailReady=waitUntil([&]{const auto items=timelineController.items();return !items.isEmpty()&&!items.front().toMap().value("thumbnailUrl").toUrl().isEmpty();});
    if(!thumbnailReady)std::cerr<<"Thumbnail path: "<<ytp::MediaCache::thumbnailPath(QString::fromStdString(clipId)).toStdString()<<" exists="<<QFileInfo::exists(ytp::MediaCache::thumbnailPath(QString::fromStdString(clipId)))<<" status="<<projectController.statusMessage().toStdString()<<'\n';
    check(thumbnailReady,"missing timeline thumbnails are generated and published to the timeline model");
    const bool waveformReady=waitUntil([&]{
        const auto items=timelineController.items();
        return std::any_of(items.cbegin(),items.cend(),[](const QVariant& value){
            const auto item=value.toMap();
            return item.value("kind").toInt()==1&&!item.value("waveformUrl").toUrl().isEmpty();
        });
    });
    check(waveformReady,"audio waveform generation is published to timeline clips");
    QCoreApplication::processEvents();
    check(countVisualItems(window->contentItem(), [](QQuickItem* item) {
              return item->objectName() == QStringLiteral("clipMediaPreview");
          }) == timelineController.items().size(),
          "each visible timeline event uses one media-preview node");
    check(countVisualItems(window->contentItem(), [](QQuickItem* item) {
              return item->objectName() == QStringLiteral("timelineFilmstrip");
          }) >= 1, "video events use discrete source-relative filmstrip cells instead of a squashed strip");
    auto* filmstrip = findVisualItem(window->contentItem(), [](QQuickItem* item) {
        return item->objectName() == QStringLiteral("timelineFilmstrip") && item->isVisible();
    });
    check(filmstrip && std::abs(filmstrip->property("cellWidth").toDouble() - 72.0) < .1 &&
              std::abs(filmstrip->property("cellHeight").toDouble() - 40.0) < .1,
          "timeline thumbnails retain fixed 72 by 40 pixel visual cells");
    if (filmstrip) {
        const auto sourceAt = [filmstrip](const int frameIndex) {
            QVariant result;
            QMetaObject::invokeMethod(filmstrip, "sourceTimeForCell", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, QVariant{frameIndex}));
            return result.toDouble();
        };
        filmstrip->setProperty("sourceStartMs", 40'000.0);
        filmstrip->setProperty("sourceEndMs", 60'000.0);
        filmstrip->setProperty("pixelsPerSecond", 100.0);
        filmstrip->setProperty("playbackRate", 1.0);
        filmstrip->setProperty("reverse", false);
        filmstrip->setProperty("freeze", false);
        check(std::abs(sourceAt(5) - 43'600.0) < 1.0,
              "thumbnail X maps through timeline-local time to the exact source timestamp");
        filmstrip->setProperty("playbackRate", 2.0);
        check(std::abs(sourceAt(5) - 47'200.0) < 1.0,
              "speed-adjusted thumbnail mapping advances source time at the playback rate");
        filmstrip->setProperty("sourceStartMs", 48'000.0);
        check(std::abs(sourceAt(0) - 48'000.0) < 1.0,
              "left trimming immediately makes the first thumbnail use the new source in-point");
        filmstrip->setProperty("playbackRate", 1.0);
        filmstrip->setProperty("sourceEndMs", 50'000.0);
        check(sourceAt(5) < 50'000.0 && sourceAt(5) >= 49'998.0,
              "right trimming clamps thumbnails before the new source out-point");
        filmstrip->setProperty("sourceStartMs", 40'000.0);
        filmstrip->setProperty("sourceEndMs", 60'000.0);
        filmstrip->setProperty("pixelsPerSecond", 200.0);
        check(std::abs(sourceAt(5) - 41'800.0) < 1.0 &&
                  std::abs(filmstrip->property("cellWidth").toDouble() - 72.0) < .1,
              "timeline zoom changes timestamp sampling without stretching thumbnail cells");
    }

    auto* timelineToolbar = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("timelineToolbar"));
    check(timelineToolbar && timelineToolbar->height() <= 36,
          "timeline tools use a compact desktop-editor toolbar");
    check(countVisualItems(window->contentItem(), [](QQuickItem* item) {
              return item->objectName() == QStringLiteral("trackLockButton");
          }) >= 2 && countVisualItems(window->contentItem(), [](QQuickItem* item) {
              return item->objectName() == QStringLiteral("trackVisibilityButton");
          }) >= 1, "track headers expose recognizable lock and video visibility controls");

    auto* inspectorTabs = engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("inspectorTabs"));
    check(inspectorTabs, "Inspector, Effects, and YTP tabs exist");
    check(inspectorTabs && inspectorTabs->property("height").toDouble() <= 32,
          "Inspector navigation stays within the 28 to 32 pixel density target");
    if (inspectorTabs) {
        inspectorTabs->setProperty("currentIndex", 1);
        QCoreApplication::processEvents();
        auto* effectsBrowser = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("effectsBrowser"));
        auto* effectsSearch = engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("effectsSearch"));
        const auto unfilteredEffects = countVisualItems(window->contentItem(), [](QQuickItem* item) {
            return item->objectName() == QStringLiteral("effectBrowserItem");
        });
        check(effectsBrowser && effectsBrowser->isVisible() && effectsSearch && unfilteredEffects >= 4,
              "Effects tab is a searchable categorized browser rather than an effect-property editor");
        effectsSearch->setProperty("text", QStringLiteral("blur"));
        QCoreApplication::processEvents();
        const auto filteredEffects = countVisualItems(window->contentItem(), [](QQuickItem* item) {
            return item->objectName() == QStringLiteral("effectBrowserItem");
        });
        check(filteredEffects >= 1 && filteredEffects < unfilteredEffects,
              "effects browser search narrows the available catalog");
        effectsSearch->setProperty("text", QString{});

        timelineController.select(videoItemId, false);
        check(timelineController.addEffectToSelection(QStringLiteral("brightness_contrast")),
              "effects browser action applies a compatible effect to the selected event");
        inspectorTabs->setProperty("currentIndex", 0);
        auto* editPicker = engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("editSection"));
        if (editPicker) editPicker->setProperty("currentIndex", 5);
        QCoreApplication::processEvents();
        auto* appliedStack = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("appliedEffectStack"));
        auto* appliedSection = findVisualItem(window->contentItem(), [](QQuickItem* item) {
            return item->objectName() == QStringLiteral("appliedEffectSection");
        });
        check(appliedStack && appliedStack->isVisible() && appliedSection &&
                  appliedSection->property("expanded").isValid(),
              "applied effects appear as a collapsible Inspector stack");
        const auto effects = timelineController.inspector().value(QStringLiteral("effects")).toList();
        if (!effects.isEmpty())
            timelineController.removeEffect(0, videoItemId,
                effects.front().toMap().value(QStringLiteral("id")).toString());
        if (editPicker) editPicker->setProperty("currentIndex", 0);
    }
    if (inspectorTabs) for (int index=0;index<3;++index) {
        inspectorTabs->setProperty("currentIndex",index);
        QCoreApplication::processEvents();
    }
    for (const auto& name : {"editSection","ytpSection","finishSection","remixSection"}) {
        if (auto* picker=engine.rootObjects().front()->findChild<QObject*>(QString::fromLatin1(name))) {
            const int count=picker->property("count").toInt();
            for(int index=0;index<count;++index){picker->setProperty("currentIndex",index);QCoreApplication::processEvents();}
        } else check(false,"inspector page selector exists");
    }
    for(const auto& error:bindingErrors)std::cerr<<"QML: "<<error.toStdString()<<'\n';
    check(bindingErrors.isEmpty(), "all inspector pages instantiate without QML binding/action errors");

    const auto trackHeaderWidth = engine.rootObjects().front()->property("trackHeaderWidth").toDouble();
    const QPointF rulerPoint = timeline->mapToScene(QPointF(trackHeaderWidth + 160, 17));
    sendMouse(window, QEvent::MouseButtonPress, rulerPoint, Qt::LeftButton, Qt::LeftButton);
    sendMouse(window, QEvent::MouseButtonRelease, rulerPoint, Qt::LeftButton, Qt::NoButton);
    check(std::abs(timelineController.playheadMs() - 2'000) <= 20,
          "clicking the timeline ruler seeks the playhead");
    check(tabs->property("currentIndex").toInt() == 1,
          "timeline seek switches the monitor to Program");

    const auto beforeDrag = timelineController.playheadMs();
    const QPointF handle = playhead->mapToScene(QPointF(1, 20));
    sendMouse(window, QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
    sendMouse(window, QEvent::MouseMove, handle + QPointF(80, 0), Qt::NoButton, Qt::LeftButton);
    sendMouse(window, QEvent::MouseButtonRelease, handle + QPointF(80, 0), Qt::LeftButton, Qt::NoButton);
    check(timelineController.playheadMs() >= beforeDrag + 950 &&
          timelineController.playheadMs() <= beforeDrag + 1'050,
          "dragging the playhead handle scrubs by the expected timeline distance");

    for (const auto& track : timelineController.tracks())
        timelineController.setTrackState(track.toMap().value("trackId").toString(), QStringLiteral("height"), 180);
    timeline->setProperty("contentY", 0.0);
    QCoreApplication::processEvents();
    const auto rulerSceneY = ruler->mapToScene(QPointF{}).y();
    const auto playheadSceneY = playhead->mapToScene(QPointF{}).y();
    timeline->setProperty("contentY", 80.0);
    QCoreApplication::processEvents();
    check(timeline->property("contentY").toDouble() > 0.0,
          "the timeline fixture is vertically scrollable");
    check(std::abs(ruler->mapToScene(QPointF{}).y() - rulerSceneY) < 2.0,
          "vertical scrolling keeps the timeline ruler visible");
    check(std::abs(playhead->mapToScene(QPointF{}).y() - playheadSceneY) < 2.0,
          "vertical scrolling keeps the playhead handle visible");
    check(!timeline->property("interactive").toBool(),
          "timeline background dragging is reserved for marquee selection instead of flicking");

    timelineController.setPlayheadMs(1'000);
    sendKey(window, Qt::Key_Right);
    const auto afterRightArrow = timelineController.playheadMs();
    check(afterRightArrow > 1'000 && afterRightArrow < 1'050,
          "Right Arrow scrubs one project frame forward");
    timelineController.setPlayheadMs(1'000);
    sendKey(window, Qt::Key_Left);
    check(timelineController.playheadMs() < 1'000 && timelineController.playheadMs() > 950,
          "Left Arrow scrubs one project frame backward");

    timeline->setProperty("contentX",160.0);
    timelineController.setPlayheadMs(4'000);
    QCoreApplication::processEvents();
    const auto timelineSceneX=timeline->mapToScene(QPointF{}).x();
    const auto playheadSceneX=playhead->mapToScene(QPointF{}).x();
    check(std::abs((playheadSceneX-timelineSceneX)-(trackHeaderWidth+4'000.0*timelineController.pixelsPerSecond()/1'000.0-160.0))<2.0,
          "horizontal scrolling keeps the playhead aligned to timeline content");
    const QPointF panStart=timeline->mapToScene(QPointF{timeline->width()*0.65,timeline->height()*0.5});
    const auto contentBeforePan=timeline->property("contentX").toDouble();
    sendMouse(window,QEvent::MouseButtonPress,panStart,Qt::MiddleButton,Qt::MiddleButton);
    sendMouse(window,QEvent::MouseMove,panStart-QPointF{90,0},Qt::NoButton,Qt::MiddleButton);
    sendMouse(window,QEvent::MouseButtonRelease,panStart-QPointF{90,0},Qt::MiddleButton,Qt::NoButton);
    check(timeline->property("contentX").toDouble()>contentBeforePan+70,
          "holding the mouse wheel and dragging pans the timeline horizontally");
    timelineController.setPixelsPerSecond(500);
    timeline->setProperty("contentX",0.0);
    engine.rootObjects().front()->setProperty("timelineFollowSuppressed",false);
    engine.rootObjects().front()->setProperty("programPlaybackRequested",true);
    timelineController.setPlayheadMs(3'000);
    QCoreApplication::processEvents();
    check(timeline->property("contentX").toDouble()>100,
          "timeline viewport automatically follows a playing playhead beyond its forward guide");
    engine.rootObjects().front()->setProperty("programPlaybackRequested",false);
    timelineController.setPixelsPerSecond(80);
    timeline->setProperty("contentX",0.0);
    check(timelineController.stepFrame(33,1)>33&&timelineController.stepFrame(34,-1)<34,
          "Program frame stepping always advances to a distinct project frame");

    tabs->setProperty("currentIndex",1);timelineController.setPlayheadMs(1'000);
    const auto firstInstantSeek=timelineController.instantPreview().value("sourcePositionMs").toLongLong();
    check(waitUntil([&]{return std::abs(instantPlayer->property("position").toLongLong()-firstInstantSeek)<=100;},10'000),
          "moving the playhead seeks the direct Program source without waiting for a render");
    timelineController.setPlayheadMs(1'500);
    const auto secondInstantSeek=timelineController.instantPreview().value("sourcePositionMs").toLongLong();
    check(waitUntil([&]{return std::abs(instantPlayer->property("position").toLongLong()-secondInstantSeek)<=100;}),
          "successive timeline seeks stay synchronized with the direct Program player");
    const auto beforeSpace=timelineController.playheadMs();QElapsedTimer directStartTimer;directStartTimer.start();sendKey(window,Qt::Key_Space," ");
    const bool directStarted=waitUntil([&]{return instantPlayer->property("playbackState").toInt()==1&&
                               timelineController.livePreviewUrl().isEmpty()&&
                               timelineController.playheadMs()>beforeSpace+80;},5'000);
    check(directStarted&&directStartTimer.elapsed()<2'500,
          "Space starts clean Program playback directly without an FFmpeg startup delay");
    const auto stableClockStart=timelineController.playheadMs();
    QElapsedTimer stableClockTimer;stableClockTimer.start();
    while(stableClockTimer.elapsed()<800){QCoreApplication::processEvents(QEventLoop::AllEvents,25);QThread::msleep(10);}
    const auto stableClockAdvance=timelineController.playheadMs()-stableClockStart;
    check(stableClockAdvance>=500&&stableClockAdvance<=1'200,
          "the Program clock remains near real time and cannot run away at 5x speed");
    sendKey(window,Qt::Key_Space," ");
    sendKey(window,Qt::Key_Space," ");
    check(engine.rootObjects().front()->property("programPlaybackRequested").toBool() &&
              waitUntil([&]{return instantPlayer->property("playbackState").toInt()==1;}),
          "rapid Space toggles preserve focus and the final requested Program playback state");
    timelineController.setPlayheadMs(4'000);
    const auto seekResumed=waitUntil([&]{return timelineController.playheadMs()>4'080&&
                               instantPlayer->property("playbackState").toInt()==1;},5'000);
    check(seekResumed,
          "timeline-driven seeks keep direct clean playback synchronized and running");
    sendKey(window,Qt::Key_Space," ");
    check(waitUntil([&]{return instantPlayer->property("playbackState").toInt()!=1&&programPlayer->property("playbackState").toInt()!=1;}),"Space stops timeline Program playback");
    auto*pausedProgramOutput=rootObject->findChild<QQuickItem*>(QStringLiteral("programVideoOutput"));
    check(instantVideo->isVisible()&&(!pausedProgramOutput||!pausedProgramOutput->isVisible()),
          "Pause preserves the direct Program frame without a stale rendered layer");
    const auto beforePausedDrag=timelineController.playheadMs();
    const auto pausedDragStart=playhead->mapToScene(QPointF(1,20));
    sendMouse(window,QEvent::MouseButtonPress,pausedDragStart,Qt::LeftButton,Qt::LeftButton);
    sendMouse(window,QEvent::MouseMove,pausedDragStart+QPointF(80,0),Qt::NoButton,Qt::LeftButton);
    sendMouse(window,QEvent::MouseButtonRelease,pausedDragStart+QPointF(80,0),Qt::LeftButton,Qt::NoButton);
    const auto pausedDragTarget=timelineController.playheadMs();
    check(pausedDragTarget>=beforePausedDrag+950&&pausedDragTarget<=beforePausedDrag+1'050&&
              timelineController.livePreviewUrl().isEmpty(),
          "dragging a paused playhead retires the stale sequential Program stream at the new time");
    const auto draggedInstantSeek=timelineController.instantPreview().value("sourcePositionMs").toLongLong();
    check(waitUntil([&]{return instantVideo->isVisible()&&
        std::abs(instantPlayer->property("position").toLongLong()-draggedInstantSeek)<=60;},2'000),
          "mouse scrubbing presents the exact frame at the released playhead position");
    QElapsedTimer pausedDragHold;pausedDragHold.start();
    while(pausedDragHold.elapsed()<400){QCoreApplication::processEvents(QEventLoop::AllEvents,25);QThread::msleep(10);}
    check(timelineController.playheadMs()==pausedDragTarget&&instantVideo->isVisible(),
          "the mouse-scrub frame remains visible after scrubbing stops");
    const auto beforePausedStep=timelineController.playheadMs();
    sendKey(window,Qt::Key_Right);
    const auto pausedStepTarget=timelineController.playheadMs();
    check(pausedStepTarget>beforePausedStep&&timelineController.livePreviewUrl().isEmpty(),
          "frame stepping retires the paused sequential Program stream instead of leaving its stale frame underneath");
    const auto steppedInstantSeek=timelineController.instantPreview().value("sourcePositionMs").toLongLong();
    check(waitUntil([&]{return instantVideo->isVisible()&&
        std::abs(instantPlayer->property("position").toLongLong()-steppedInstantSeek)<=60;},2'000),
          "Right Arrow presents the exact newly selected source frame immediately");
    QElapsedTimer pausedStepHold;pausedStepHold.start();
    while(pausedStepHold.elapsed()<400){QCoreApplication::processEvents(QEventLoop::AllEvents,25);QThread::msleep(10);}
    check(timelineController.playheadMs()==pausedStepTarget&&instantVideo->isVisible(),
          "the arrow-key scrub frame remains selected after the preview-release interval");
    check(engine.rootObjects().front()->property("programDroppedFrames").toInt() < 3,
          "the dropped-frame counter ignores seeks, pauses, and preview rebuilds");

    timelineController.select(videoItemId);
    check(timelineController.splitSelected(3'000),"fixture can be split to create a real effect boundary");
    QString effectedRightItemId;
    if(const auto* current=projectController.project().findSequence(timelineController.activeSequenceId().toStdString()))
        for(const auto&track:current->tracks)if(track.kind==ytp::TrackKind::Video)
            for(const auto&item:track.items)if(item.timelineStart==ytp::Rational{3,1})
                effectedRightItemId=QString::fromStdString(item.id);
    check(!effectedRightItemId.isEmpty(),"the right side of the effect-boundary fixture exists");
    timelineController.select(effectedRightItemId);
    rootObject->setProperty("markInMs",0);
    rootObject->setProperty("markOutMs",14'000);
    const auto libraryCountBeforeTimelineCreate=projectController.project().libraryClips().size();
    sendKey(window,Qt::Key_C,"c");
    check(waitUntil([&]{return projectController.project().libraryClips().size()==libraryCountBeforeTimelineCreate+1;},2'000),
          "C creates a reusable clip from the selected cut timeline segment");
    if(projectController.project().libraryClips().size()==libraryCountBeforeTimelineCreate+1){
        const auto&createdFromTimeline=projectController.project().libraryClips().back();
        check(createdFromTimeline.sourceRange.start()==ytp::Rational{3,1}&&
                  createdFromTimeline.sourceRange.duration()==ytp::Rational{3,1},
              "timeline clip creation uses the cut segment's source range instead of the full Source viewer range");
        projectController.undo();
        QCoreApplication::processEvents();
    }
    check(timelineController.applyYtpVisualPreset(QStringLiteral("threshold_vision")),
          "Threshold Vision applies only after the split boundary");
    check(timelineController.addEffectToSelection(QStringLiteral("video_feedback")) &&
              timelineController.addEffectToSelection(QStringLiteral("pixel_sort")) &&
              timelineController.addEffectToSelection(QStringLiteral("radial_ripple")),
          "the effect-boundary playback fixture carries the heavy stress stack");
    timelineController.setPlayheadMs(2'400);
    sendKey(window,Qt::Key_Space," ");
    check(waitUntil([&]{return instantPlayer->property("playbackState").toInt()==1&&
                                  timelineController.playheadMs()>3'150&&
                                  !timelineController.instantPreview().value("exact").toBool()&&
                                  timelineController.instantPreview().value("draftSafe").toBool();},5'000) &&
              programPlayer->property("playbackState").toInt()!=1 &&
              timelineController.livePreviewUrl().isEmpty(),
          "direct draft playback crosses into an effected clip without starting a live re-encode");
    const auto effectClockStart=timelineController.playheadMs();
    QElapsedTimer effectClockTimer;effectClockTimer.start();
    while(effectClockTimer.elapsed()<800){QCoreApplication::processEvents(QEventLoop::AllEvents,25);QThread::msleep(10);}
    const auto effectClockAdvance=timelineController.playheadMs()-effectClockStart;
    check(effectClockAdvance>=500&&effectClockAdvance<=1'200&&
              rootObject->property("programPlaybackRequested").toBool()&&
              programPlayer->property("playbackState").toInt()!=1&&timelineController.livePreviewUrl().isEmpty(),
          "effect playback stays clocked in real time without an FFmpeg process or random stop");
    sendKey(window,Qt::Key_Space," ");
    sendKey(window,Qt::Key_Space," ");
    check(waitUntil([&]{return instantPlayer->property("playbackState").toInt()==1&&
                                timelineController.livePreviewUrl().isEmpty();},2'000),
          "pausing and resuming an effect remains immediate and does not start a render process");
    sendKey(window,Qt::Key_Space," ");
    projectController.undo();
    projectController.undo();
    projectController.undo();
    projectController.undo();
    projectController.undo();

    timelineController.select(videoItemId);
    check(timelineController.moveSelected(1'000),"linked A/V can be moved to create a leading timeline gap");
    timelineController.setPlayheadMs(200);
    check(timelineController.instantPreview().isEmpty(),"empty timeline space has no direct-source preview");
    sendKey(window,Qt::Key_Space," ");
    const auto gapPlayback=waitUntil([&]{return timelineController.playheadMs()>350;},2'000) &&
              engine.rootObjects().front()->property("programPlaybackRequested").toBool()&&
              programPlayer->property("playbackState").toInt()!=1&&
              timelineController.livePreviewUrl().isEmpty();
    check(gapPlayback,
          "Program playback clocks through empty space without stopping or starting FFmpeg");
    sendKey(window,Qt::Key_Space," ");
    projectController.undo();

    timeline->setProperty("contentX", 0.0);
    timeline->setProperty("contentY", 180.0);
    QCoreApplication::processEvents();
    auto* marqueeArea = findVisualItem(window->contentItem(), [&](QQuickItem* candidate) {
        return candidate->objectName() == QStringLiteral("timelineMarqueeArea") &&
               candidate->property("timelineTrackId").toString() == videoTrackId;
    });
    if (!marqueeArea) {
        std::cerr << "Expected marquee track " << videoTrackId.toStdString() << " from item " << videoItemId.toStdString() << '\n';
        for (auto* object : engine.rootObjects().front()->findChildren<QObject*>())
            if (object->objectName().contains(QStringLiteral("timelineMarqueeArea")))
                std::cerr << "Available marquee area: " << object->objectName().toStdString() << '\n';
    }
    check(marqueeArea, "video track exposes an empty-space marquee interaction area");
    if (marqueeArea) {
        const QPointF marqueeStart = marqueeArea->mapToScene(QPointF(560, 90));
        const QPointF marqueeEnd = marqueeArea->mapToScene(QPointF(80, 90));
        sendMouse(window, QEvent::MouseButtonPress, marqueeStart, Qt::LeftButton, Qt::LeftButton);
        sendMouse(window, QEvent::MouseMove, marqueeEnd, Qt::NoButton, Qt::LeftButton);
        check(marquee->isVisible(), "dragging empty timeline space displays the marquee box");
        sendMouse(window, QEvent::MouseButtonRelease, marqueeEnd, Qt::LeftButton, Qt::NoButton);
        check(!marquee->isVisible() && timelineController.selectedIds().size() == 2,
              "marquee selection selects the intersecting linked video and audio clips");
    }

    timelineController.addMarker(7'000, QStringLiteral("Drag snap target"));
    QCoreApplication::processEvents();
    auto* videoClip = findVisualItem(window->contentItem(), [&](QQuickItem* candidate) {
        return candidate->objectName() == QStringLiteral("timelineClip_") + videoItemId && candidate->isVisible();
    });
    auto* audioClip = findVisualItem(window->contentItem(), [&](QQuickItem* candidate) {
        return candidate->objectName() == QStringLiteral("timelineClip_") + audioItemId && candidate->isVisible();
    });
    check(videoClip, "selected video clip exposes its timeline drag surface");
    check(audioClip, "linked audio clip exposes its timeline drag surface");
    if (videoClip && audioClip) {
        auto* selectionEmphasis = findVisualItem(videoClip, [](QQuickItem* candidate) {
            return candidate->objectName() == QStringLiteral("timelineSelectionEmphasis");
        });
        const auto firstClick = videoClip->mapToScene(QPointF(videoClip->width() / 2, videoClip->height() / 2));
        timelineController.clearSelection();
        QCoreApplication::processEvents();
        sendMouse(window, QEvent::MouseButtonPress, firstClick, Qt::LeftButton, Qt::LeftButton);
        QCoreApplication::processEvents();
        if (!(timelineController.selectedIds().size() == 2 && selectionEmphasis && selectionEmphasis->isVisible()))
            std::cerr << "First-click diagnostic: selected=" << timelineController.selectedIds().size()
                      << " emphasis=" << (selectionEmphasis && selectionEmphasis->isVisible())
                      << " point=" << firstClick.x() << ',' << firstClick.y()
                      << " clip=" << videoClip->x() << ',' << videoClip->y() << ' '
                      << videoClip->width() << 'x' << videoClip->height() << '\n';
        check(timelineController.selectedIds().size() == 2 && selectionEmphasis && selectionEmphasis->isVisible(),
              "a timeline clip highlights on the first press without requiring a second click");
        sendMouse(window, QEvent::MouseButtonRelease, firstClick, Qt::LeftButton, Qt::NoButton);
        check(timelineController.selectedIds().size() == 2,
              "releasing the first clip click does not undo or repeat its selection");
        check(selectionEmphasis && selectionEmphasis->isVisible(),
              "selected timeline clips expose a high-contrast selection treatment");
        const auto dragStart = videoClip->mapToScene(QPointF(videoClip->width() / 2, videoClip->height() / 2));
        sendMouse(window, QEvent::MouseButtonPress, dragStart, Qt::RightButton, Qt::RightButton);
        sendMouse(window, QEvent::MouseButtonRelease, dragStart, Qt::RightButton, Qt::NoButton);
        auto* clipMenu = videoClip->findChild<QObject*>(QStringLiteral("clipContextMenu_") + videoItemId);
        if (!clipMenu) clipMenu = engine.rootObjects().front()->findChild<QObject*>(
            QStringLiteral("clipContextMenu_") + videoItemId);
        if (clipMenu && !clipMenu->property("visible").toBool()) {
            QMetaObject::invokeMethod(clipMenu, "popup");
            QCoreApplication::processEvents();
        }
        check(clipMenu && clipMenu->property("visible").toBool(),
              "right-clicking a timeline clip opens contextual edit and YTP actions");
        if (clipMenu) QMetaObject::invokeMethod(clipMenu, "close");
        QCoreApplication::processEvents();
        const auto dragEnd = dragStart + QPointF(79.2, 0);
        const auto videoStartX = videoClip->x();
        const auto audioStartX = audioClip->x();
        sendMouse(window, QEvent::MouseButtonPress, dragStart, Qt::LeftButton, Qt::LeftButton);
        sendMouse(window, QEvent::MouseMove, dragStart + QPointF(40, 0), Qt::NoButton, Qt::LeftButton);
        const auto videoDragDelta = videoClip->x() - videoStartX;
        const auto audioDragDelta = audioClip->x() - audioStartX;
        check(std::abs(videoDragDelta) > 5.0 && std::abs(videoDragDelta - audioDragDelta) < 1.0,
              "linked video and audio clips move together during the drag gesture");
        sendMouse(window, QEvent::MouseMove, dragEnd, Qt::NoButton, Qt::LeftButton);
        sendMouse(window, QEvent::MouseButtonRelease, dragEnd, Qt::LeftButton, Qt::NoButton);
        const auto* movedVideo = projectController.project().sequences().front().findItem(videoItemId.toStdString());
        const auto movedStartMs = movedVideo ? static_cast<qint64>(std::llround(movedVideo->timelineStart.asLongDouble() * 1000.0L)) : -1;
        if (!(movedVideo && movedStartMs >= 990 && movedStartMs <= 1'010 && timelineController.selectedIds().size() == 2))
            std::cerr << "Clip drag start=" << movedStartMs << " selected=" << timelineController.selectedIds().size() << '\n';
        check(movedVideo && movedStartMs >= 990 && movedStartMs <= 1'010 && timelineController.selectedIds().size() == 2,
              "dragging a marquee-selected clip moves the linked selection and snaps its trailing edge to a marker");
    }

    auto itemCount = [&] {
        qsizetype count = 0;
        for (const auto& track : projectController.project().sequences().front().tracks)
            count += static_cast<qsizetype>(track.items.size());
        return count;
    };
    auto clickButton = [&](const QString& name) {
        auto* button = engine.rootObjects().front()->findChild<QQuickItem*>(name);
        check(button && button->isEnabled(), "requested timeline control is present and enabled");
        if (!button || !button->isEnabled()) return false;
        if (!button->isVisible()) {
            QMetaObject::invokeMethod(button, "clicked");
            QCoreApplication::processEvents();
            return true;
        }
        const auto center = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
        sendMouse(window, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
        sendMouse(window, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
        return true;
    };

    timelineController.setPlayheadMs(2'000);
    const auto initialItemCount = itemCount();
    clickButton(QStringLiteral("timelineSplitButton"));
    check(waitUntil([&]{ return itemCount() == initialItemCount + 2; }),
          "the visible Split control splits the selected linked clips");
    clickButton(QStringLiteral("timelineDuplicateButton"));
    check(waitUntil([&]{ return itemCount() == initialItemCount + 4; }),
          "the visible Duplicate control creates linked copies");
    clickButton(QStringLiteral("timelineGroupButton"));
    QString selectedGroup;
    bool selectionGrouped = !timelineController.selectedIds().isEmpty();
    for (const auto& id : timelineController.selectedIds()) {
        const auto* item = projectController.project().sequences().front().findItem(id.toStdString());
        if (!item || item->groupId.empty()) { selectionGrouped = false; break; }
        if (selectedGroup.isEmpty()) selectedGroup = QString::fromStdString(item->groupId);
        else selectionGrouped = selectionGrouped && selectedGroup == QString::fromStdString(item->groupId);
    }
    check(selectionGrouped, "the visible Group control gives the selected clips one group identity");
    clickButton(QStringLiteral("timelineDeleteButton"));
    check(waitUntil([&]{ return itemCount() == initialItemCount + 2; }),
          "the visible Delete control removes the grouped duplicate selection");

    const auto markerCount = timelineController.markers().size();
    clickButton(QStringLiteral("timelineMarkerButton"));
    check(timelineController.markers().size() == markerCount + 1,
          "the visible Marker control creates a marker at the playhead");
    const auto snappingBefore = timelineController.snapping();
    clickButton(QStringLiteral("timelineMagnetButton"));
    check(timelineController.snapping() != snappingBefore,
          "the visible Magnet control toggles timeline snapping");

    auto* programOutput = engine.rootObjects().front()->findChild<QQuickItem*>(QStringLiteral("programVideoOutput"));
    check(programOutput && !programOutput->isVisible(),
          "the retired rendered-stream surface cannot cover native Program playback");

    auto boundaryProject=seeded;
    auto boundarySequence=boundaryProject.sequences().front();
    for(auto&track:boundarySequence.tracks)for(auto&item:track.items){item.timelineStart=ytp::Rational{};item.duration=ytp::Rational{14,1};item.sourceRange=ytp::TimeRange{ytp::Rational{},ytp::Rational{14,1}};}
    boundaryProject.updateSequence(std::move(boundarySequence));
    const auto boundaryPath=settingsDirectory.filePath("boundary-project.ytp.json");
    check(ytp::ProjectSerializer::save(boundaryProject,boundaryPath,&saveError)&&projectController.openProject(QUrl::fromLocalFile(boundaryPath)),
          "the long playback-boundary fixture opens");
    timelineController.setPlayheadMs(0);sendKey(window,Qt::Key_Space," ");
    check(waitUntil([&]{return instantPlayer->property("playbackState").toInt()==1;},5'000),
          "long clean Program playback starts directly from the timeline origin");
    const auto uninterruptedSource=instantPlayer->property("source").toUrl();
    check(waitUntil([&]{return timelineController.playheadMs()>12'400;},25'000)&&
              instantPlayer->property("playbackState").toInt()==1&&instantPlayer->property("source").toUrl()==uninterruptedSource,
          "direct Program playback crosses 12 seconds without EOF or media-source replacement");
    sendKey(window,Qt::Key_Space," ");

    for (const auto& error : bindingErrors)
        std::cerr << "QML: " << error.toStdString() << '\n';
    check(bindingErrors.isEmpty(), "native playback and layout changes do not produce QML errors");

    qInstallMessageHandler(previousHandler);
    std::cout << "QML tabs/pages, sticky ruler, Program playback, frame accounting, and track interaction passed.\n";
    return failures == 0 ? 0 : 1;
}
