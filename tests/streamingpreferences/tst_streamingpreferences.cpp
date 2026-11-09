#include "streamingpreferences.h"
#include "cli/commandlineparser.h"

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

static const QString HOST_A = "11111111-1111-1111-1111-111111111111";
static const QString HOST_B = "22222222-2222-2222-2222-222222222222";

using PrefsPtr = std::unique_ptr<StreamingPreferences>;

class TestStreamingPreferences : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_SettingsDir;

    static PrefsPtr load(const QString& hostUuid = QString())
    {
        return PrefsPtr(StreamingPreferences::forHost(hostUuid));
    }

    static PrefsPtr edit(const QString& hostUuid)
    {
        return PrefsPtr(StreamingPreferences::createHostEditor(hostUuid));
    }

    // Changes one host setting the way the settings page does
    static void setHostSetting(const QString& hostUuid, const char* property, const QVariant& value)
    {
        PrefsPtr editor = edit(hostUuid);
        editor->setProperty(property, value);
        editor->save();
    }

    static QStringList overrideKeys(const QString& hostUuid)
    {
        QSettings settings;
        settings.beginGroup("hostsettings/" + hostUuid);
        QStringList keys = settings.childKeys();
        keys.sort();
        return keys;
    }

    static QVariantMap globalValues()
    {
        QSettings settings;
        QVariantMap values;
        for (const QString& key : settings.childKeys()) {
            values[key] = settings.value(key);
        }
        return values;
    }

    static void setGlobal(const QString& key, const QVariant& value)
    {
        QSettings settings;
        settings.setValue(key, value);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_SettingsDir.isValid());
        QCoreApplication::setOrganizationName("MoonlightTest");
        QCoreApplication::setApplicationName("StreamingPreferencesTest");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_SettingsDir.path());
    }

    void init()
    {
        QSettings settings;
        settings.clear();
        settings.sync();
    }

    // An installation without overrides resolves every host to the global settings
    void noOverridesMatchesGlobal()
    {
        setGlobal("width", 2560);
        setGlobal("height", 1440);
        setGlobal("fps", 90);
        setGlobal("bitrate", 42000);
        setGlobal("capturesyskeys", StreamingPreferences::CSK_ALWAYS);

        PrefsPtr global = load();
        PrefsPtr host = edit(HOST_A);

        QCOMPARE(host->width, 2560);
        QCOMPARE(host->height, 1440);
        QCOMPARE(host->fps, 90);
        QCOMPARE(host->bitrateKbps, 42000);
        QCOMPARE(host->captureSysKeysMode, StreamingPreferences::CSK_ALWAYS);
        QCOMPARE(host->windowMode, global->windowMode);
        QCOMPARE(host->enableYUV444, global->enableYUV444);
        QVERIFY(host->overriddenSettings().isEmpty());

        // Opening an editor and saving without changes writes nothing
        host->save();
        QVERIFY(!QSettings().childGroups().contains("hostsettings"));
    }

    // Editing the global settings works as before and creates no host overrides
    void globalSettingsPersistGlobally()
    {
        PrefsPtr global = load();
        global->fps = 120;
        global->save();

        QCOMPARE(QSettings().value("fps").toInt(), 120);
        QVERIFY(!QSettings().childGroups().contains("hostsettings"));
        QCOMPARE(load(HOST_A)->fps, 120);

        // The same keys as before per-host settings existed are written,
        // with enums stored as integers
        QStringList keys = QSettings().childKeys();
        keys.sort();
        QCOMPARE(keys, QStringList({
            "abstouchmode", "audiocfg", "autoadjustbitrate", "backgroundgamepad", "bitrate",
            "capturesyskeys", "confwarnings", "connwarnings", "defaultver", "detectnetblocking",
            "fps", "framepacing", "gameopts", "gamepadmouse", "hdr", "height", "hostaudio",
            "keepawake", "language", "mdns", "mouseacceleration", "multicontroller",
            "muteonfocusloss", "packetsize", "quitAppAfter", "renderer", "reversescroll",
            "richpresence", "showperfoverlay", "swapfacebuttons", "swapmousebuttons",
            "uidisplaymode", "unlockbitrate", "videocfg", "videodec", "vsync", "width",
            "windowmode", "yuv444" }));
        QCOMPARE(QSettings().value("capturesyskeys").toString(), QString("0"));
    }

    // Changing one setting for a host creates only that override and
    // leaves the global settings and other hosts untouched
    void hostEditCreatesSingleOverride()
    {
        setGlobal("fps", 60);
        QVariantMap globalBefore = globalValues();

        setHostSetting(HOST_A, "fps", 120);

        QCOMPARE(overrideKeys(HOST_A), QStringList({"fps"}));
        QCOMPARE(load(HOST_A)->fps, 120);
        QCOMPARE(load(HOST_B)->fps, 60);
        QCOMPARE(globalValues(), globalBefore);
    }

    // Settings without overrides follow later global changes
    void inheritanceFollowsGlobalChanges()
    {
        setGlobal("width", 1920);
        setGlobal("height", 1080);
        setGlobal("fps", 60);
        setHostSetting(HOST_A, "fps", 120);

        PrefsPtr global = load();
        global->width = 3840;
        global->height = 2160;
        global->fps = 30;
        global->save();

        PrefsPtr host = load(HOST_A);
        QCOMPARE(host->width, 3840);
        QCOMPARE(host->height, 2160);
        QCOMPARE(host->fps, 120);
    }

    // Changing a host value back to the global value removes the override
    void revertingToGlobalRemovesOverride()
    {
        setGlobal("fps", 60);

        PrefsPtr editor = edit(HOST_A);
        editor->fps = 120;
        editor->absoluteMouseMode = true;
        editor->save();
        QCOMPARE(overrideKeys(HOST_A), QStringList({"fps", "mouseacceleration"}));

        editor->fps = 60;
        editor->save();
        QCOMPARE(overrideKeys(HOST_A), QStringList({"mouseacceleration"}));

        // The host now inherits subsequent global changes
        setGlobal("fps", 90);
        QCOMPARE(load(HOST_A)->fps, 90);
    }

    // Overrides that become equal to a changed global value are kept
    void staleEqualOverrideIsRetained()
    {
        setGlobal("fps", 60);
        setHostSetting(HOST_A, "fps", 120);
        setGlobal("fps", 120);

        // Editing an unrelated setting must not drop the fps override
        PrefsPtr editor = edit(HOST_A);
        editor->enableVsync = !editor->enableVsync;
        editor->save();
        QCOMPARE(overrideKeys(HOST_A), QStringList({"fps", "vsync"}));

        setGlobal("fps", 30);
        QCOMPARE(load(HOST_A)->fps, 120);
    }

    // Reset removes all of the host's overrides and nothing else
    void resetRemovesAllOverrides()
    {
        setGlobal("fps", 60);
        setHostSetting(HOST_B, "bitrateKbps", 12345);

        PrefsPtr editor = edit(HOST_A);
        editor->fps = 120;
        editor->captureSysKeysMode = StreamingPreferences::CSK_ALWAYS;
        editor->save();
        QCOMPARE(editor->overriddenSettings(), QStringList({"fps", "captureSysKeysMode"}));

        QSignalSpy spy(editor.get(), &StreamingPreferences::overriddenSettingsChanged);
        editor->resetHostOverrides();

        QVERIFY(spy.count() >= 1);
        QVERIFY(editor->overriddenSettings().isEmpty());
        QVERIFY(overrideKeys(HOST_A).isEmpty());
        QCOMPARE(editor->fps, 60);
        QCOMPARE(editor->captureSysKeysMode, StreamingPreferences::CSK_OFF);
        QCOMPARE(overrideKeys(HOST_B), QStringList({"bitrate"}));
        QCOMPARE(QSettings().value("fps").toInt(), 60);

        // Saving after the reset must not bring the overrides back
        editor->save();
        QVERIFY(overrideKeys(HOST_A).isEmpty());

        // Reset of the global settings does nothing, not even a reload
        PrefsPtr global = load();
        global->fps = 90;
        global->resetHostOverrides();
        QCOMPARE(global->fps, 90);
        QCOMPARE(QSettings().value("fps").toInt(), 60);
    }

    // overriddenSettings tracks unsaved edits and notifies on property writes
    // from QML, so the settings page updates live
    void hasOverridesTracksPendingEdits()
    {
        setGlobal("fps", 60);

        PrefsPtr editor = edit(HOST_A);
        QVERIFY(editor->overriddenSettings().isEmpty());

        QSignalSpy spy(editor.get(), &StreamingPreferences::overriddenSettingsChanged);
        editor->setProperty("fps", 120);
        QVERIFY(spy.count() >= 1);
        QCOMPARE(editor->overriddenSettings(), QStringList({"fps"}));

        editor->setProperty("fps", 60);
        QVERIFY(editor->overriddenSettings().isEmpty());

        // The global settings never report overrides
        PrefsPtr global = load();
        global->setProperty("fps", 120);
        QVERIFY(global->overriddenSettings().isEmpty());
    }

    // A host's settings don't depend on which host is open in an editor
    void hostsAreIndependent()
    {
        PrefsPtr editorB = edit(HOST_B);
        setHostSetting(HOST_A, "fps", 120);
        editorB->fps = 30;
        editorB->save();

        QCOMPARE(load(HOST_A)->fps, 120);
        QCOMPARE(load(HOST_B)->fps, 30);
        QCOMPARE(overrideKeys(HOST_A), QStringList({"fps"}));
        QCOMPARE(overrideKeys(HOST_B), QStringList({"fps"}));
    }

    // Booleans, enums and input settings resolve per host
    void independentHostConfigurations()
    {
        setGlobal("yuv444", false);

        PrefsPtr a = edit(HOST_A);
        a->windowMode = StreamingPreferences::WM_WINDOWED;
        a->absoluteMouseMode = true;
        a->captureSysKeysMode = StreamingPreferences::CSK_ALWAYS;
        a->richPresence = false;
        a->save();

        PrefsPtr b = edit(HOST_B);
        b->windowMode = StreamingPreferences::WM_FULLSCREEN_DESKTOP;
        b->unlockBitrate = true;
        b->bitrateKbps = 500000;
        b->enableYUV444 = true;
        b->save();

        a = load(HOST_A);
        QCOMPARE(a->windowMode, StreamingPreferences::WM_WINDOWED);
        QCOMPARE(a->absoluteMouseMode, true);
        QCOMPARE(a->captureSysKeysMode, StreamingPreferences::CSK_ALWAYS);
        QCOMPARE(a->richPresence, false);
        QCOMPARE(a->enableYUV444, false);

        b = load(HOST_B);
        QCOMPARE(b->windowMode, StreamingPreferences::WM_FULLSCREEN_DESKTOP);
        QCOMPARE(b->bitrateKbps, 500000);
        QCOMPARE(b->enableYUV444, true);
        QCOMPARE(b->captureSysKeysMode, StreamingPreferences::CSK_OFF);
        QCOMPARE(b->richPresence, true);

        // YUV 4:4:4 is inherited by host A and follows the global value
        setGlobal("yuv444", true);
        QCOMPARE(load(HOST_A)->enableYUV444, true);
    }

    // A host's bitrate follows the inherited bitrate limit without losing its override
    void bitrateLimitAppliesToInheritedOverrides()
    {
        setGlobal("unlockbitrate", true);
        setHostSetting(HOST_A, "bitrateKbps", 400000);
        QCOMPARE(load(HOST_A)->bitrateKbps, 400000);

        // Locking the global limit caps the host to the slider maximum
        setGlobal("unlockbitrate", false);
        PrefsPtr editor = edit(HOST_A);
        QCOMPARE(editor->bitrateKbps, 150000);

        // Viewing the host doesn't overwrite its stored bitrate
        editor->save();
        QCOMPARE(QSettings().value("hostsettings/" + HOST_A + "/bitrate").toInt(), 400000);

        // Unlocking again restores the host's own bitrate
        setGlobal("unlockbitrate", true);
        QCOMPARE(load(HOST_A)->bitrateKbps, 400000);
    }

    // App-wide settings are never stored or resolved per host
    void appWideSettingsAreNotHostScoped()
    {
        setGlobal("language", StreamingPreferences::LANG_DE);
        setGlobal("mdns", true);

        // Overrides written by hand for app-wide keys are ignored
        QSettings().setValue("hostsettings/" + HOST_A + "/language", StreamingPreferences::LANG_FR);
        QSettings().setValue("hostsettings/" + HOST_A + "/mdns", false);
        PrefsPtr host = load(HOST_A);
        QCOMPARE(host->language, StreamingPreferences::LANG_DE);
        QCOMPARE(host->enableMdns, true);

        QSettings().remove("hostsettings");

        PrefsPtr editor = edit(HOST_A);
        editor->language = StreamingPreferences::LANG_FR;
        editor->enableMdns = false;
        editor->uiDisplayMode = StreamingPreferences::UI_FULLSCREEN;
        QVERIFY(editor->overriddenSettings().isEmpty());
        editor->save();

        QVERIFY(overrideKeys(HOST_A).isEmpty());
        QCOMPARE(QSettings().value("language").toInt(), static_cast<int>(StreamingPreferences::LANG_DE));
        QCOMPARE(QSettings().value("mdns").toBool(), true);
    }

    // "moonlight stream" starts from the host's settings unless --no-host-settings
    // is given, and its options apply on top without being saved
    void commandLineHostSettingsSelection()
    {
        setGlobal("fps", 60);
        setHostSetting(HOST_A, "fps", 120);

        const QStringList base = { "moonlight", "stream", "host", "app" };

        StreamCommandLineParser hostParser;
        PrefsPtr host = load(HOST_A);
        hostParser.parse(base + QStringList({ "--bitrate", "30000" }), host.get());
        QVERIFY(hostParser.getUseHostSettings());
        QCOMPARE(host->fps, 120);
        QCOMPARE(host->bitrateKbps, 30000);

        StreamCommandLineParser globalParser;
        globalParser.parse(base + QStringList({ "--no-host-settings" }), load().get());
        QVERIFY(!globalParser.getUseHostSettings());
    }

    // Deleting a host removes only that host's overrides
    void removeHostOverrides()
    {
        setHostSetting(HOST_A, "fps", 144);
        setHostSetting(HOST_B, "fps", 30);

        // An empty UUID must not remove the whole host settings group
        StreamingPreferences::removeHostOverrides(QString());
        QCOMPARE(overrideKeys(HOST_A), QStringList({"fps"}));

        StreamingPreferences::removeHostOverrides(HOST_A);
        QVERIFY(overrideKeys(HOST_A).isEmpty());
        QCOMPARE(overrideKeys(HOST_B), QStringList({"fps"}));
    }
};

QTEST_GUILESS_MAIN(TestStreamingPreferences)
#include "tst_streamingpreferences.moc"
