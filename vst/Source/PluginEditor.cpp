#include "PluginEditor.h"
#include "BinaryData.h"

using namespace epiano;

namespace
{
const juce::Colour kBackground { 0xff0a0b0d };

const char* mimeForExtension (const juce::String& ext)
{
    if (ext == "html") return "text/html";
    if (ext == "js")   return "text/javascript";
    if (ext == "css")  return "text/css";
    if (ext == "svg")  return "image/svg+xml";
    if (ext == "json") return "application/json";
    if (ext == "png")  return "image/png";
    return "application/octet-stream";
}

std::vector<std::byte> toBytes (const void* data, size_t size)
{
    const auto* p = static_cast<const std::byte*> (data);
    return { p, p + size };
}

juce::var makeObject (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto* obj = new juce::DynamicObject();
    for (const auto& [k, v] : props) obj->setProperty (k, v);
    return juce::var (obj);
}
} // namespace

//==============================================================================
EPianoEditor::EPianoEditor (EPianoProcessor& p)
    : AudioProcessorEditor (p), processor (p), browser (makeBrowserOptions())
{
    lastSent.fill (std::numeric_limits<float>::quiet_NaN());

    addAndMakeVisible (browser);
    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    setResizeLimits (640, 560, 2400, 2200);
    setSize (1000, 980);
    startTimerHz (30);
}

EPianoEditor::~EPianoEditor()
{
    stopTimer();
    // Close any knob gestures the UI didn't get to finish.
    for (int i = 0; i < kNumParams; ++i)
        if (inGesture[(size_t) i]) processor.param (i)->endChangeGesture();
}

void EPianoEditor::paint (juce::Graphics& g) { g.fillAll (kBackground); }
void EPianoEditor::resized() { browser.setBounds (getLocalBounds()); }

//==============================================================================
juce::WebBrowserComponent::Options EPianoEditor::makeBrowserOptions()
{
    using Opts = juce::WebBrowserComponent::Options;

    // WebView2 needs a writable data folder; the default (next to the DAW's exe) usually isn't.
    const auto userData = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("EPianoVST-WebView2");

    return Opts{}
        .withBackend (Opts::Backend::webview2)
        .withWinWebView2Options (Opts::WinWebView2{}
                                     .withUserDataFolder (userData)
                                     .withBackgroundColour (kBackground)
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
        .withEventListener ("uiReady",    [this] (const juce::var&) { onUiReady(); })
        .withEventListener ("setParam",   [this] (const juce::var& e) { onSetParam (e); })
        .withEventListener ("gesture",    [this] (const juce::var& e) { onGesture (e); })
        .withEventListener ("noteOn", [this] (const juce::var& e)
        {
            const int note = juce::jlimit (0, 127, (int) e["note"]);
            const float vel = juce::jlimit (1, 127, (int) e["vel"]) / 127.0f;
            processor.keyboardState.noteOn (1, note, vel);
        })
        .withEventListener ("noteOff", [this] (const juce::var& e)
        {
            processor.keyboardState.noteOff (1, juce::jlimit (0, 127, (int) e["note"]), 0.0f);
        })
        .withEventListener ("panic", [this] (const juce::var&)
        {
            processor.keyboardState.allNotesOff (0);
            processor.requestPanic();
        })
        .withEventListener ("modWheel", [this] (const juce::var& e) { processor.setUiModWheel ((float) e["value"]); })
        .withEventListener ("loadPreset", [this] (const juce::var& e)
        {
            juce::String error;
            if (! processor.loadPresetById (e["id"].toString(), error)) sendMessage (error);
            sendPreset();
        })
        .withEventListener ("savePreset", [this] (const juce::var& e)
        {
            const auto name = e["name"].toString().trim();
            bool exists = false;
            juce::String error;
            const bool ok = processor.saveUserPreset (name, (bool) e["overwrite"], exists, error);
            browser.emitEventIfBrowserIsVisible ("saveResult", makeObject ({
                { "ok", ok }, { "exists", exists }, { "name", name }, { "error", error } }));
            if (ok) { sendPresetList(); sendPreset(); }
        })
        .withEventListener ("deletePreset", [this] (const juce::var& e)
        {
            juce::String error;
            if (processor.deleteUserPreset (e["id"].toString(), error)) sendMessage ("Preset moved to the Recycle Bin");
            else sendMessage (error);
            sendPresetList();
            sendPreset();
        })
        .withEventListener ("loadPresetFile", [this] (const juce::var&) { chooseAndLoadPresetFile(); })
        // right-click on a control: the host's own parameter menu (FL Studio: Create automation clip,
        // Link to controller...). Hosts without one get a small fallback menu.
        .withEventListener ("paramMenu", [this] (const juce::var& e) { showParamMenu (e); })
        // ---- oscillator samples
        .withEventListener ("loadSample", [this] (const juce::var& e) { chooseSample ((int) e["osc"]); })
        .withEventListener ("dropSample", [this] (const juce::var& e)
        {
            const int osc = (int) e["osc"];
            juce::MemoryOutputStream bytes;
            juce::String msg;
            if (! juce::Base64::convertFromBase64 (bytes, e["data"].toString()))
                msg = "Couldn't read the dropped file";
            else if (processor.loadSampleMemory (osc, bytes.getData(), bytes.getDataSize(), e["name"].toString(), msg) && msg.isEmpty())
                msg = "Loaded " + e["name"].toString() + " into OSC " + juce::String (osc + 1);
            sendMessage (msg);
        })
        .withEventListener ("clearSample", [this] (const juce::var& e) { processor.setSample ((int) e["osc"], nullptr); })
        .withEventListener ("openPresetFolder", [] (const juce::var&)
        {
            const auto folder = PresetManager::userFolder();
            folder.createDirectory();
            folder.startAsProcess();
        });
}

void EPianoEditor::chooseAndLoadPresetFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load E-Piano preset", PresetManager::userFolder(),
                                                        juce::String ("*") + PresetManager::fileExtension);
    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync (chooserFlags, [safeThis = juce::Component::SafePointer<EPianoEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr) return;
        const auto file = fc.getResult();
        if (file == juce::File()) return; // cancelled
        juce::String error;
        if (safeThis->processor.loadPresetFile (file, error)) safeThis->sendMessage ("Loaded " + file.getFileName());
        else safeThis->sendMessage (error);
        safeThis->sendPreset();
    });
}

void EPianoEditor::chooseSample (int osc)
{
    if (osc < 0 || osc >= kNumOsc) return;
    fileChooser = std::make_unique<juce::FileChooser> ("Load a sample into OSC " + juce::String (osc + 1),
                                                        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                        "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync (chooserFlags, [safeThis = juce::Component::SafePointer<EPianoEditor> (this), osc] (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr) return;
        const auto file = fc.getResult();
        if (file == juce::File()) return;
        juce::String msg;
        if (safeThis->processor.loadSampleFile (osc, file, msg) && msg.isEmpty())
            msg = "Loaded " + file.getFileName() + " into OSC " + juce::String (osc + 1);
        safeThis->sendMessage (msg);
    });
}

void EPianoEditor::sendSamples()
{
    lastSampleVersion = processor.sampleVersion.load();
    juce::Array<juce::var> slots;
    for (int k = 0; k < kNumOsc; ++k)
    {
        if (auto s = processor.getSample (k))
            slots.add (makeObject ({ { "name", s->name }, { "root", s->root }, { "seconds", s->seconds() },
                                     { "peaks", samplePeaks (*s, 100) } }));
        else
            slots.add (juce::var());
    }
    browser.emitEventIfBrowserIsVisible ("samples", makeObject ({ { "slots", slots } }));
}

void EPianoEditor::showParamMenu (const juce::var& e)
{
    const int i = indexForId (e["id"].toString());
    if (i < 0) return;
    auto* p = processor.param (i);
    const juce::Point<int> pos ((int) e["x"], (int) e["y"]);

    if (auto* host = getHostContext())
        if (auto menu = host->getContextMenuForParameter (p))
        {
            menu->showNativeMenu (pos);
            return;
        }

    // No host menu (e.g. Standalone): minimal fallback
    juce::PopupMenu m;
    m.addSectionHeader (p->getName (64) + ": " + p->getCurrentValueAsText());
    m.addItem (1, "Reset to default");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                .withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> (pos.x, pos.y, 1, 1))),
                     [safeThis = juce::Component::SafePointer<EPianoEditor> (this), i] (int result)
                     {
                         if (safeThis == nullptr || result != 1) return;
                         auto* param = safeThis->processor.param (i);
                         param->beginChangeGesture();
                         param->setValueNotifyingHost (param->getDefaultValue());
                         param->endChangeGesture();
                     });
}

void EPianoEditor::sendMessage (const juce::String& text)
{
    browser.emitEventIfBrowserIsVisible ("message", makeObject ({ { "text", text } }));
}

void EPianoEditor::sendPresetList()
{
    auto& pm = processor.presetManager;
    pm.rescanIfChanged();

    juce::Array<juce::var> list;
    const auto& factory = pm.factory();
    for (size_t i = 0; i < factory.size(); ++i)
        list.add (makeObject ({ { "id", "factory:" + juce::String ((int) i) }, { "name", factory[i].name },
                                { "category", factory[i].category }, { "factory", true } }));
    for (const auto& e : pm.userPresets())
        list.add (makeObject ({ { "id", e.id }, { "name", e.name }, { "category", e.category }, { "factory", false } }));

    browser.emitEventIfBrowserIsVisible ("presetList", makeObject ({
        { "presets", list }, { "folder", PresetManager::userFolder().getFullPathName() } }));
}

void EPianoEditor::sendPreset()
{
    const auto info = processor.currentPreset();
    lastPresetVersion = processor.presetVersion.load();
    browser.emitEventIfBrowserIsVisible ("preset", makeObject ({
        { "id", info.id }, { "name", info.name }, { "category", info.category } }));
}

std::optional<juce::WebBrowserComponent::Resource> EPianoEditor::getResource (const juce::String& url)
{
    auto path = url.upToFirstOccurrenceOf ("?", false, false).trimCharactersAtStart ("/");
    if (path.isEmpty()) path = "index.html";
    const auto fileName = path.fromLastOccurrenceOf ("/", false, false);
    const auto mime = mimeForExtension (fileName.fromLastOccurrenceOf (".", false, false).toLowerCase());

    // Dev mode: set EPIANO_UI_DIR to the vst/ui folder to load the UI from disk
    // (edit HTML/CSS/JS and just reopen the editor, no rebuild needed).
    const auto devDir = juce::SystemStats::getEnvironmentVariable ("EPIANO_UI_DIR", {});
    if (devDir.isNotEmpty())
    {
        const auto f = juce::File (devDir).getChildFile (path);
        juce::MemoryBlock mb;
        if (f.existsAsFile() && f.loadFileAsData (mb))
            return juce::WebBrowserComponent::Resource { toBytes (mb.getData(), mb.getSize()), mime };
    }

    // Embedded UI. BinaryData is keyed by file name (all UI file names are unique).
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (fileName == BinaryData::originalFilenames[i])
        {
            int size = 0;
            if (const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                return juce::WebBrowserComponent::Resource { toBytes (data, (size_t) size), mime };
        }
    }
    return std::nullopt;
}

//==============================================================================
int EPianoEditor::indexForId (const juce::String& id) const
{
    for (int i = 0; i < kNumParams; ++i)
        if (id == paramDefs()[(size_t) i].id) return i;
    return -1;
}

void EPianoEditor::onUiReady()
{
    uiReady = true;
    lastSent.fill (std::numeric_limits<float>::quiet_NaN()); // force a full resend
    lastNotes.reset();
    sendConfig();
    sendPresetList();
    sendPreset();
    sendSamples();
    timerCallback();
}

void EPianoEditor::onSetParam (const juce::var& e)
{
    const int i = indexForId (e["id"].toString());
    if (i < 0) return;
    auto* p = processor.param (i);
    const float v = (float) (double) e["value"];
    const bool wrap = ! inGesture[(size_t) i];   // wheel / keys / double-click: one-shot gesture
    if (wrap) p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (v));
    if (wrap) p->endChangeGesture();
    lastSent[(size_t) i] = processor.paramValue (i); // don't echo it back to the UI
}

void EPianoEditor::onGesture (const juce::var& e)
{
    const int i = indexForId (e["id"].toString());
    if (i < 0) return;
    const bool begin = (bool) e["begin"];
    if (begin == inGesture[(size_t) i]) return;
    inGesture[(size_t) i] = begin;
    if (begin) processor.param (i)->beginChangeGesture();
    else       processor.param (i)->endChangeGesture();
}

void EPianoEditor::sendConfig()
{
    const auto wrapper = juce::String (juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType));
    browser.emitEventIfBrowserIsVisible ("config", makeObject ({
        { "sampleRate", processor.getSampleRate() },
        { "blockSize",  processor.getBlockSize() },
        { "wrapper",    wrapper },
        { "voices",     kNumVoices },
    }));
}

void EPianoEditor::timerCallback()
{
    if (! uiReady) return;

    // Parameter changes (host automation, preset loads, state restore)
    auto* changed = new juce::DynamicObject();
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = processor.paramValue (i);
        if (v != lastSent[(size_t) i]) // NaN != anything -> always sent after uiReady
        {
            changed->setProperty (paramDefs()[(size_t) i].id, v);
            lastSent[(size_t) i] = v;
        }
    }
    juce::var changedVar (changed);
    if (changed->getProperties().size() > 0)
        browser.emitEventIfBrowserIsVisible ("params", changedVar);

    // Preset changed outside the UI (host program change, project reload)
    if (processor.presetVersion.load() != lastPresetVersion)
        sendPreset();

    // Samples loaded / cleared (UI, preset, project reload); free buffers the audio thread retired
    processor.collectRetiredSamples();
    if (processor.sampleVersion.load() != lastSampleVersion)
        sendSamples();

    // Autoload: pick up preset files added/removed/edited in the preset folder (every ~2 s)
    if (++ticks % 60 == 0 && processor.presetManager.rescanIfChanged())
        sendPresetList();

    // Sounding notes (host MIDI + UI keyboard)
    juce::Array<juce::var> on, off;
    for (int n = 0; n < 128; ++n)
    {
        const bool down = processor.keyboardState.isNoteOnForChannels (0xffff, n);
        if (down != lastNotes[(size_t) n])
        {
            lastNotes[(size_t) n] = down;
            (down ? on : off).add (n);
        }
    }
    if (! on.isEmpty() || ! off.isEmpty())
        browser.emitEventIfBrowserIsVisible ("notes", makeObject ({ { "on", on }, { "off", off } }));

    browser.emitEventIfBrowserIsVisible ("stats", makeObject ({
        { "voices",  processor.statVoices.load() },
        { "peak",    processor.statPeak.exchange (0.0f) },
        { "sustain", processor.statSustain.load() },
        { "modWheel", processor.statModWheel.load() },
        { "bpm",     processor.statBpm.load() },
    }));
}
