#pragma once

#include "SegmentedControl.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "ChordView.h"
#include "MetronomeProcessor.h"
#include "PracticeLog.h"
#include "PlayerProcessor.h"
#include "StemMixer.h"
#include "Take.h"
#include "TimeDisplay.h"
#include "WaveformView.h"

namespace pv
{

class YouTubePlayer;

/** The "Practice" tab: song player (speed, pitch, A-B loop) and metronome. */
class PracticePanel final : public juce::Component,
                            private juce::Timer,
                            private StemMixer::Host
{
public:
    PracticePanel (PlayerProcessor&, MetronomeProcessor&);
    ~PracticePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Loads a song into the player (also used by the Topics tab to open audio resources).
        `then`: speed, pitch, loop and metronome to apply once it's loaded (restoring a practice setup). */
    void loadSong (const juce::URL&, std::optional<PracticeSetup> then = std::nullopt, std::function<void()> onLoaded = {});

    /** Replays a recorded take over its song: loads the song (unless it's loaded already), then the take.
        While the take is open, the transport plays the take and the song follows the take's timeline. */
    void openTake (const juce::File& songFile, const juce::File& takeFile, const TakeInfo&);
    void closeTake();
    bool isTakeMode() const                             { return player.hasTake(); }

    /** What's on screen now, for the practice log (songPath: the song's full path). */
    PracticeSetup captureSetup() const;

    /** Speed, pitch, loop (for the song or video showing now) and metronome settings of a saved setup. */
    void applySetup (const PracticeSetup&);

    /** Footswitch action: mutes / unmutes one stem (Stems::Index) (returns a short message for the log). */
    juce::String toggleStem (int stem);

    /** Shows a short note in place of the hint line for a few seconds (e.g. "Metronome set to 84 bpm..."). */
    void showNote (const juce::String&);

    juce::File getSongFile() const                      { return songFile; }   // the loaded song's file, if local

    /** Markers and saved loops on a song or take, kept in its topic. getMarks returns nullopt when the file isn't
        in a topic (marks can't be saved then). */
    std::function<std::optional<std::vector<SongMark>> (const juce::File&)> getMarks;
    std::function<void (const juce::File&, const SongMark&)> saveMark;        // add, or replace by id
    std::function<void (const juce::File&, const juce::String& markId)> removeMark;

    /** Shows the marks of the song (and the open take) again, e.g. after the topic changed on disk. */
    void refreshMarks();

    /** A song chosen with "Open song..." has loaded (the URL it came from). */
    std::function<void (const juce::URL&)> onSongPicked;

    /** The loaded song was copied to `newFile` (e.g. into the library's Inbox): from now on it is that file
        (stems, practice log and setups refer to the copy). Nothing is reloaded. */
    void songCopiedTo (const juce::File& newFile);

    bool isSeparatingStems() const;
    bool isMediaPlaying() const;                        // the song or the video is playing
    double getMediaSpeed() const                        { return speedSlider.getValue() / 100.0; }
    juce::String getMediaName() const;

    /** YouTube mode: the video (drawn by MainComponent's YouTubePlayer over getVideoArea()) replaces the waveform;
        transport, A-B loop, speed and volume control the video. */
    void setYouTubePlayer (YouTubePlayer* p)            { youtube = p; }
    void showYouTube();
    void closeYouTube();
    bool isYouTubeMode() const noexcept                 { return youTubeMode; }
    juce::Rectangle<int> getVideoArea() const noexcept  { return videoArea; }
    std::function<void()> onCompactRequested;

private:
    class BeatIndicator;
    class YouTubeTimeline;
    class TakeTimelineView;

    // Stems (see Stems.h / StemMixer.h): separated in the background, mixed by the player
    StemService stemService;
    StemControls stemControls;
    juce::TextButton stemsButton { "Stems" };
    juce::File stemFolder;                              // for the song loaded now
    std::shared_ptr<const LoadedSong> stemsSong, stemsSongOfFile;
    bool stemsChecked = false, loadingStems = false;
    void updateStems();
    void loadStems();
    void applyStemGains();
    void showStemMixer();

    PlayerProcessor& getPlayer() override               { return player; }
    StemService& getStemService() override              { return stemService; }
    StemControls& getStemControls() override            { return stemControls; }
    void stemControlsChanged() override                 { applyStemGains(); }
    juce::File getStemFolder() override                 { return stemFolder; }
    bool isLoadingStems() override                      { return loadingStems; }
    void separateCurrentSong() override;

    // Take mode (a recorded take replays over the song)
    std::unique_ptr<TakeTimelineView> takeTimeline;
    juce::Label guitarVolumeLabel;
    juce::Slider guitarVolumeSlider;
    juce::TextButton closeTakeButton { "Close take" }, exportMixButton { "Export mix..." };
    juce::ScopedMessageBox shareBox;                // iPad share sheet for an exported mix
    void exportMix();
    void renderMix (const juce::File& target, bool share);
    juce::String takeTitle;
    bool takeShown = false;
    double savedSpeed = 100.0, savedPitch = 0.0, savedLoopStart = 0.0, savedLoopEnd = 0.0;
    bool savedLoopOn = false;
    std::shared_ptr<const LoadedSong> takeSong;     // the song the saved loop belongs to
    void updateTakeMode();

    // YouTube mode
    YouTubePlayer* youtube = nullptr;
    bool youTubeMode = false, compactShown = false;
    double lastShownRate = 1.0;
    juce::Rectangle<int> videoArea;
    juce::Rectangle<int> playbackCard, metronomeCard;   // painted as rounded cards
    std::unique_ptr<YouTubeTimeline> youTubeTimeline;
    juce::TextButton compactButton { "Compact player" }, closeVideoButton { "Close video" }, openOnYouTubeButton { "Open on YouTube" };
    void updateModeVisibility();
    double positionSeconds() const;
    bool loopIsEnabled() const;
    bool loopExists() const;
    double loopStartSeconds() const;
    double loopEndSeconds() const;
    void setLoop (double start, double end, bool enabled);

    void timerCallback() override;
    void openFile();
    void togglePlay();
    void updateLoopButtons();
    void updateTrainerControls();
    void startChordAnalysis (std::shared_ptr<const LoadedSong>);

    static juce::String formatTime (double seconds);

    PlayerProcessor& player;
    MetronomeProcessor& metronome;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Song
    juce::Label songHeader, songLabel;
    TimeDisplay timeLabel;
    juce::TextButton openButton { "Open audio file" };
    juce::TextButton marksButton { "Marks" };
    juce::File takeFile;                        // the open take's audio file
    void showMarksMenu();
    void recallMark (const SongMark&);
    void editMark (const juce::File&, SongMark, const juce::String& title);
    juce::File markedFile() const               { return isTakeMode() ? takeFile : songFile; }
    WaveformView waveform;
    juce::TextButton zoomOutButton { "-" }, zoomInButton { "+" }, fitButton { "Fit" }, zoomLoopButton { "Zoom to loop" };
    juce::TextButton snapButton;
    void updateSnapButton();
    juce::TextButton rewindButton { "|<" }, playButton { "Play" }, stopButton { "Stop" },
                     setAButton { "Set A" }, setBButton { "Set B" }, loopButton { "Loop" }, clearLoopButton { "Clear loop" };
    double pendingLoopStart = -1.0;
    std::shared_ptr<const LoadedSong> shownSong;   // detects songs loaded from elsewhere (e.g. command line)
    std::shared_ptr<const LoadedSong> songOfFile;  // the song `songFile` belongs to
    juce::File songFile;

    // Chords: analysed in the background whenever a song is loaded.
    ChordView chordView;
    juce::ThreadPool chordPool { 1 };
    std::shared_ptr<std::atomic<bool>> chordCancel;

    juce::Slider speedSlider, pitchSlider, songVolumeSlider;
    juce::Label speedLabel, pitchLabel, songVolumeLabel, hintLabel, noteLabel;
    juce::uint32 noteUntilMs = 0;

    // Metronome
    juce::Label metronomeHeader, bpmDisplay;
    juce::TextButton metronomeButton { "Start metronome" };
    std::unique_ptr<BeatIndicator> beatIndicator;
    juce::Slider bpmSlider, clickVolumeSlider;
    juce::Label bpmLabel, beatsLabel, clickVolumeLabel;
    SegmentedControl beatsBox, subdivisionBox;     // segmented (mock-up); ComboBox-like calls
    juce::ComboBox soundBox;
    juce::Label subdivisionLabel, soundLabel, subLevelLabel, swingLabel;
    juce::Slider subLevelSlider, swingSlider;
    void updateSubdivisionControls();

    juce::ToggleButton trainerToggle { "Speed trainer" };
    juce::ToggleButton followToggle { "Follow song beats" };
    void updateFollowControls();
    bool tempoWasLocked = false, songGridFound = false;
    juce::Slider stepSlider, intervalSlider, maxBpmSlider;
    juce::ComboBox unitBox;
    juce::Label stepLabel, everyLabel, maxLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PracticePanel)
};

} // namespace pv
