#include <QtTest>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <qqml.h>

#include "backend.h"
#include "markdownhighlighter.h"

class OmawriteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QVERIFY(m_dataDirectory.isValid());
        QVERIFY(qputenv("XDG_DATA_HOME", m_dataDirectory.path().toUtf8()));
        QQuickStyle::setStyle(QStringLiteral("Material"));
        qmlRegisterType<Backend>("Omawrite", 1, 0, "DocumentBackend");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\"\n"
            "accent = \"#112233\"\n"
            "selection = \"#445566\"\n"
            "background = \"#fefefe\"\n"
            "foreground = \"#101010\"\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(backend.themeSelection(), QStringLiteral("#445566"));
        QVERIFY(!backend.darkMode());
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/MutationHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void savesAndOpensFromFooterButtons() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("initialSessionTabs"), QVariantList());
        engine.rootContext()->setContextProperty(QStringLiteral("initialOpenUrl"), QUrl());
        engine.rootContext()->setContextProperty(QStringLiteral("initialActiveTabIndex"), 0);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QTRY_VERIFY(window->property("activeEditor").value<QObject *>());
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("modeToggle")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        Backend *activeBackend = qobject_cast<Backend *>(
            window->property("activeBackend").value<QObject *>());
        QVERIFY(activeBackend);

        QSignalSpy saveDialogSpy(activeBackend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QSignalSpy openDialogSpy(activeBackend, &Backend::openDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        QCOMPARE(openDialogSpy.count(), 1);
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("initialSessionTabs"), QVariantList());
        engine.rootContext()->setContextProperty(QStringLiteral("initialOpenUrl"), QUrl());
        engine.rootContext()->setContextProperty(QStringLiteral("initialActiveTabIndex"), 0);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = nullptr;
        QTRY_VERIFY(editor = window->property("activeEditor").value<QObject *>());
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        // `omarchy display text size 16` sets the GNOME factor to 16/12.
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 15);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 15);
    }

    void persistsTabSessionLayout() {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        const QString path = files.filePath(QStringLiteral("saved.md"));
        QFile savedFile(path);
        QVERIFY(savedFile.open(QIODevice::WriteOnly | QIODevice::Text));
        QCOMPARE(savedFile.write("from disk"), qint64(9));
        savedFile.close();

        Backend store;
        QVariantList entries;
        entries << QVariantMap{{QStringLiteral("empty"), true}};
        entries << QVariantMap{{QStringLiteral("fileUrl"), QUrl::fromLocalFile(path).toString()}};
        entries << QVariantMap{{QStringLiteral("text"), QStringLiteral("draft text")},
                               {QStringLiteral("modified"), true}};
        store.saveSessionTabs(entries, 1);

        const QVariantList restored = store.restoreSessionTabs();
        QCOMPARE(restored.size(), 2);
        QCOMPARE(restored.at(0).toMap().value(QStringLiteral("fileUrl")).toString(),
                 QUrl::fromLocalFile(path).toString());
        QVERIFY(!restored.at(0).toMap().contains(QStringLiteral("text")));
        QCOMPARE(restored.at(1).toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("draft text"));
        QCOMPARE(store.restoreSessionActiveIndex(), 1);
    }

    void savedCleanTabsPersistAsFileReferences() {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        const QString path = files.filePath(QStringLiteral("saved-clean.md"));

        Backend document;
        document.saveAs(QUrl::fromLocalFile(path));
        const QVariantMap entry = document.sessionEntry();
        QCOMPARE(entry.value(QStringLiteral("fileUrl")).toString(),
                 QUrl::fromLocalFile(path).toString());
        QVERIFY(!entry.contains(QStringLiteral("text")));
    }

    void qmlFlushesMultipleEditedTabs() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("initialSessionTabs"), QVariantList());
        engine.rootContext()->setContextProperty(QStringLiteral("initialOpenUrl"), QUrl());
        engine.rootContext()->setContextProperty(QStringLiteral("initialActiveTabIndex"), 0);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = nullptr;
        QTRY_VERIFY(editor = window->property("activeEditor").value<QObject *>());
        editor->setProperty("text", QStringLiteral("tab one"));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("tab one"));
        QObject *firstEditor = editor;
        QVERIFY(QMetaObject::invokeMethod(window.data(), "createTab", Q_ARG(QVariant, QVariant())));
        QTRY_COMPARE(window->property("currentTabIndex").toInt(), 1);
        QTRY_VERIFY((editor = window->property("activeEditor").value<QObject *>()) && editor != firstEditor);
        editor->setProperty("text", QStringLiteral("tab two"));
        QObject *secondEditor = editor;
        QVERIFY(QMetaObject::invokeMethod(window.data(), "createTab", Q_ARG(QVariant, QVariant())));
        QTRY_COMPARE(window->property("currentTabIndex").toInt(), 2);
        QTRY_VERIFY((editor = window->property("activeEditor").value<QObject *>()) && editor != secondEditor);
        editor->setProperty("text", QStringLiteral("tab three"));
        QVariant entriesVariant;
        QVERIFY(QMetaObject::invokeMethod(window.data(), "sessionEntries",
                                          Q_RETURN_ARG(QVariant, entriesVariant)));
        const QVariantList entries = entriesVariant.toList();
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries.at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("tab one"));
        QCOMPARE(entries.at(1).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("tab two"));
        QCOMPARE(entries.at(2).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("tab three"));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "flushTabs"));

        const QVariantList restored = backend.restoreSessionTabs();
        QCOMPARE(restored.size(), 3);
        QCOMPARE(restored.at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("tab one"));
        QCOMPARE(restored.at(1).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("tab two"));
        QCOMPARE(restored.at(2).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("tab three"));
    }

    void qmlFlushesEditedTabSession() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("initialSessionTabs"), QVariantList());
        engine.rootContext()->setContextProperty(QStringLiteral("initialOpenUrl"), QUrl());
        engine.rootContext()->setContextProperty(QStringLiteral("initialActiveTabIndex"), 0);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = nullptr;
        QTRY_VERIFY(editor = window->property("activeEditor").value<QObject *>());
        editor->setProperty("text", QStringLiteral("unsaved session text"));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "flushTabs"));

        const QVariantList restored = backend.restoreSessionTabs();
        QCOMPARE(restored.size(), 1);
        QCOMPARE(restored.at(0).toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("unsaved session text"));
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

private:
    QTemporaryDir m_settingsDirectory;
    QTemporaryDir m_dataDirectory;
};

QTEST_MAIN(OmawriteTest)
#include "tst_omawrite.moc"
