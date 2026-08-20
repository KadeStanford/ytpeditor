#include "core/rational.h"

#ifdef YTP_HAS_QT
#include <QGuiApplication>
#include <QDir>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <qqml.h>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include "ui/project_controller.h"
#include "ui/timeline_controller.h"
#include "ui/export_controller.h"
#include "media/native_media_player.h"
#endif

#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
#ifdef YTP_HAS_QT
    qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Basic"));
    QGuiApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("YTP Editor"));
    application.setOrganizationName(QStringLiteral("YTP Editor"));
    application.setApplicationVersion(QStringLiteral("0.5.0-alpha"));
    const auto bundledFrei0rPath=QDir(application.applicationDirPath()).filePath(QStringLiteral("frei0r-1"));
    if(QDir(bundledFrei0rPath).exists())
        qputenv("FREI0R_PATH",QDir::toNativeSeparators(bundledFrei0rPath).toUtf8());

    ytp::ProjectController projectController;
    ytp::TimelineController timelineController(&projectController);
    ytp::ExportController exportController(&projectController, &timelineController);
    qmlRegisterType<ytp::NativeMediaPlayer>("YTPEditor.Native", 1, 0, "NativeMediaPlayer");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("projectController"), &projectController);
    engine.rootContext()->setContextProperty(QStringLiteral("timelineController"), &timelineController);
    engine.rootContext()->setContextProperty(QStringLiteral("exportController"), &exportController);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("YTPEditor"), QStringLiteral("Main"));

    bool smokeTest = false;
    bool showGuide = false;
    bool showToolkit = false;
    bool showFinish = false;
    bool showRemix = false;
    bool showDual = false;
    bool testInspectorWheel = false;
    int screenshotWidth = 0;
    int screenshotHeight = 0;
    int inspectorTab = -1;
    int inspectorSection = -1;
    QString screenshotPath;
    for (int index = 1; index < argc; ++index) {
        if (std::string_view{argv[index]} == "--smoke-test") smokeTest = true;
        if (std::string_view{argv[index]} == "--show-guide") showGuide = true;
        if (std::string_view{argv[index]} == "--show-toolkit") showToolkit = true;
        if (std::string_view{argv[index]} == "--show-finish") showFinish = true;
        if (std::string_view{argv[index]} == "--show-remix") showRemix = true;
        if (std::string_view{argv[index]} == "--show-dual") showDual = true;
        if (std::string_view{argv[index]} == "--test-inspector-wheel") testInspectorWheel = true;
        if (std::string_view{argv[index]} == "--inspector-tab" && index + 1 < argc)
            inspectorTab = QString::fromLocal8Bit(argv[++index]).toInt();
        if (std::string_view{argv[index]} == "--inspector-section" && index + 1 < argc)
            inspectorSection = QString::fromLocal8Bit(argv[++index]).toInt();
        if (std::string_view{argv[index]} == "--screenshot" && index + 1 < argc) {
            screenshotPath = QString::fromLocal8Bit(argv[++index]);
        }
        if (std::string_view{argv[index]} == "--window-size" && index + 2 < argc) {
            screenshotWidth = QString::fromLocal8Bit(argv[++index]).toInt();
            screenshotHeight = QString::fromLocal8Bit(argv[++index]).toInt();
        }
    }
    if (showGuide) projectController.showFirstRunTutorial();
    if (showToolkit && !engine.rootObjects().isEmpty()) {
        projectController.dismissFirstRunTutorial();
        engine.rootObjects().front()->setProperty("inspectorContentIndex", 3);
        if (auto* tabs = engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("inspectorTabs")))
            tabs->setProperty("currentIndex", 2);
    }
    if (showFinish && !engine.rootObjects().isEmpty()) {
        projectController.dismissFirstRunTutorial();
        engine.rootObjects().front()->setProperty("inspectorContentIndex", 4);
        if (auto* tabs = engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("inspectorTabs")))
            tabs->setProperty("currentIndex", 2);
    }
    if (showRemix && !engine.rootObjects().isEmpty()) {
        projectController.dismissFirstRunTutorial();
        engine.rootObjects().front()->setProperty("inspectorContentIndex", 5);
        if (auto* tabs = engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("inspectorTabs")))
            tabs->setProperty("currentIndex", 2);
    }
    if (showDual && !engine.rootObjects().isEmpty()) {
        if (auto* tabs = engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("viewerTabs")))
            tabs->setProperty("currentIndex", 2);
    }
    if (inspectorTab >= 0 && !engine.rootObjects().isEmpty()) {
        projectController.dismissFirstRunTutorial();
        engine.rootObjects().front()->setProperty("inspectorContentIndex", inspectorTab);
        if (auto* tabs = engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("inspectorTabs"))) {
            const int visibleTab = inspectorTab == 1 ? 0 : inspectorTab == 3 ? 2 : inspectorTab >= 4 ? 2 : 0;
            tabs->setProperty("currentIndex", visibleTab);
            engine.rootObjects().front()->setProperty("inspectorContentIndex", inspectorTab);
        }
    }
    if (inspectorSection >= 0 && inspectorTab >= 1 && !engine.rootObjects().isEmpty()) {
        static constexpr const char* sectionNames[] = {"", "editSection", "", "ytpSection", "finishSection", "remixSection"};
        if (inspectorTab < 6) {
            if (auto* section = engine.rootObjects().front()->findChild<QObject*>(QString::fromLatin1(sectionNames[inspectorTab])))
                section->setProperty("currentIndex", inspectorSection);
        }
    }
    if (testInspectorWheel && !engine.rootObjects().isEmpty()) {
        application.setProperty("inspectorWheelMoved", false);
        QTimer::singleShot(500, &application, [&application, &engine, inspectorTab] {
            const auto roots = engine.rootObjects();
            auto* window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(roots.front());
            if (!window) return;
            static constexpr const char* pageNames[] = {
                "sourceInspectorPages", "editInspectorPages", "mixerScroll",
                "ytpToolsPages", "finishToolsPages", "remixToolsPages"
            };
            if (inspectorTab < 0 || inspectorTab >= 6) return;
            auto* inspector = roots.front()->findChild<QQuickItem*>(QString::fromLatin1(pageNames[inspectorTab]));
            if (!inspector || inspector->height() <= 0.0) return;
            const QPointF localPosition = inspector->mapToScene(
                QPointF(inspector->width() * 0.5, qMin(24.0, inspector->height() * 0.5)));
            QWheelEvent event(localPosition, window->mapToGlobal(localPosition.toPoint()), QPoint{}, QPoint{0, -120},
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window, &event);
            const char* scrollProperty = inspectorTab == 2 ? "testWheelContentY" : "contentY";
            application.setProperty("inspectorWheelMoved", inspector->property(scrollProperty).toDouble() > 0.0);
        });
    }
    if (!screenshotPath.isEmpty()) {
        if (!engine.rootObjects().isEmpty() && screenshotWidth > 0 && screenshotHeight > 0) {
            if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front())) {
                window->setWidth(screenshotWidth);
                window->setHeight(screenshotHeight);
            }
        }
        QTimer::singleShot(1'000, &application, [&application, &engine, screenshotPath, testInspectorWheel] {
            const auto roots = engine.rootObjects();
            auto* window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(roots.front());
            const bool saved = window && window->grabWindow().save(screenshotPath);
            const bool wheelMoved = !testInspectorWheel || application.property("inspectorWheelMoved").toBool();
            QCoreApplication::exit(saved && wheelMoved ? 0 : 1);
        });
    } else if (smokeTest) {
        QTimer::singleShot(100, &application, &QCoreApplication::quit);
    }
    return application.exec();
#else
    (void)argc;
    (void)argv;
    const auto ntscFrame = ytp::frameDuration(30'000, 1'001);
    std::cout << "YTP Editor foundation 0.5.0\n"
              << "Exact 29.97 fps frame duration: " << ntscFrame.toString() << " seconds\n";
    return 0;
#endif
}
