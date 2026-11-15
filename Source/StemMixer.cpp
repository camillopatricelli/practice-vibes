#include "StemMixer.h"
#include "Theme.h"

#include <thread>

namespace pv
{

//==============================================================================
struct StemService::Job
{
    std::shared_ptr<const LoadedSong> song;
    juce::File folder;

    enum Phase { downloading, starting, separating };
    std::atomic<int> phase { downloading };
    std::atomic<double> progress { 0.0 };
    std::atomic<juce::int64> downloaded { 0 }, total { 0 };
    std::atomic<bool> cancel { false }, done { false }, downloadDone { false }, downloadOk { false };
    juce::Result result = juce::Result::ok();     // written before `done`
    std::thread worker;
};

class StemService::Download final : private juce::URL::DownloadTaskListener
{
public:
    Download (std::shared_ptr<Job> j, const juce::File& target) : job (std::move (j))
    {
        target.getParentDirectory().createDirectory();
        target.deleteFile();
        task = StemSeparator::getModelUrl().downloadToFile (target, juce::URL::DownloadTaskOptions().withListener (this));

        if (task == nullptr)
        {
            job->downloadOk = false;
            job->downloadDone = true;
        }
    }

    ~Download() override        { task.reset(); }

private:
    void finished (juce::URL::DownloadTask* t, bool success) override
    {
        job->downloadOk = success && ! t->hadError() && t->statusCode() < 400;
        job->downloadDone = true;
    }

    void progress (juce::URL::DownloadTask*, juce::int64 bytes, juce::int64 total) override
    {
        job->downloaded = bytes;
        job->total = total > 0 ? total : StemSeparator::modelBytes;
    }

    std::shared_ptr<Job> job;
    std::unique_ptr<juce::URL::DownloadTask> task;
};

StemService::StemService() = default;

StemService::~StemService()
{
    alive.reset();
    download.reset();

    if (job != nullptr)
    {
        job->cancel = true;

        if (job->worker.joinable())
            job->worker.join();     // the separator checks `cancel` every few seconds
    }
}

static juce::File partialModelFile()   { return StemSeparator::getModelFile().withFileExtension ("part"); }

bool StemService::start (std::shared_ptr<const LoadedSong> song, const juce::File& folder)
{
    if (job != nullptr || song == nullptr || folder == juce::File())
        return false;

    job = std::make_shared<Job>();
    job->song = std::move (song);
    job->folder = folder;
    lastError.clear();
    juce::Logger::writeToLog ("Stems: separating \"" + job->song->name + "\" into " + folder.getFullPathName());

    if (StemSeparator::hasModel())
    {
        runSeparation();
    }
    else
    {
        juce::Logger::writeToLog ("Stems: downloading the model from " + StemSeparator::getModelUrl().toString (false));
        download = std::make_unique<Download> (job, partialModelFile());
    }

    startTimerHz (10);
    return true;
}

void StemService::cancel()
{
    if (job == nullptr)
        return;

    job->cancel = true;

    if (job->phase == Job::downloading)
    {
        download.reset();
        partialModelFile().deleteFile();
        finish (juce::Result::fail ("cancelled"));
    }
}

juce::File StemService::getBusyFolder() const       { return job != nullptr ? job->folder : juce::File(); }
juce::String StemService::getBusySongName() const   { return job != nullptr ? job->song->name : juce::String(); }

double StemService::getProgress() const
{
    if (job == nullptr)
        return 0.0;

    if (job->phase == Job::downloading)
        return job->total > 0 ? (double) job->downloaded / (double) job->total : 0.0;

    return job->phase == Job::starting ? 0.0 : job->progress.load();
}

juce::String StemService::getStatus() const
{
    if (job == nullptr)
        return {};

    auto mb = [] (juce::int64 bytes) { return juce::String (juce::roundToInt ((double) bytes / (1024.0 * 1024.0))); };

    switch (job->phase.load())
    {
        case Job::downloading:
            return "Downloading the separation model (first time only): " + mb (job->downloaded) + " of "
                   + mb (job->total > 0 ? job->total.load() : StemSeparator::modelBytes) + " MB";
        case Job::starting:
            return "Starting the separation...";
        default:
            return "Separating \"" + job->song->name + "\": " + juce::String (juce::roundToInt (job->progress * 100.0)) + " %";
    }
}

void StemService::timerCallback()
{
    if (job == nullptr)
    {
        stopTimer();
        return;
    }

    if (job->phase == Job::downloading && job->downloadDone)
    {
        download.reset();
        const auto part = partialModelFile();

        if (job->downloadOk && part.getSize() == StemSeparator::modelBytes
            && (StemSeparator::getModelFile().deleteFile(), part.moveFileTo (StemSeparator::getModelFile())))
        {
            juce::Logger::writeToLog ("Stems: model downloaded");
            runSeparation();
        }
        else
        {
            part.deleteFile();
            finish (juce::Result::fail ("couldn't download the separation model (check the internet connection)"));
        }

        return;
    }

    if (job->phase != Job::downloading && job->done)
    {
        if (job->worker.joinable())
            job->worker.join();

        finish (job->result);
    }
}

void StemService::runSeparation()
{
    job->phase = Job::starting;
    auto j = job;

    job->worker = std::thread ([j]
    {
        juce::String error;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        auto model = StemSeparator::openModel (error);
        auto result = juce::Result::fail (error);

        if (model != nullptr && ! j->cancel)
        {
            j->phase = Job::separating;
            result = StemSeparator::separateToFolder (j->song->audio, j->song->sampleRate, j->folder, j->song->name, *model,
                                                      [j] (float p) { j->progress = p; return ! j->cancel.load(); });
        }
        else if (j->cancel)
        {
            result = juce::Result::fail ("cancelled");
        }

        if (result.wasOk())
            juce::Logger::writeToLog ("Stems: done in " + juce::String (juce::roundToInt ((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0))
                                      + " s for " + juce::String (juce::roundToInt (j->song->getLengthSeconds())) + " s of audio");

        j->result = result;
        j->done = true;
    });
}

void StemService::finish (juce::Result result)
{
    stopTimer();
    const auto folder = job != nullptr ? job->folder : juce::File();

    if (result.failed())
    {
        lastError = result.getErrorMessage() == "cancelled" ? juce::String() : result.getErrorMessage();
        juce::Logger::writeToLog ("Stems: " + result.getErrorMessage());

        if (folder != juce::File() && ! Stems::isComplete (folder))
            folder.deleteRecursively();     // nothing half-written left behind
    }

    job.reset();

    if (onFinished)
        onFinished (folder, result);
}

//==============================================================================
float StemControls::gainFor (int stem) const
{
    const auto anySolo = std::any_of (solo.begin(), solo.end(), [] (bool b) { return b; });

    if (anySolo)
        return solo[(size_t) stem] ? volume[(size_t) stem] : 0.0f;

    return mute[(size_t) stem] ? 0.0f : volume[(size_t) stem];
}

bool StemControls::isNeutral() const
{
    for (int s = 0; s < Stems::count; ++s)
        if (gainFor (s) != 1.0f)
            return false;

    return true;
}

juce::String StemControls::describe() const
{
    if (isNeutral())
        return {};

    juce::StringArray soloed, muted;

    for (auto s : Stems::displayOrder)
    {
        if (solo[(size_t) s])
            soloed.add (Stems::fileStem (s));
        else if (mute[(size_t) s])
            muted.add (Stems::fileStem (s));
    }

    if (! soloed.isEmpty())
        return soloed.joinIntoString (" + ") + " solo";

    if (! muted.isEmpty())
        return muted.joinIntoString (", ") + " off";

    return "custom mix";
}

//==============================================================================
StemMixer::StemMixer (Host& h) : host (h)
{
    title.setFont (theme::headerFont());
    title.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (title);

    message.setJustificationType (juce::Justification::topLeft);
    message.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
    addAndMakeVisible (message);
    addChildComponent (progressBar);
    addChildComponent (actionButton);

    resetButton.onClick = [this]
    {
        host.getStemControls().reset();
        syncFromControls();
        host.stemControlsChanged();
    };
    addChildComponent (resetButton);

    for (int i = 0; i < Stems::count; ++i)
    {
        auto row = std::make_unique<Row>();
        row->stem = Stems::displayOrder[(size_t) i];
        const auto stem = row->stem;
        row->name.setText (Stems::displayName (stem), juce::dontSendNotification);
        row->name.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));

        for (auto* b : { &row->mute, &row->solo })
        {
            b->setClickingTogglesState (true);
            b->setTooltip (b == &row->mute ? "Mute" : "Solo (only the soloed stems play)");
        }

        row->mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc04848));
        row->solo.setColour (juce::TextButton::buttonOnColourId, theme::highlight.darker (0.2f));
        row->solo.setColour (juce::TextButton::textColourOnId, juce::Colours::black);

        row->mute.onClick = [this, stem, r = row.get()]
        {
            host.getStemControls().mute[(size_t) stem] = r->mute.getToggleState();
            host.stemControlsChanged();
        };
        row->solo.onClick = [this, stem, r = row.get()]
        {
            host.getStemControls().solo[(size_t) stem] = r->solo.getToggleState();
            host.stemControlsChanged();
        };

        row->volume.setRange (0.0, 1.5, 0.01);
        row->volume.setDoubleClickReturnValue (true, 1.0);
        row->volume.setTooltip ("Volume (double-click: back to 100 %)");
        row->volume.onValueChange = [this, stem, r = row.get()]
        {
            host.getStemControls().volume[(size_t) stem] = (float) r->volume.getValue();
            host.stemControlsChanged();
        };

        for (juce::Component* c : { (juce::Component*) &row->name, (juce::Component*) &row->mute, (juce::Component*) &row->solo, (juce::Component*) &row->volume })
            addChildComponent (c);

        rows[(size_t) i] = std::move (row);
    }

    syncFromControls();
    setSize (480, 92 + Stems::count * 42);
    update();
    startTimerHz (5);
}

StemMixer::~StemMixer() = default;

void StemMixer::syncFromControls()
{
    const auto& controls = host.getStemControls();

    for (auto& row : rows)
    {
        row->mute.setToggleState (controls.mute[(size_t) row->stem], juce::dontSendNotification);
        row->solo.setToggleState (controls.solo[(size_t) row->stem], juce::dontSendNotification);
        row->volume.setValue (controls.volume[(size_t) row->stem], juce::dontSendNotification);
    }
}

void StemMixer::timerCallback()
{
    update();
}

void StemMixer::update()
{
    auto& service = host.getStemService();
    const auto folder = host.getStemFolder();
    const auto busyHere = service.isBusy() && service.getBusyFolder() == folder;

    Mode newMode;

    if (host.getPlayer().hasStems())
        newMode = Mode::mixer;
    else if (service.isBusy())
        newMode = Mode::busy;
    else if (host.isLoadingStems())
        newMode = Mode::loading;
    else if (folder != juce::File() && StemSeparator::isSupported())
        newMode = Mode::offer;
    else
        newMode = Mode::unavailable;

    if (newMode == Mode::busy)
    {
        progress = service.getProgress();
        message.setText (service.getStatus() + (busyHere ? juce::String() : "\n\n(This is another song: its stems will be ready when you load it again.)")
                             + "\n\nYou can go on practising meanwhile: it runs in the background.",
                         juce::dontSendNotification);
    }

    if (newMode == mode)
        return;

    mode = newMode;
    const auto mixer = mode == Mode::mixer;

    for (auto& row : rows)
        for (juce::Component* c : { (juce::Component*) &row->name, (juce::Component*) &row->mute, (juce::Component*) &row->solo, (juce::Component*) &row->volume })
            c->setVisible (mixer);

    resetButton.setVisible (mixer);
    message.setVisible (! mixer);
    progressBar.setVisible (mode == Mode::busy);
    actionButton.setVisible (mode == Mode::busy || mode == Mode::offer);
    actionButton.onClick = nullptr;

    switch (mode)
    {
        case Mode::mixer:
            title.setText ("Stems", juce::dontSendNotification);
            break;

        case Mode::busy:
            title.setText ("Separating stems", juce::dontSendNotification);
            actionButton.setButtonText ("Cancel");
            actionButton.onClick = [this] { host.getStemService().cancel(); };
            break;

        case Mode::loading:
            title.setText ("Stems", juce::dontSendNotification);
            message.setText ("Loading the stems...", juce::dontSendNotification);
            break;

        case Mode::offer:
        {
            title.setText ("Separate into stems", juce::dontSendNotification);
            juce::String text = "Splits this song into vocals, guitar, bass, drums, piano and other, so you can mute "
                                "or solo any of them (speed, pitch and loops keep working).\n\n"
                                "It runs in the background and takes a few minutes. The stems are kept with the "
                                "song (about 30 MB for 4 minutes; Storage and clean-up can remove them).";

            if (! StemSeparator::hasModel())
                text << "\n\nThe first time, the separation model is downloaded (115 MB).";

            if (const auto error = host.getStemService().getLastError(); error.isNotEmpty())
                text << "\n\nLast attempt failed: " << error;

            message.setText (text, juce::dontSendNotification);
            actionButton.setButtonText ("Separate stems");
            actionButton.onClick = [this] { host.separateCurrentSong(); update(); };
            break;
        }

        case Mode::unavailable:
        case Mode::none:
            title.setText ("Stems", juce::dontSendNotification);
           #if JUCE_IOS
            message.setText ("Stem separation runs in the Windows app. Stems made there reach the iPad with the song "
                             "through the library folder, and then show up here.", juce::dontSendNotification);
           #else
            message.setText (host.getStemFolder() == juce::File() ? "Load a song file first (YouTube videos can't be separated)."
                                                                  : "Stem separation isn't available in this copy of the app (onnxruntime.dll is missing).",
                             juce::dontSendNotification);
           #endif
            break;
    }

    resized();
    repaint();
}

void StemMixer::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void StemMixer::resized()
{
    auto area = getLocalBounds().reduced (14, 10);
    title.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);

    if (mode == Mode::mixer)
    {
        resetButton.setBounds (area.removeFromBottom (34).removeFromRight (100).reduced (0, 2));

        for (auto& row : rows)
        {
            auto r = area.removeFromTop (42).reduced (0, 4);
            row->name.setBounds (r.removeFromLeft (80));
            row->mute.setBounds (r.removeFromLeft (40).reduced (2, 0));
            row->solo.setBounds (r.removeFromLeft (40).reduced (2, 0));
            r.removeFromLeft (10);
            row->volume.setBounds (r);
        }
    }
    else
    {
        auto bottom = area.removeFromBottom (38);
        actionButton.setBounds (bottom.removeFromRight (160).reduced (0, 2));

        if (mode == Mode::busy)
            progressBar.setBounds (area.removeFromBottom (24));

        message.setBounds (area);
    }
}

} // namespace pv
