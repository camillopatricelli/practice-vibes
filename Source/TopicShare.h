#pragma once

#include "Library.h"
#include <map>
#include <memory>

namespace pv
{

/** Sharing a topic with a teacher, a student or a bandmate: one file, "<title>.pvtopic".

    A .pvtopic is a zip of the topic's folder: topic.xml plus the files the sender chose (and the details of any takes
    among them, from ".takes"). It keeps the topic id, so importing it again updates the topic instead of making a
    second one. Never inside: private notes, the "in practice rotation" switch, cloud-conflict copies, stems, the
    practice log (that lives outside topic folders anyway) and absolute paths of the sender's device.

    Files the sender left out (usually commercial songs) stay listed with their size: on import the receiver's own
    copy is found by file name + size anywhere in their library, or they point to it once.
*/
namespace TopicShare
{
    constexpr const char* extension = ".pvtopic";
    constexpr juce::int64 emailLimitBytes = 20 * 1024 * 1024;    // most mail services take 20-25 MB

    bool isSharedTopicFile (const juce::File&);

    //==============================================================================
    // Sending

    /** One file of the topic, as offered in the Share dialog. */
    struct FileItem
    {
        juce::String fileName, title;
        juce::int64 bytes = 0;
        bool include = false;       // the default tick: own recordings, documents and images in; songs and videos out
        bool isTake = false;
        bool isSong = false;        // audio that isn't a take (may be a commercial recording)
        bool canShrink = false;     // uncompressed audio (WAV / AIFF) that can go in as AAC
        double seconds = 0.0;       // length of shrinkable audio (for the size estimate)
    };

    /** The topic's files that exist on this device, in the topic's order. */
    std::vector<FileItem> listFiles (const Topic&);

    /** Estimated size of a shrunk (AAC 192 kbit/s) copy. */
    juce::int64 shrunkBytes (const FileItem&);

    /** "12.4 MB, fits in an email" / "48 MB: too big for most email, send it with AirDrop, WhatsApp or a cloud drive". */
    juce::String sizeHint (juce::int64 bytes);

    struct ExportOptions
    {
        juce::StringArray includedFiles;    // file names inside the topic folder
        bool shrinkAudio = false;           // WAV / AIFF in the list go in as .m4a (AAC); needs AacFile::isAvailable()
    };

    /** Writes the .pvtopic (any thread: reads only the given Topic copy and the file system). */
    juce::Result write (const Topic&, const ExportOptions&, const juce::File& target);

    /** "<title>.pvtopic" in the folder (a name a file system can hold). */
    juce::File fileNameFor (const Topic&, const juce::File& folder);

    //==============================================================================
    // Receiving

    /** An unpacked .pvtopic, in a temporary folder that is deleted with the Package. */
    struct Package
    {
        Package() = default;
        ~Package();

        juce::File stage;                               // the unpacked folder
        Topic topic;                                    // as the sender has it (folder = stage)
        std::map<juce::String, juce::int64> sizes;      // file name -> size on the sender's device (files left out too)
        juce::Time sharedAt;

        bool isIncluded (const juce::String& fileName) const;

        JUCE_DECLARE_NON_COPYABLE (Package)
    };

    /** Checks and unpacks a .pvtopic. nullptr + error if it isn't one (or is damaged). Only plain file names and
        ".takes/<name>.xml" are taken from the zip: nothing can land outside the temporary folder. */
    std::unique_ptr<Package> open (const juce::File& pvtopic, juce::String& error);

    /** Where a left-out file can come from: a file chosen by the receiver, or a copy already in their library. */
    using LocatedFiles = std::map<juce::String, juce::File>;   // file name -> file on this device

    /** A copy of the file (same name, same size when the size is known) in one of the library's topics or the Inbox. */
    juce::File findInLibrary (const Library&, const juce::String& fileName, juce::int64 size, const juce::File& skipFolder = {});

    enum class Mode { update, copy };   // update: same topic id -> that topic (or a new one if there's none)

    /** What importing would do. */
    struct Preview
    {
        bool updatesExisting = false;           // a topic with the same id is in the library (and mode is update)
        juce::String title, existingTitle;
        juce::StringArray newResources;         // titles
        juce::StringArray updatedFiles;         // file names whose content changes
        juce::StringArray reconnected;          // left-out files found on this device (library or chosen)
        juce::StringArray missing;              // left-out files with no copy here: listed as missing until located
        bool titleChanged = false, notesChanged = false, tagsChanged = false, detailsChanged = false;
        int resourceCount = 0, includedFiles = 0;

        bool changesAnything() const;
        juce::String summary() const;           // one or two sentences for the import dialog
    };

    Preview preview (const Library&, const Package&, Mode, const LocatedFiles&);

    /** Imports into the library: copies the files in, connects left-out ones, saves topic.xml. On success topicId is
        the topic that was created or updated. Keeps the receiver's private notes, rotation switch and own resources.
        Rescan the library afterwards. */
    juce::Result apply (Library&, const Package&, Mode, const LocatedFiles&, juce::String& topicId);
}

} // namespace pv
