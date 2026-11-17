#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace pv
{

/** What a resource is, decided from its file extension or URL. Drives the icon and what "Open" does. */
enum class ResourceKind
{
    pdf, image, text, document, spreadsheet, presentation, audio, video,
    youtube, googleDoc, web, other
};

/** One item attached to a topic: a file inside the topic's folder, or a web link. */
struct Resource
{
    juce::String id;            // file resources: the file name (same on every device); links: a UUID
    juce::String title;
    juce::String description;
    juce::String fileName;      // file inside the topic folder; empty for links
    juce::String url;           // links only
    juce::StringArray tags;
    juce::Time added;
    bool isTake = false;        // a recorded take (has its details in the folder's ".takes", see TakeInfo)

    bool isLink() const noexcept    { return fileName.isEmpty(); }
};

ResourceKind classifyFileName (const juce::String& fileName);
ResourceKind classifyUrl (const juce::String& url);
ResourceKind kindOf (const Resource&);

/** A YouTube video reference taken from any usual link form. */
struct YouTubeLink
{
    juce::String videoId;
    double startSeconds = 0.0;
};

/** watch?v=, youtu.be/, shorts/, embed/, live/, m. and music. hosts; start from t= / start= / #t= ("90", "1m30s"). */
std::optional<YouTubeLink> parseYouTubeUrl (const juce::String& url);

/** Splits "jazz, #technique , Jazz" into { "jazz", "technique" }: trimmed, no '#', no empties, no duplicates (ignoring case). */
juce::StringArray parseTags (const juce::String& text);
juce::String joinTags (const juce::StringArray& tags);
bool hasTag (const juce::StringArray& tags, const juce::String& tag);

/** Short text for the resource's badge, e.g. "PDF", "MP3", "YT", "WEB". */
juce::String badgeText (const Resource&);

/** Search for a single resource (Links tab): every word must start a word in its title, URL, tags or notes. */
bool resourceMatches (const Resource&, const juce::String& searchText);

/** Trims a typed URL and adds "https://" when it has no scheme ("www.x.com" -> "https://www.x.com"). */
juce::String normaliseUrl (const juce::String& url);

/** A point or a loop on a song (or a take) in a topic, with a short comment: "bar 17: watch the fingering".
    Loops keep the speed and pitch to practise them at. Saved in topic.xml, so they travel when the topic is shared. */
struct SongMark
{
    juce::String id;            // UUID
    juce::String fileName;      // the song or take, inside the topic folder
    double start = 0.0, end = 0.0;          // seconds; end > start: a loop
    double speed = 0.0, semitones = 0.0;    // loops: speed 0 = leave the player's speed as it is
    juce::String text;
    juce::Time added;

    bool isLoop() const noexcept    { return end > start + 0.01; }

    /** "0:42  bar 17: watch the fingering" / "Loop 0:42-0:58 at 80 %  ..." */
    juce::String describe() const;
};

/** "1:05" / "1:02:05" */
juce::String formatMarkTime (double seconds);

/** A student's practice on one topic, sent to the teacher with the topic if the student ticks it.
    Totals only: no times of day, no device names. */
struct PracticeSummary
{
    juce::Time made;                        // when the student shared it
    double totalSeconds = 0.0;              // over the last `weeklySeconds.size()` weeks
    int sessions = 0;
    std::vector<double> weeklySeconds;      // oldest first, the last one is the week it was made
    double firstBpm = 0.0, lastBpm = 0.0;   // metronome tempo at the start / end of those weeks (0 = not used)
    double firstSpeed = 0.0, lastSpeed = 0.0;
    int streakDays = 0;                     // days in a row with practice on this topic, up to the day it was made

    bool isEmpty() const noexcept           { return made == juce::Time(); }

    /** "3 h 10 min in 8 weeks (12 sessions) · tempo 80 -> 96 bpm · 4 days in a row" */
    juce::String describe() const;

    void toXml (juce::XmlElement& parent) const;
    static PracticeSummary fromXml (const juce::XmlElement&);
};

/** A practice topic: a folder in the library holding topic.xml and the topic's files. */
struct Topic
{
    juce::String id;            // UUID, or "dir:<folder name>" for folders made outside the app
    juce::Time created;
    juce::String title;
    juce::String description;   // the topic's notes: they travel when the topic is shared
    juce::String privateNotes;  // "My private notes": never leave the device (stripped from shared topics)
    juce::StringArray tags;
    bool inRotation = false;    // offered on the Today tab ("haven't practised since...")
    bool noGuitar = false;      // ear training, theory...: practice time runs on the clock, not the guitar input
    std::vector<Resource> resources;
    std::vector<SongMark> marks;   // markers and saved loops on the topic's songs and takes

    // Sharing. Never sent along: kept on this user's devices only.
    juce::String student;               // teachers: who the topic is for (a first name or nickname, typed by the teacher)
    juce::Time lastSent, lastReceived;
    PracticeSummary studentSummary;     // the latest summary a student sent with this topic (read-only)

    juce::File folder;
    juce::StringArray conflictFiles;   // copies of topic.xml left by a cloud sync conflict

    juce::File getFile (const Resource& r) const    { return r.isLink() ? juce::File() : folder.getChildFile (r.fileName); }
    bool isMissing (const Resource& r) const        { return ! r.isLink() && ! getFile (r).existsAsFile(); }
    Resource* findResource (const juce::String& resourceId);

    /** The marks on one file, by time. */
    std::vector<SongMark> marksFor (const juce::String& fileName) const;
    /** Adds the mark, or replaces the one with the same id. */
    void setMark (const SongMark&);
    bool removeMark (const juce::String& markId);
    /** Search: every word must start a word in the title, tags, notes, or a resource's name, tags or notes. */
    bool matches (const juce::String& searchText) const;

    /** The tag filter value that means "topics for this student" (Topics > Tags menu). */
    static juce::String studentFilter (const juce::String& student)     { return "@student:" + student; }

    /** True if the topic or any of its resources carries the tag (empty tag = always), or it's the student filter. */
    bool matchesTag (const juce::String& tag) const;

    /** Why a matching topic is listed when the reason isn't visible in the list (resource, notes, a resource's tag),
        e.g. "in resource “Autumn Leaves.mp3”"; empty when the title or the topic's own tags explain it. */
    juce::String describeMatch (const juce::String& searchText, const juce::String& tag) const;
};

/** The user's library folder: one sub-folder per topic, plus loose files (the Inbox).

    Everything lives in plain files so the folder can sit in a cloud drive and be shared between devices:
    - <topic folder>/topic.xml holds the title, description and the resource list;
    - files in a topic folder that topic.xml doesn't list (e.g. dropped in from another device)
      still show up as resources, titled with their file name;
    - files at the top level are the Inbox, waiting to be moved into a topic;
    - folders whose name starts with "." or "_" are ignored (reserved for the app's own data).
*/
class Library
{
public:
    explicit Library (const juce::File& rootFolder);

    const juce::File& getRoot() const noexcept           { return root; }

    /** Re-reads every topic and the Inbox from disk. */
    void rescan();

    /** Cheap fingerprint of the folder (names, sizes, modification times): changes when anything on disk does. */
    juce::String computeSignature() const;

    const std::vector<Topic>& getTopics() const noexcept { return topics; }   // newest first
    Topic* findTopic (const juce::String& topicId);

    /** The topic whose folder holds the file (e.g. a song loaded in the player), or nullptr. */
    const Topic* findTopicForFile (const juce::File&) const;

    const juce::Array<juce::File>& getInbox() const noexcept { return inbox; }

    /** Every tag used by a topic or resource, sorted, with the first spelling found. */
    juce::StringArray getAllTags() const;

    /** New topic folder + topic.xml. topicId: keep this id (a shared topic), unless empty or already used. */
    Topic* createTopic (const juce::String& title, const juce::String& topicId = {});
    bool saveTopic (const Topic&);
    bool deleteTopic (const juce::String& topicId);

    /** Copies a file (or reads a URL, e.g. from the iOS document picker) into the topic's folder.
        Finished stems of the original (from outside the library) move along. */
    Resource* addFile (Topic&, const juce::URL& source);

    /** Copies a file into the Inbox (the library's top folder); its finished stems move along. Empty File on failure. */
    juce::File addToInbox (const juce::URL& source);

    /** Moves a song's finished stems to where another copy of it keeps them (e.g. app data -> the library's
        ".stems"). False if there were none, they're incomplete, or the target already has some. */
    bool adoptStems (const juce::File& fromSong, const juce::File& toSong);

    /** Moves a file from the Inbox into the topic. */
    Resource* moveInboxFile (Topic&, const juce::File& inboxFile);

    Resource* addLink (Topic&, const juce::String& title, const juce::String& url);

    /** Removes a resource; a file resource's file goes to the bin (or is deleted where there's no bin). */
    bool removeResource (Topic&, const juce::String& resourceId);

    /** Moves a file or folder to the bin (deletes it where there's no bin, e.g. on the iPad). */
    static bool moveToBin (const juce::File&);

    bool moveResource (Topic&, const juce::String& resourceId, int delta);

    static bool isIgnoredFile (const juce::File&);
    static bool isConflictCopy (const juce::String& fileName);
    static bool isTemporaryTopicFile (const juce::String& fileName);
    static bool isDefaultFolderName (const juce::String& folderName);   // "New topic", "New topic (2)"...
    static juce::String titleFromUrl (const juce::String& url);

    /** Renames a folder still called "New topic" after the topic's title (not if it holds audio). True if renamed. */
    bool renameFolderAfterTitle (Topic&);

    /** topic.xml and the copies a cloud sync left next to it (Topic::conflictFiles), to choose which one to keep. */
    struct TopicFileVersion
    {
        juce::String fileName, title;
        juce::Time saved;
        int resources = 0;
    };
    static std::vector<TopicFileVersion> getTopicFileVersions (const Topic&);

    /** Makes the chosen file topic.xml and moves the other copies to the bin. Rescan afterwards. */
    static bool keepTopicFileVersion (const Topic&, const juce::String& fileName);

    static constexpr const char* topicFileName = "topic.xml";
    static constexpr const char* topicTempFileName = ".topic.xml.tmp";

    /** Reads a topic folder (topic.xml + files it doesn't list). Also used on unpacked shared topics. */
    static bool loadTopic (const juce::File& folder, Topic&);

    /** What saveTopic() writes to topic.xml. */
    static std::unique_ptr<juce::XmlElement> topicToXml (const Topic&);

private:
    static bool writeTopicFile (const juce::File& folder, const juce::String& text);
    void sortTopics();
    juce::File uniqueChild (const juce::File& folder, const juce::String& fileName) const;
    juce::File copyIn (const juce::URL& source, const juce::File& folder);

    juce::File root;
    std::vector<Topic> topics;
    juce::Array<juce::File> inbox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Library)
};

/** Where the library lives on this device (a per-device setting, kept outside the library). */
struct LibraryLocation
{
    static juce::File getDefaultFolder();
    static juce::File load();                 // empty File if the user hasn't chosen yet (or the folder is gone)
    static void save (const juce::File& folder);

    /** A folder picked with a FileChooser. iPad: keeps the permission to use it (folders in cloud drives).
        Returns the folder, or an empty File if it can't be used. */
    static juce::File saveChosen (const juce::URL& chosenFolder);
};

} // namespace pv
