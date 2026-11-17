#include "TopicShare.h"
#include "AacFile.h"
#include "PracticeLog.h"
#include "Take.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <set>

namespace pv::TopicShare
{

namespace
{
    constexpr int formatVersion = 1;

    bool isUncompressedAudio (const juce::String& fileName)
    {
        const auto ext = fileName.fromLastOccurrenceOf (".", true, false).toLowerCase();
        return ext == ".wav" || ext == ".aif" || ext == ".aiff";
    }

    /** Already-compressed media gain nothing from deflate: stored as is (fast, and the size is what the user saw). */
    int compressionFor (const juce::String& fileName)
    {
        switch (classifyFileName (fileName))
        {
            case ResourceKind::audio: case ResourceKind::video: case ResourceKind::image:
                return isUncompressedAudio (fileName) ? 6 : 0;
            case ResourceKind::pdf: case ResourceKind::text: case ResourceKind::document: case ResourceKind::spreadsheet:
            case ResourceKind::presentation: case ResourceKind::youtube: case ResourceKind::googleDoc: case ResourceKind::web:
            case ResourceKind::other:
                break;
        }

        return 6;
    }

    double audioSeconds (const juce::File& f)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (f) }; reader != nullptr && reader->sampleRate > 0)
            return (double) reader->lengthInSamples / reader->sampleRate;

        return 0.0;
    }

    juce::Result shrinkToAac (const juce::File& source, const juce::File& target)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (source) };

        if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples <= 0)
            return juce::Result::fail ("can't read " + source.getFileName());

        const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
        juce::AudioBuffer<float> audio (channels, (int) reader->lengthInSamples);

        if (! reader->read (&audio, 0, audio.getNumSamples(), 0, true, channels > 1))
            return juce::Result::fail ("can't read " + source.getFileName());

        return AacFile::write (target, audio, reader->sampleRate, 192);
    }

    /** A name inside a .pvtopic we accept: "topic.xml", a plain file name, or ".takes/<plain name>.xml". */
    bool isSafeEntryName (const juce::String& name)
    {
        auto plain = [] (const juce::String& n)
        {
            return n.isNotEmpty() && n != "." && n != ".." && ! n.containsAnyOf ("/\\:") && n.trim() == n;
        };

        if (name.startsWith (juce::String (TakeInfo::folderName) + "/"))
        {
            const auto rest = name.fromFirstOccurrenceOf ("/", false, false);
            return plain (rest) && rest.endsWithIgnoreCase (".xml");
        }

        return plain (name) && ! name.startsWithChar ('.');
    }

    /** "name.m4a", or "name (2).m4a"... if that name is taken. */
    juce::String uniqueName (const juce::String& stem, const juce::String& ext, const juce::StringArray& taken)
    {
        auto name = stem + ext;

        for (int n = 2; taken.contains (name, true) && n < 10000; ++n)
            name = stem + " (" + juce::String (n) + ")" + ext;

        return name;
    }

    /** Deletes a temporary folder when it goes out of scope. */
    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("PracticeVibes-share-" + juce::Uuid().toString());
        ~TempFolder()   { folder.deleteRecursively(); }
    };

    bool copyReplacing (const juce::File& from, const juce::File& to)
    {
        if (from == to)
            return true;

        if (to.existsAsFile() && ! to.deleteFile())
            return false;

        return from.copyFileTo (to);
    }
}

bool isSharedTopicFile (const juce::File& f)
{
    return f.hasFileExtension (extension);
}

//==============================================================================
std::vector<FileItem> listFiles (const Topic& topic)
{
    std::vector<FileItem> items;

    for (auto& r : topic.resources)
    {
        if (r.isLink())
            continue;

        const auto file = topic.getFile (r);

        if (! file.existsAsFile())
            continue;

        FileItem item;
        item.fileName = r.fileName;
        item.title = r.title.isNotEmpty() ? r.title : r.fileName;
        item.bytes = file.getSize();
        item.isTake = r.isTake;

        const auto kind = classifyFileName (r.fileName);
        item.isSong = kind == ResourceKind::audio && ! r.isTake;
        item.canShrink = kind == ResourceKind::audio && isUncompressedAudio (r.fileName) && AacFile::isAvailable();
        item.include = ! item.isSong && kind != ResourceKind::video;

        if (item.canShrink)
            item.seconds = audioSeconds (file);

        items.push_back (std::move (item));
    }

    return items;
}

juce::int64 shrunkBytes (const FileItem& item)
{
    if (! item.canShrink || item.seconds <= 0.0)
        return item.bytes;

    return juce::jmin (item.bytes, (juce::int64) (item.seconds * 192000.0 / 8.0) + 4096);
}

juce::String sizeHint (juce::int64 bytes)
{
    juce::String size;

    if (bytes < 1024 * 1024)
        size = juce::String (juce::jmax ((juce::int64) 1, bytes / 1024)) + " KB";
    else
        size = juce::String ((double) bytes / (1024.0 * 1024.0), bytes < 100 * 1024 * 1024 ? 1 : 0) + " MB";

    return bytes <= emailLimitBytes ? size + ", fits in an email"
                                    : size + ": too big for most email. Send it with AirDrop, WhatsApp or a cloud drive";
}

juce::File fileNameFor (const Topic& topic, const juce::File& folder)
{
    auto name = juce::File::createLegalFileName (topic.title).trim().substring (0, 80).trim();

    if (name.isEmpty() || name.startsWithChar ('.'))
        name = "Topic " + name;

    return folder.getChildFile (name + extension);
}

PracticeSummary makeSummary (const PracticeLog& log, const juce::String& topicId, juce::Time now, int weeks)
{
    PracticeSummary s;
    s.made = now;
    s.weeklySeconds = log.weeklySeconds (topicId, now, weeks);

    for (auto w : s.weeklySeconds)
        s.totalSeconds += w;

    const auto from = PracticeLog::startOfWeek (now) - juce::RelativeTime::weeks (weeks - 1);
    std::set<juce::int64> days;   // local calendar days with practice on the topic

    for (auto* e : log.historyFor (topicId))
    {
        days.insert (juce::Time (e->start.getYear(), e->start.getMonth(), e->start.getDayOfMonth(), 0, 0).toMilliseconds());

        if (e->start < from || e->start > now)
            continue;

        ++s.sessions;

        if (e->maxBpm > 0.0)
        {
            if (s.firstBpm <= 0.0) s.firstBpm = e->maxBpm;
            s.lastBpm = e->maxBpm;
        }

        if (e->maxSpeed > 0.0)
        {
            if (s.firstSpeed <= 0.0) s.firstSpeed = e->maxSpeed;
            s.lastSpeed = e->maxSpeed;
        }
    }

    // Days in a row up to today (or up to yesterday, if today hasn't been practised yet).
    auto day = juce::Time (now.getYear(), now.getMonth(), now.getDayOfMonth(), 0, 0);

    if (days.count (day.toMilliseconds()) == 0)
        day = day - juce::RelativeTime::days (1);

    for (int guard = 0; guard < 3660 && days.count (day.toMilliseconds()) > 0; ++guard)
    {
        ++s.streakDays;
        const auto previous = day - juce::RelativeTime::hours (12);   // DST-safe step back to the previous day
        day = juce::Time (previous.getYear(), previous.getMonth(), previous.getDayOfMonth(), 0, 0);
    }

    return s;
}

juce::Result write (const Topic& topic, const ExportOptions& options, const juce::File& target)
{
    Topic shared = topic;
    shared.privateNotes = {};
    shared.inRotation = false;
    shared.student = {};
    shared.lastSent = shared.lastReceived = {};
    shared.studentSummary = options.summary;     // only the sender's own summary, if they chose to send it

    if (options.asNewTopic)
        shared.id = juce::Uuid().toDashedString();

    juce::TemporaryFile tempZip (target);
    TempFolder shrinkFolderHolder;
    const auto shrinkFolder = shrinkFolderHolder.folder;
    juce::ZipFile::Builder zip;
    juce::StringArray namesInZip { Library::topicFileName };

    // Names of included files that keep their name (a shrunk copy must not take one of them).
    for (auto& r : shared.resources)
        if (! r.isLink() && options.includedFiles.contains (r.fileName))
            if (! (options.shrinkAudio && isUncompressedAudio (r.fileName)))
                namesInZip.add (r.fileName);
    std::map<juce::String, juce::int64> leftOut;

    for (auto& r : shared.resources)
    {
        if (r.isLink())
            continue;

        const auto source = topic.getFile (r);

        if (! options.includedFiles.contains (r.fileName) || ! source.existsAsFile())
        {
            leftOut[r.fileName] = source.existsAsFile() ? source.getSize() : -1;
            continue;
        }

        auto fileToAdd = source;
        auto nameInZip = r.fileName;

        if (options.shrinkAudio && isUncompressedAudio (r.fileName) && AacFile::isAvailable())
        {
            const auto m4aName = uniqueName (source.getFileNameWithoutExtension(), ".m4a", namesInZip);
            shrinkFolder.createDirectory();
            const auto m4a = shrinkFolder.getChildFile (m4aName);

            if (const auto result = shrinkToAac (source, m4a); result.failed())
                return juce::Result::fail ("Could not make " + r.fileName + " smaller: " + result.getErrorMessage());

            fileToAdd = m4a;
            nameInZip = m4aName;
        }

        namesInZip.addIfNotAlreadyThere (nameInZip);
        zip.addFile (fileToAdd, compressionFor (nameInZip), nameInZip);

        // A take's details (song, timeline) go along; the song path only if it's relative to the library.
        if (r.isTake)
        {
            if (auto info = TakeInfo::load (source))
            {
                if (juce::File::isAbsolutePath (info->songPath))
                    info->songPath = juce::File (info->songPath).getFileName();

                if (auto xml = info->toXml())
                {
                    const auto entry = juce::String (TakeInfo::folderName) + "/" + nameInZip + ".xml";
                    zip.addEntry (std::make_unique<juce::MemoryInputStream> (juce::MemoryBlock (xml->toString().toRawUTF8(), xml->toString().getNumBytesAsUTF8()), true),
                                  6, entry, juce::Time::getCurrentTime());
                }
            }
        }

        for (auto& m : shared.marks)
            if (m.fileName == r.fileName)
                m.fileName = nameInZip;

        r.id = r.fileName = nameInZip;
    }

    auto xml = Library::topicToXml (shared);
    xml->setAttribute ("shared", juce::Time::getCurrentTime().toISO8601 (true));
    xml->setAttribute ("format", formatVersion);

    if (options.senderName.trim().isNotEmpty())
        xml->setAttribute ("from", options.senderName.trim().substring (0, 60));

    for (auto* e : xml->getChildWithTagNameIterator ("RESOURCE"))
    {
        const auto file = e->getStringAttribute ("file");

        if (file.isEmpty())
            continue;

        if (auto it = leftOut.find (file); it != leftOut.end())
        {
            e->setAttribute ("included", false);

            if (it->second >= 0)
                e->setAttribute ("size", juce::String (it->second));
        }
        else if (const auto f = shrinkFolder.getChildFile (file); f.existsAsFile())
        {
            e->setAttribute ("size", juce::String (f.getSize()));
        }
        else if (const auto source = topic.folder.getChildFile (file); source.existsAsFile())
        {
            e->setAttribute ("size", juce::String (source.getSize()));
        }
    }

    const auto text = xml->toString();
    zip.addEntry (std::make_unique<juce::MemoryInputStream> (juce::MemoryBlock (text.toRawUTF8(), text.getNumBytesAsUTF8()), true),
                  9, Library::topicFileName, juce::Time::getCurrentTime());

    {
        juce::FileOutputStream out (tempZip.getFile());

        if (! out.openedOk())
            return juce::Result::fail ("Can't write " + target.getFullPathName());

        if (! zip.writeToStream (out, nullptr))
            return juce::Result::fail ("Could not write the topic file");

        out.flush();

        if (out.getStatus().failed())
            return out.getStatus();
    }

    if (! tempZip.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Can't write " + target.getFullPathName());

    return juce::Result::ok();
}

//==============================================================================
namespace
{
    juce::String escapeHtml (const juce::String& text)
    {
        return text.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;").replace ("\"", "&quot;");
    }

    /** A file name as a relative link (spaces and other characters percent-encoded). */
    juce::String relativeHref (const juce::String& fileName)
    {
        return escapeHtml (juce::URL::addEscapeChars (fileName, false).replace ("+", "%20"));
    }

    juce::String kindName (const Resource& r)
    {
        switch (kindOf (r))
        {
            case ResourceKind::youtube:      return "YouTube video";
            case ResourceKind::googleDoc:    return "Google document";
            case ResourceKind::pdf:          return "PDF";
            case ResourceKind::image:        return "Image";
            case ResourceKind::audio:        return r.isTake ? "Recording" : "Audio";
            case ResourceKind::video:        return "Video";
            case ResourceKind::text:         return "Text";
            case ResourceKind::document:     return "Document";
            case ResourceKind::spreadsheet:  return "Spreadsheet";
            case ResourceKind::presentation: return "Presentation";
            case ResourceKind::web:          return "Web page";
            case ResourceKind::other:        break;
        }

        return "File";
    }
}

juce::String documentHtml (const Topic& topic, const juce::StringArray& attachedFiles, juce::Time sharedAt)
{
    juce::String links, files;

    for (auto& r : topic.resources)
    {
        const auto title = escapeHtml (r.title.isNotEmpty() ? r.title : (r.isLink() ? Library::titleFromUrl (r.url) : r.fileName));
        const auto description = r.description.trim().isNotEmpty() ? "<div class=\"desc\">" + escapeHtml (r.description.trim()) + "</div>" : juce::String();

        if (r.isLink())
        {
            auto host = juce::URL (r.url).getDomain();

            if (host.startsWithIgnoreCase ("www."))
                host = host.substring (4);

            links << "<li><a href=\"" << escapeHtml (r.url) << "\">" << title << "</a>"
                  << "<span class=\"meta\">" << kindName (r) << (host.isNotEmpty() ? " &middot; " + escapeHtml (host) : juce::String()) << "</span>"
                  << description << "</li>\n";
        }
        else
        {
            const auto attached = attachedFiles.contains (r.fileName);
            juce::String marks;

            for (auto& m : topic.marksFor (r.fileName))
                marks << "<div class=\"mark\">" << escapeHtml (m.describe()) << "</div>";

            files << "<li>" << (attached ? "<a href=\"" + relativeHref (r.fileName) + "\">" + title + "</a>" : title)
                  << "<span class=\"meta\">" << kindName (r) << " &middot; " << escapeHtml (r.fileName)
                  << (attached ? juce::String() : juce::String (" &middot; not included: use your own copy")) << "</span>"
                  << description << marks << "</li>\n";
        }
    }

    juce::String tags;

    for (auto& tag : topic.tags)
        tags << "<span class=\"tag\">" << escapeHtml (tag) << "</span>";

    juce::String html;
    html << "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
         << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
         << "<title>" << escapeHtml (topic.title) << "</title>\n<style>\n"
         << ":root { --bg: #fbf8f3; --card: #ffffff; --text: #2b2420; --muted: #7a6e66; --accent: #b0640f; --line: #e8e0d6; }\n"
         << "@media (prefers-color-scheme: dark) { :root { --bg: #1b1714; --card: #26201c; --text: #f1e9e1; --muted: #a89a8f; --accent: #f0a640; --line: #3a322c; } }\n"
         << "body { margin: 0; background: var(--bg); color: var(--text); font: 16px/1.55 -apple-system, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; }\n"
         << "main { max-width: 760px; margin: 0 auto; padding: 32px 16px 48px; }\n"
         << "h1 { font-size: 28px; line-height: 1.2; margin: 0 0 8px; }\n"
         << "h2 { font-size: 13px; letter-spacing: .08em; text-transform: uppercase; color: var(--muted); margin: 32px 0 10px; }\n"
         << ".tag { display: inline-block; font-size: 13px; padding: 2px 10px; margin: 0 6px 6px 0; border-radius: 999px; border: 1px solid var(--line); color: var(--muted); }\n"
         << ".date { color: var(--muted); font-size: 14px; margin: 4px 0 0; }\n"
         << ".notes { background: var(--card); border: 1px solid var(--line); border-radius: 12px; padding: 16px 18px; white-space: pre-wrap; overflow-wrap: anywhere; }\n"
         << "ul { list-style: none; padding: 0; margin: 0; }\n"
         << "li { background: var(--card); border: 1px solid var(--line); border-radius: 12px; padding: 12px 16px; margin-bottom: 8px; overflow-wrap: anywhere; }\n"
         << "a { color: var(--accent); font-weight: 600; text-decoration: none; } a:hover { text-decoration: underline; }\n"
         << ".meta { display: block; color: var(--muted); font-size: 13px; }\n"
         << ".desc { margin-top: 4px; white-space: pre-wrap; }\n"
         << ".mark { margin-top: 4px; font-size: 14px; padding-left: 10px; border-left: 3px solid var(--accent); }\n"
         << "footer { margin-top: 40px; color: var(--muted); font-size: 12px; }\n"
         << "@media print { body { background: #fff; color: #000; } li, .notes { break-inside: avoid; } "
         << "li > a[href^='http']::after { content: ' (' attr(href) ')'; font-weight: 400; color: #555; font-size: 12px; } }\n"
         << "</style></head><body><main>\n"
         << "<h1>" << escapeHtml (topic.title) << "</h1>\n";

    if (tags.isNotEmpty())
        html << "<div>" << tags << "</div>\n";

    if (sharedAt != juce::Time())
        html << "<p class=\"date\">Shared on " << sharedAt.formatted ("%d %B %Y") << "</p>\n";

    if (! topic.assignment.isEmpty())
        html << "<h2>Assignment</h2>\n<div class=\"notes\">" << escapeHtml (topic.assignment.describe().replace (middleDot(), "\n")) << "</div>\n";

    if (topic.description.trim().isNotEmpty())
        html << "<h2>Notes</h2>\n<div class=\"notes\">" << escapeHtml (topic.description.trim()) << "</div>\n";

    if (links.isNotEmpty())
        html << "<h2>Links</h2>\n<ul>\n" << links << "</ul>\n";

    if (files.isNotEmpty())
        html << "<h2>Files</h2>\n<ul>\n" << files << "</ul>\n";

    html << "<footer>Made with Practice Vibes</footer>\n</main></body></html>\n";
    return html;
}

juce::Result writeDocument (const Topic& topic, const ExportOptions& options, const juce::File& folder, juce::Array<juce::File>& written)
{
    written.clear();
    const auto name = fileNameFor (topic, folder).getFileNameWithoutExtension();
    const auto target = folder.getChildFile (name);

    // A fresh folder each time: attachments left out this time mustn't go along.
    if (target.isDirectory() && ! target.deleteRecursively())
        return juce::Result::fail ("Can't replace " + target.getFullPathName() + " (is a file in it open?)");

    if (! target.createDirectory())
        return juce::Result::fail ("Can't create " + target.getFullPathName());

    Topic shown = topic;
    juce::StringArray attached;
    juce::Array<juce::File> files;

    for (auto& r : shown.resources)
    {
        if (r.isLink() || ! options.includedFiles.contains (r.fileName))
            continue;

        const auto source = topic.getFile (r);

        if (! source.existsAsFile())
            continue;

        auto dest = target.getChildFile (r.fileName);

        if (options.shrinkAudio && isUncompressedAudio (r.fileName) && AacFile::isAvailable())
        {
            dest = target.getChildFile (uniqueName (source.getFileNameWithoutExtension(), ".m4a", attached));

            if (const auto result = shrinkToAac (source, dest); result.failed())
                return juce::Result::fail ("Could not make " + r.fileName + " smaller: " + result.getErrorMessage());
        }
        else if (! source.copyFileTo (dest))
        {
            return juce::Result::fail ("Could not copy " + r.fileName);
        }

        for (auto& m : shown.marks)
            if (m.fileName == r.fileName)
                m.fileName = dest.getFileName();

        r.fileName = r.id = dest.getFileName();
        attached.add (dest.getFileName());
        files.add (dest);
    }

    const auto page = target.getChildFile (name + ".html");

    if (! page.replaceWithText (documentHtml (shown, attached, juce::Time::getCurrentTime()), false, false, "\n"))
        return juce::Result::fail ("Can't write " + page.getFullPathName());

    written.add (page);
    written.addArray (files);
    return juce::Result::ok();
}

//==============================================================================
Package::~Package()
{
    if (stage.isDirectory())
        stage.deleteRecursively();
}

bool Package::isIncluded (const juce::String& fileName) const
{
    return fileName.isNotEmpty() && stage.getChildFile (fileName).existsAsFile();
}

std::unique_ptr<Package> open (const juce::File& pvtopic, juce::String& error)
{
    juce::ZipFile zip (pvtopic);

    if (zip.getNumEntries() == 0 || zip.getEntry (Library::topicFileName) == nullptr)
    {
        error = "This is not a Practice Vibes topic file (or it is damaged).";
        return nullptr;
    }

    auto package = std::make_unique<Package>();
    package->stage = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("PracticeVibes-import-" + juce::Uuid().toString());

    if (! package->stage.createDirectory())
    {
        error = "Can't unpack the topic: no room for a temporary folder.";
        return nullptr;
    }

    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* entry = zip.getEntry (i);
        const auto name = entry->filename;

        if (name.endsWithChar ('/') || ! isSafeEntryName (name))
            continue;   // folders and anything that isn't a plain name (never extracted outside the stage)

        const auto target = name.startsWith (juce::String (TakeInfo::folderName) + "/")
                                ? package->stage.getChildFile (TakeInfo::folderName).getChildFile (name.fromFirstOccurrenceOf ("/", false, false))
                                : package->stage.getChildFile (name);

        if (! target.isAChildOf (package->stage))
            continue;

        target.getParentDirectory().createDirectory();
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
        juce::FileOutputStream out (target);

        if (in == nullptr || ! out.openedOk() || out.writeFromInputStream (*in, -1) != entry->uncompressedSize)
        {
            error = "The topic file is damaged (could not unpack " + name + ").";
            return nullptr;
        }
    }

    auto xml = juce::parseXMLIfTagMatches (package->stage.getChildFile (Library::topicFileName), "TOPIC");

    if (xml == nullptr)
    {
        error = "This is not a Practice Vibes topic file (its topic.xml can't be read).";
        return nullptr;
    }

    if (xml->getIntAttribute ("format", formatVersion) > formatVersion)
        juce::Logger::writeToLog ("Shared topic made by a newer version of the app: importing what this version knows");

    package->sharedAt = juce::Time::fromISO8601 (xml->getStringAttribute ("shared"));
    package->senderName = xml->getStringAttribute ("from").trim().substring (0, 60);

    for (auto* e : xml->getChildWithTagNameIterator ("RESOURCE"))
        if (const auto file = e->getStringAttribute ("file"); file.isNotEmpty())
            package->sizes[file] = e->getStringAttribute ("size", "-1").getLargeIntValue();

    Library::loadTopic (package->stage, package->topic);
    package->summary = package->topic.studentSummary;

    // Never taken from a shared file, even if someone put them in.
    package->topic.privateNotes = {};
    package->topic.student = {};
    package->topic.lastSent = package->topic.lastReceived = {};
    package->topic.studentSummary = {};
    package->topic.conflictFiles.clear();

    if (package->topic.id.startsWith ("dir:"))
        package->topic.id = {};           // a folder made outside the app: no id to match on

    return package;
}

juce::File findInLibrary (const Library& library, const juce::String& fileName, juce::int64 size, const juce::File& skipFolder)
{
    auto matches = [&] (const juce::File& f)
    {
        return f.existsAsFile() && f.getFileName().equalsIgnoreCase (fileName) && (size < 0 || f.getSize() == size);
    };

    for (auto& t : library.getTopics())
        if (t.folder != skipFolder)
            if (const auto f = t.folder.getChildFile (fileName); matches (f))
                return f;

    for (auto& f : library.getInbox())
        if (matches (f))
            return f;

    return {};
}

//==============================================================================
namespace
{
    const Topic* targetFor (const Library& library, const Package& package, Mode mode)
    {
        if (mode == Mode::copy || package.topic.id.isEmpty())
            return nullptr;

        for (auto& t : library.getTopics())
            if (t.id == package.topic.id)
                return &t;

        return nullptr;
    }

    juce::File sourceFor (const Library& library, const Package& package, const LocatedFiles& located,
                          const juce::String& fileName, const juce::File& targetFolder)
    {
        if (package.isIncluded (fileName))
            return package.stage.getChildFile (fileName);

        if (auto it = located.find (fileName); it != located.end() && it->second.existsAsFile())
            return it->second;

        const auto size = package.sizes.count (fileName) > 0 ? package.sizes.at (fileName) : (juce::int64) -1;
        return findInLibrary (library, fileName, size, targetFolder);
    }
}

bool Preview::changesAnything() const
{
    return ! updatesExisting || titleChanged || notesChanged || tagsChanged || detailsChanged || newMarks > 0 || hasSummary
        || assignmentChanged
        || ! newResources.isEmpty() || ! updatedFiles.isEmpty() || ! reconnected.isEmpty();
}

juce::String Preview::summary() const
{
    auto plural = [] (int n, const juce::String& one, const juce::String& many)
    {
        return juce::String (n) + " " + (n == 1 ? one : many);
    };

    juce::String text;

    if (! updatesExisting)
    {
        text << "New topic " << quoted (title) << ": " << plural (resourceCount, "resource", "resources");

        if (includedFiles > 0)
            text << " (" << plural (includedFiles, "file", "files") << " included)";

        text << ".";
    }
    else if (! changesAnything())
    {
        text << "You already have " << quoted (existingTitle) << " as it is in this file: nothing to update.";
    }
    else
    {
        juce::StringArray changes;

        if (titleChanged)                 changes.add ("renamed to " + quoted (title));
        if (! newResources.isEmpty())     changes.add (plural (newResources.size(), "new resource", "new resources"));
        if (! updatedFiles.isEmpty())     changes.add (plural (updatedFiles.size(), "updated file", "updated files"));
        if (notesChanged)                 changes.add ("notes changed");
        if (tagsChanged)                  changes.add ("new tags");
        if (detailsChanged)               changes.add ("resource details changed");
        if (newMarks > 0)                 changes.add (plural (newMarks, "new marker or loop", "new markers or loops"));
        if (hasSummary)                   changes.add ("a practice summary");
        if (assignmentChanged)            changes.add ("new assignment");
        if (! reconnected.isEmpty())      changes.add (plural (reconnected.size(), "file connected", "files connected"));

        text << "Updates your topic " << quoted (existingTitle) << ": " << changes.joinIntoString (", ") << ".";
    }

    if (! missing.isEmpty())
        text << "\n" << (missing.size() == 1 ? "One file was" : juce::String (missing.size()) + " files were")
             << " left out by the sender and isn't on this device yet (it stays listed as missing until you locate it).";

    return text;
}

Preview preview (const Library& library, const Package& package, Mode mode, const LocatedFiles& located)
{
    Preview p;
    const auto& incoming = package.topic;
    const auto* target = targetFor (library, package, mode);

    p.updatesExisting = target != nullptr;
    p.title = incoming.title;
    p.resourceCount = (int) incoming.resources.size();

    if (target != nullptr)
    {
        p.existingTitle = target->title;
        p.titleChanged = target->title != incoming.title;
        p.notesChanged = target->description.trim() != incoming.description.trim();

        for (auto& tag : incoming.tags)
            p.tagsChanged = p.tagsChanged || ! hasTag (target->tags, tag);
    }

    p.hasSummary = ! package.summary.isEmpty();
    p.assignmentChanged = incoming.assignment.describe() != (target != nullptr ? target->assignment.describe() : juce::String());

    for (auto& m : incoming.marks)
    {
        bool same = false;

        if (target != nullptr)
            for (auto& tm : target->marks)
                same = same || (tm.id == m.id && tm.text == m.text && std::abs (tm.start - m.start) < 0.01
                                && std::abs (tm.end - m.end) < 0.01 && std::abs (tm.speed - m.speed) < 0.001
                                && std::abs (tm.semitones - m.semitones) < 0.01);

        if (! same)
            ++p.newMarks;
    }

    for (auto& r : incoming.resources)
    {
        const Resource* existing = nullptr;

        if (target != nullptr)
            for (auto& tr : target->resources)
                if (tr.id == r.id)
                    existing = &tr;

        if (existing == nullptr)
            p.newResources.add (r.title.isNotEmpty() ? r.title : r.fileName);
        else if (existing->title != r.title || existing->description.trim() != r.description.trim() || existing->tags != r.tags
                 || existing->url != r.url)
            p.detailsChanged = true;

        if (r.isLink())
            continue;

        if (package.isIncluded (r.fileName))
            ++p.includedFiles;

        const auto here = target != nullptr ? target->folder.getChildFile (r.fileName) : juce::File();

        if (existing != nullptr && ! here.existsAsFile() && package.isIncluded (r.fileName))
        {
            p.updatedFiles.add (r.fileName);   // was missing here, now comes with the file
            continue;
        }

        if (here.existsAsFile())
        {
            if (package.isIncluded (r.fileName) && package.stage.getChildFile (r.fileName).getSize() != here.getSize())
                p.updatedFiles.add (r.fileName);

            continue;
        }

        if (package.isIncluded (r.fileName))
            continue;

        if (sourceFor (library, package, located, r.fileName, target != nullptr ? target->folder : juce::File()).existsAsFile())
            p.reconnected.add (r.fileName);
        else
            p.missing.add (r.fileName);
    }

    return p;
}

juce::Result apply (Library& library, const Package& package, Mode mode, const LocatedFiles& located, juce::String& topicId,
                    const juce::String& student)
{
    const auto& incoming = package.topic;
    const auto* existing = targetFor (library, package, mode);

    // Sources first (a reconnect may come from a topic folder; nothing moves until all are known).
    std::map<juce::String, juce::File> sources;

    for (auto& r : incoming.resources)
        if (! r.isLink())
            sources[r.fileName] = sourceFor (library, package, located, r.fileName, existing != nullptr ? existing->folder : juce::File());

    Topic* target = existing != nullptr ? library.findTopic (existing->id)
                                        : library.createTopic (incoming.title, mode == Mode::copy ? juce::String() : incoming.id);

    if (target == nullptr)
        return juce::Result::fail ("Could not create the topic folder in the library.");

    const bool isNew = existing == nullptr;

    for (auto& r : incoming.resources)
    {
        Resource* tr = target->findResource (r.id);

        if (! r.isLink())
        {
            const auto dest = target->folder.getChildFile (r.fileName);
            const auto& source = sources[r.fileName];
            const bool replace = package.isIncluded (r.fileName);

            if (source.existsAsFile() && (replace || ! dest.existsAsFile()))
            {
                if (! copyReplacing (source, dest))
                    return juce::Result::fail ("Could not copy " + r.fileName + " into the topic.");

                // A take's details travel with it.
                const auto info = package.stage.getChildFile (TakeInfo::folderName).getChildFile (r.fileName + ".xml");

                if (replace && info.existsAsFile())
                {
                    const auto infoDest = TakeInfo::infoFileFor (dest);
                    infoDest.getParentDirectory().createDirectory();
                    copyReplacing (info, infoDest);
                }
            }
        }

        if (tr == nullptr)
        {
            Resource added = r;
            added.isTake = false;
            target->resources.push_back (std::move (added));
        }
        else
        {
            tr->title = r.title;
            tr->description = r.description;
            tr->tags = r.tags;

            if (r.isLink())
                tr->url = r.url;
        }
    }

    for (auto& m : incoming.marks)
        target->setMark (m);

    target->assignment = incoming.assignment;

    if (isNew && ! incoming.assignment.isEmpty())
        target->inRotation = true;   // homework: offered on the Today tab (the receiver can switch it off)

    if (! package.summary.isEmpty())
        target->studentSummary = package.summary;

    if (student.trim().isNotEmpty())
        target->student = student.trim().substring (0, 60);

    target->lastReceived = juce::Time::getCurrentTime();
    target->title = incoming.title.isNotEmpty() ? incoming.title : target->title;
    target->description = incoming.description;
    target->noGuitar = incoming.noGuitar;

    if (isNew)
        target->tags = incoming.tags;
    else
        for (auto& tag : incoming.tags)
            if (! hasTag (target->tags, tag))
                target->tags.add (tag);

    if (! library.saveTopic (*target))
        return juce::Result::fail ("Could not save the topic.");

    topicId = target->id;
    return juce::Result::ok();
}

} // namespace pv::TopicShare
