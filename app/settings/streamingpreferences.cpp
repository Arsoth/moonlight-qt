#include "streamingpreferences.h"
#include "utils.h"

#include <QSettings>
#include <QTranslator>
#include <QCoreApplication>
#include <QLocale>
#include <QReadWriteLock>
#include <QtMath>
#include <QMetaProperty>

#include <QtDebug>

#define SER_STREAMSETTINGS "streamsettings"
#define SER_WIDTH "width"
#define SER_HEIGHT "height"
#define SER_FPS "fps"
#define SER_BITRATE "bitrate"
#define SER_UNLOCK_BITRATE "unlockbitrate"
#define SER_AUTOADJUSTBITRATE "autoadjustbitrate"
#define SER_FULLSCREEN "fullscreen"
#define SER_VSYNC "vsync"
#define SER_GAMEOPTS "gameopts"
#define SER_HOSTAUDIO "hostaudio"
#define SER_MULTICONT "multicontroller"
#define SER_AUDIOCFG "audiocfg"
#define SER_VIDEOCFG "videocfg"
#define SER_HDR "hdr"
#define SER_YUV444 "yuv444"
#define SER_VIDEODEC "videodec"
#define SER_WINDOWMODE "windowmode"
#define SER_MDNS "mdns"
#define SER_QUITAPPAFTER "quitAppAfter"
#define SER_ABSMOUSEMODE "mouseacceleration"
#define SER_ABSTOUCHMODE "abstouchmode"
#define SER_STARTWINDOWED "startwindowed"
#define SER_FRAMEPACING "framepacing"
#define SER_CONNWARNINGS "connwarnings"
#define SER_CONFWARNINGS "confwarnings"
#define SER_UIDISPLAYMODE "uidisplaymode"
#define SER_RICHPRESENCE "richpresence"
#define SER_GAMEPADMOUSE "gamepadmouse"
#define SER_DEFAULTVER "defaultver"
#define SER_PACKETSIZE "packetsize"
#define SER_DETECTNETBLOCKING "detectnetblocking"
#define SER_SHOWPERFOVERLAY "showperfoverlay"
#define SER_SWAPMOUSEBUTTONS "swapmousebuttons"
#define SER_MUTEONFOCUSLOSS "muteonfocusloss"
#define SER_BACKGROUNDGAMEPAD "backgroundgamepad"
#define SER_REVERSESCROLL "reversescroll"
#define SER_SWAPFACEBUTTONS "swapfacebuttons"
#define SER_CAPTURESYSKEYS "capturesyskeys"
#define SER_KEEPAWAKE "keepawake"
#define SER_LANGUAGE "language"
#define SER_RENDERER "renderer"
#define SER_HOSTSETTINGS "hostsettings"

#define CURRENT_DEFAULT_VER 2

// Settings that a host can't override. Add a setting here if it's read
// outside of a Session, where a host override would never be applied.
static bool isAppWideKey(const QString& key)
{
    return key == SER_LANGUAGE || key == SER_UIDISPLAYMODE ||
           key == SER_MDNS || key == SER_DETECTNETBLOCKING;
}

// The stored settings and their properties
static const struct {
    const char* key;
    const char* property;
} k_Settings[] = {
    { SER_WIDTH, "width" },
    { SER_HEIGHT, "height" },
    { SER_FPS, "fps" },
    { SER_BITRATE, "bitrateKbps" },
    { SER_UNLOCK_BITRATE, "unlockBitrate" },
    { SER_AUTOADJUSTBITRATE, "autoAdjustBitrate" },
    { SER_VSYNC, "enableVsync" },
    { SER_GAMEOPTS, "gameOptimizations" },
    { SER_HOSTAUDIO, "playAudioOnHost" },
    { SER_MULTICONT, "multiController" },
    { SER_MDNS, "enableMdns" },
    { SER_QUITAPPAFTER, "quitAppAfter" },
    { SER_ABSMOUSEMODE, "absoluteMouseMode" },
    { SER_ABSTOUCHMODE, "absoluteTouchMode" },
    { SER_FRAMEPACING, "framePacing" },
    { SER_CONNWARNINGS, "connectionWarnings" },
    { SER_CONFWARNINGS, "configurationWarnings" },
    { SER_RICHPRESENCE, "richPresence" },
    { SER_GAMEPADMOUSE, "gamepadMouse" },
    { SER_DETECTNETBLOCKING, "detectNetworkBlocking" },
    { SER_SHOWPERFOVERLAY, "showPerformanceOverlay" },
    { SER_AUDIOCFG, "audioConfig" },
    { SER_HDR, "enableHdr" },
    { SER_YUV444, "enableYUV444" },
    { SER_VIDEOCFG, "videoCodecConfig" },
    { SER_VIDEODEC, "videoDecoderSelection" },
    { SER_RENDERER, "rendererSelection" },
    { SER_WINDOWMODE, "windowMode" },
    { SER_UIDISPLAYMODE, "uiDisplayMode" },
    { SER_LANGUAGE, "language" },
    { SER_SWAPMOUSEBUTTONS, "swapMouseButtons" },
    { SER_MUTEONFOCUSLOSS, "muteOnFocusLoss" },
    { SER_BACKGROUNDGAMEPAD, "backgroundGamepad" },
    { SER_REVERSESCROLL, "reverseScrollDirection" },
    { SER_SWAPFACEBUTTONS, "swapFaceButtons" },
    { SER_CAPTURESYSKEYS, "captureSysKeysMode" },
    { SER_KEEPAWAKE, "keepAwake" },
};

static QString hostSettingsGroup(const QString& hostUuid)
{
    return QString(SER_HOSTSETTINGS "/") + hostUuid;
}

static StreamingPreferences* s_GlobalPrefs;

Q_GLOBAL_STATIC(QReadWriteLock, s_GlobalPrefsLock)

StreamingPreferences::StreamingPreferences(QQmlEngine *qmlEngine, const QString& hostUuid, QObject* parent)
    : QObject(parent),
      m_QmlEngine(qmlEngine),
      m_HostUuid(hostUuid)
{
    reload();
}

StreamingPreferences* StreamingPreferences::forHost(const QString& hostUuid, QObject* parent)
{
    return new StreamingPreferences(nullptr, hostUuid, parent);
}

StreamingPreferences* StreamingPreferences::createHostEditor(const QString& hostUuid)
{
    StreamingPreferences* editor = forHost(hostUuid);
    editor->m_GlobalValues = StreamingPreferences(nullptr).serialize();

    // Any setting change may add or remove a pending override
    const QMetaObject* metaObject = editor->metaObject();
    QMetaMethod overridesChanged = QMetaMethod::fromSignal(&StreamingPreferences::overriddenSettingsChanged);
    for (int i = metaObject->propertyOffset(); i < metaObject->propertyCount(); i++) {
        QMetaProperty property = metaObject->property(i);
        if (property.hasNotifySignal() && property.notifySignal() != overridesChanged) {
            connect(editor, property.notifySignal(), editor, overridesChanged);
        }
    }

    return editor;
}

void StreamingPreferences::removeHostOverrides(const QString& hostUuid)
{
    // An empty UUID would remove the overrides of every host
    if (hostUuid.isEmpty()) {
        return;
    }

    QSettings settings;
    settings.remove(hostSettingsGroup(hostUuid));
}

QStringList StreamingPreferences::overriddenSettings() const
{
    QStringList properties;

    if (!m_HostUuid.isEmpty()) {
        QVariantMap overrides = pendingHostOverrides();
        for (const auto& setting : k_Settings) {
            if (overrides.contains(setting.key)) {
                properties.append(setting.property);
            }
        }
    }

    return properties;
}

void StreamingPreferences::resetHostOverrides()
{
    // Without a host, reload() would discard unsaved global changes
    if (m_HostUuid.isEmpty()) {
        return;
    }

    removeHostOverrides(m_HostUuid);
    reload();
    emit overriddenSettingsChanged();
}

QVariantMap StreamingPreferences::pendingHostOverrides() const
{
    QVariantMap current = serialize();
    QVariantMap overrides;

    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        if (isAppWideKey(it.key())) {
            continue;
        }

        if (it.value() != m_LoadedValues.value(it.key())) {
            // Changed by the user: an override unless it equals the global value
            if (it.value() != m_GlobalValues.value(it.key())) {
                overrides.insert(it.key(), it.value());
            }
        }
        else if (m_OverriddenKeys.contains(it.key())) {
            // Unchanged: keep the override, even if the global value
            // has since become equal to it
            overrides.insert(it.key(), it.value());
        }
    }

    return overrides;
}

StreamingPreferences* StreamingPreferences::get(QQmlEngine *qmlEngine)
{
    {
        QReadLocker readGuard(s_GlobalPrefsLock);

        // If we have a preference object and it's associated with a QML engine or
        // if the caller didn't specify a QML engine, return the existing object.
        if (s_GlobalPrefs && (s_GlobalPrefs->m_QmlEngine || !qmlEngine)) {
            // The lifetime logic here relies on the QML engine also being a singleton.
            Q_ASSERT(!qmlEngine || s_GlobalPrefs->m_QmlEngine == qmlEngine);
            return s_GlobalPrefs;
        }
    }

    {
        QWriteLocker writeGuard(s_GlobalPrefsLock);

        // If we already have an preference object but the QML engine is now available,
        // associate the QML engine with the preferences.
        if (s_GlobalPrefs) {
            if (!s_GlobalPrefs->m_QmlEngine) {
                s_GlobalPrefs->m_QmlEngine = qmlEngine;
            }
            else {
                // We could reach this codepath if another thread raced with us
                // and created the object while we were outside the pref lock.
                Q_ASSERT(!qmlEngine || s_GlobalPrefs->m_QmlEngine == qmlEngine);
            }
        }
        else {
            s_GlobalPrefs = new StreamingPreferences(qmlEngine);
        }

        return s_GlobalPrefs;
    }
}

void StreamingPreferences::reload()
{
    QSettings settings;

    // Resolve each setting from the host's overrides if present, otherwise
    // from the global settings
    m_OverriddenKeys.clear();
    auto value = [&](const QString& key, const QVariant& defaultValue) {
        if (!m_HostUuid.isEmpty() && !isAppWideKey(key)) {
            QString hostKey = hostSettingsGroup(m_HostUuid) + "/" + key;
            if (settings.contains(hostKey)) {
                m_OverriddenKeys.insert(key);
                return settings.value(hostKey);
            }
        }
        return settings.value(key, defaultValue);
    };

    int defaultVer = settings.value(SER_DEFAULTVER, 0).toInt();

#ifdef Q_OS_DARWIN
    recommendedFullScreenMode = WindowMode::WM_FULLSCREEN_DESKTOP;
#else
    // Wayland doesn't support modesetting, so use fullscreen desktop mode
    // unless we have a slow GPU (which can take advantage of wp_viewporter
    // to reduce GPU load with lower resolution video streams).
    if (WMUtils::isRunningWayland() && !WMUtils::isGpuSlow()) {
        recommendedFullScreenMode = WindowMode::WM_FULLSCREEN_DESKTOP;
    }
    else {
        recommendedFullScreenMode = WindowMode::WM_FULLSCREEN;
    }
#endif

    width = value(SER_WIDTH, 1920).toInt();
    height = value(SER_HEIGHT, 1080).toInt();
    fps = value(SER_FPS, 60).toInt();
    enableYUV444 = value(SER_YUV444, false).toBool();
    bitrateKbps = value(SER_BITRATE, getDefaultBitrate(width, height, fps, enableYUV444)).toInt();
    unlockBitrate = value(SER_UNLOCK_BITRATE, false).toBool();
    autoAdjustBitrate = value(SER_AUTOADJUSTBITRATE, true).toBool();
    enableVsync = value(SER_VSYNC, true).toBool();
    gameOptimizations = value(SER_GAMEOPTS, true).toBool();
    playAudioOnHost = value(SER_HOSTAUDIO, false).toBool();
    multiController = value(SER_MULTICONT, true).toBool();
    enableMdns = value(SER_MDNS, true).toBool();
    quitAppAfter = value(SER_QUITAPPAFTER, false).toBool();
    absoluteMouseMode = value(SER_ABSMOUSEMODE, false).toBool();
    absoluteTouchMode = value(SER_ABSTOUCHMODE, true).toBool();
    framePacing = value(SER_FRAMEPACING, false).toBool();
    connectionWarnings = value(SER_CONNWARNINGS, true).toBool();
    configurationWarnings = value(SER_CONFWARNINGS, true).toBool();
    richPresence = value(SER_RICHPRESENCE, true).toBool();
    gamepadMouse = value(SER_GAMEPADMOUSE, true).toBool();
    detectNetworkBlocking = value(SER_DETECTNETBLOCKING, true).toBool();
    showPerformanceOverlay = value(SER_SHOWPERFOVERLAY, false).toBool();
    packetSize = value(SER_PACKETSIZE, 0).toInt();
    swapMouseButtons = value(SER_SWAPMOUSEBUTTONS, false).toBool();
    muteOnFocusLoss = value(SER_MUTEONFOCUSLOSS, false).toBool();
    backgroundGamepad = value(SER_BACKGROUNDGAMEPAD, false).toBool();
    reverseScrollDirection = value(SER_REVERSESCROLL, false).toBool();
    swapFaceButtons = value(SER_SWAPFACEBUTTONS, false).toBool();
    keepAwake = value(SER_KEEPAWAKE, true).toBool();
    enableHdr = value(SER_HDR, false).toBool();
    captureSysKeysMode = static_cast<CaptureSysKeysMode>(value(SER_CAPTURESYSKEYS,
                                                         static_cast<int>(CaptureSysKeysMode::CSK_OFF)).toInt());
    audioConfig = static_cast<AudioConfig>(value(SER_AUDIOCFG,
                                                  static_cast<int>(AudioConfig::AC_STEREO)).toInt());
    videoCodecConfig = static_cast<VideoCodecConfig>(value(SER_VIDEOCFG,
                                                  static_cast<int>(VideoCodecConfig::VCC_AUTO)).toInt());
    videoDecoderSelection = static_cast<VideoDecoderSelection>(value(SER_VIDEODEC,
                                                  static_cast<int>(VideoDecoderSelection::VDS_AUTO)).toInt());
    rendererSelection = static_cast<RendererSelection>(value(SER_RENDERER,
                                                  static_cast<int>(RendererSelection::RS_AUTO)).toInt());
    windowMode = static_cast<WindowMode>(value(SER_WINDOWMODE,
                                                        // Try to load from the old preference value too
                                                        static_cast<int>(settings.value(SER_FULLSCREEN, true).toBool() ?
                                                                             recommendedFullScreenMode : WindowMode::WM_WINDOWED)).toInt());
    uiDisplayMode = static_cast<UIDisplayMode>(value(SER_UIDISPLAYMODE,
                                               static_cast<int>(settings.value(SER_STARTWINDOWED, true).toBool() ? UIDisplayMode::UI_WINDOWED
                                                                                                                 : UIDisplayMode::UI_MAXIMIZED)).toInt());
    language = static_cast<Language>(value(SER_LANGUAGE,
                                                    static_cast<int>(Language::LANG_AUTO)).toInt());


    // Perform default settings updates as required based on last default version
    if (defaultVer < 1) {
#ifdef Q_OS_DARWIN
        // Update window mode setting on macOS from full-screen (old default) to borderless windowed (new default)
        if (windowMode == WindowMode::WM_FULLSCREEN) {
            windowMode = WindowMode::WM_FULLSCREEN_DESKTOP;
        }
#endif
    }
    if (defaultVer < 2) {
        if (windowMode == WindowMode::WM_FULLSCREEN && WMUtils::isRunningWayland()) {
            windowMode = WindowMode::WM_FULLSCREEN_DESKTOP;
        }
    }

    // Fixup VCC value to the new settings format with codec and HDR separate
    if (videoCodecConfig == VCC_FORCE_HEVC_HDR_DEPRECATED) {
        videoCodecConfig = VCC_AUTO;
        enableHdr = true;
    }

    m_LoadedValues = serialize();
}

bool StreamingPreferences::retranslate()
{
    static QTranslator* translator = nullptr;

#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
    if (m_QmlEngine != nullptr) {
        // Dynamic retranslation is not supported until Qt 5.10
        return false;
    }
#endif

    QTranslator* newTranslator = new QTranslator();
    QString languageSuffix = getSuffixFromLanguage(language);

    // Remove the old translator, even if we can't load a new one.
    // Otherwise we'll be stuck with the old translated values instead
    // of defaulting to English.
    if (translator != nullptr) {
        QCoreApplication::removeTranslator(translator);
        delete translator;
        translator = nullptr;
    }

    if (newTranslator->load(QString(":/languages/qml_") + languageSuffix)) {
        qInfo() << "Successfully loaded translation for" << languageSuffix;

        translator = newTranslator;
        QCoreApplication::installTranslator(translator);
    }
    else {
        qInfo() << "No translation available for" << languageSuffix;
        delete newTranslator;
    }

    if (m_QmlEngine != nullptr) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        // This is a dynamic retranslation from the settings page.
        // We have to kick the QML engine into reloading our text.
        m_QmlEngine->retranslate();
#else
        // Unreachable below Qt 5.10 due to the check above
        Q_ASSERT(false);
#endif
    }
    else {
        // This is a translation from a non-QML context, which means
        // it is probably app startup. There's nothing to refresh.
    }

    return true;
}

QString StreamingPreferences::getSuffixFromLanguage(StreamingPreferences::Language lang)
{
    switch (lang)
    {
    case LANG_DE:
        return "de";
    case LANG_EN:
        return "en";
    case LANG_FR:
        return "fr";
    case LANG_ZH_CN:
        return "zh_CN";
    case LANG_NB_NO:
        return "nb_NO";
    case LANG_RU:
        return "ru";
    case LANG_ES:
        return "es";
    case LANG_JA:
        return "ja";
    case LANG_VI:
        return "vi";
    case LANG_TH:
        return "th";
    case LANG_KO:
        return "ko";
    case LANG_HU:
        return "hu";
    case LANG_NL:
        return "nl";
    case LANG_SV:
        return "sv";
    case LANG_TR:
        return "tr";
    case LANG_UK:
        return "uk";
    case LANG_ZH_TW:
        return "zh_TW";
    case LANG_PT:
        return "pt";
    case LANG_PT_BR:
        return "pt_BR";
    case LANG_EL:
        return "el";
    case LANG_IT:
        return "it";
    case LANG_HI:
        return "hi";
    case LANG_PL:
        return "pl";
    case LANG_CS:
        return "cs";
    case LANG_HE:
        return "he";
    case LANG_CKB:
        return "ckb";
    case LANG_LT:
        return "lt";
    case LANG_ET:
        return "et";
    case LANG_BG:
        return "bg";
    case LANG_EO:
        return "eo";
    case LANG_TA:
        return "ta";
    case LANG_AUTO:
    default:
        return QLocale::system().name();
    }
}

void StreamingPreferences::save()
{
    QSettings settings;
    QVariantMap values = serialize();

    if (m_HostUuid.isEmpty()) {
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            settings.setValue(it.key(), it.value());
        }
        settings.setValue(SER_DEFAULTVER, CURRENT_DEFAULT_VER);
    }
    else {
        // Don't write anything unless the user changed something
        if (values != m_LoadedValues) {
            QVariantMap overrides = pendingHostOverrides();
            settings.remove(hostSettingsGroup(m_HostUuid));
            settings.beginGroup(hostSettingsGroup(m_HostUuid));
            m_OverriddenKeys.clear();
            for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
                settings.setValue(it.key(), it.value());
                m_OverriddenKeys.insert(it.key());
            }
            settings.endGroup();
        }
    }

    m_LoadedValues = values;
}

QVariantMap StreamingPreferences::serialize() const
{
    QVariantMap values;

    for (const auto& setting : k_Settings) {
        QMetaProperty property = metaObject()->property(metaObject()->indexOfProperty(setting.property));
        QVariant value = property.read(this);

        // Enums are stored as integers
        values[setting.key] = property.isEnumType() ? value.toInt() : value;
    }

    // Not exposed as a property
    values[SER_PACKETSIZE] = packetSize;

    return values;
}

int StreamingPreferences::getDefaultBitrate(int width, int height, int fps, bool yuv444)
{
    // Don't scale bitrate linearly beyond 60 FPS. It's definitely not a linear
    // bitrate increase for frame rate once we get to values that high.
    float frameRateFactor = (fps <= 60 ? fps : (qSqrt(fps / 60.f) * 60.f)) / 30.f;

    // TODO: Collect some empirical data to see if these defaults make sense.
    // We're just using the values that the Shield used, as we have for years.
    static const struct resTable {
        int pixels;
        int factor;
    } resTable[] {
        { 640 * 360, 1 },
        { 854 * 480, 2 },
        { 1280 * 720, 5 },
        { 1920 * 1080, 10 },
        { 2560 * 1440, 20 },
        { 3840 * 2160, 40 },
        { -1, -1 },
    };

    // Calculate the resolution factor by linear interpolation of the resolution table
    float resolutionFactor;
    int pixels = width * height;
    for (int i = 0;; i++) {
        if (pixels == resTable[i].pixels) {
            // We can bail immediately for exact matches
            resolutionFactor = resTable[i].factor;
            break;
        }
        else if (pixels < resTable[i].pixels) {
            if (i == 0) {
                // Never go below the lowest resolution entry
                resolutionFactor = resTable[i].factor;
            }
            else {
                // Interpolate between the entry greater than the chosen resolution (i) and the entry less than the chosen resolution (i-1)
                resolutionFactor = ((float)(pixels - resTable[i-1].pixels) / (resTable[i].pixels - resTable[i-1].pixels)) * (resTable[i].factor - resTable[i-1].factor) + resTable[i-1].factor;
            }
            break;
        }
        else if (resTable[i].pixels == -1) {
            // Never go above the highest resolution entry
            resolutionFactor = resTable[i-1].factor;
            break;
        }
    }

    if (yuv444) {
        // This is rough estimation based on the fact that 4:4:4 doubles the amount of raw YUV data compared to 4:2:0
        resolutionFactor *= 2;
    }

    return qRound(resolutionFactor * frameRateFactor) * 1000;
}
