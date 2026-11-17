#include "Library.h"
#include "Stems.h"
#include "Take.h"
#include "AppPaths.h"
#include "FolderAccess.h"
#include <regex>

namespace pv
{

//==============================================================================
ResourceKind classifyFileName (const juce::String& fileName)
{
    const auto ext = fileName.fromLastOccurrenceOf (".", false, false).toLowerCase();

    if (! fileName.containsChar ('.'))                                           return ResourceKind::other;
    if (ext == "pdf")                                                            return ResourceKind::pdf;
    if (juce::StringArray { "jpg", "jpeg", "png", "gif", "heic", "heif", "webp", "bmp", "tif", "tiff" }.contains (ext))
        return ResourceKind::image;
    if (juce::StringArray { "txt", "md", "rtf", "html", "htm", "gp", "gp5", "gpx", "tab", "chopro", "cho" }.contains (ext))
        return ResourceKind::text;
    if (juce::StringArray { "doc", "docx", "odt", "pages" }.contains (ext))     return ResourceKind::document;
    if (juce::StringArray { "xls", "xlsx", "ods", "numbers", "csv" }.contains (ext))
        return ResourceKind::spreadsheet;
    if (juce::StringArray { "ppt", "pptx", "odp", "key" }.contains (ext))       return ResourceKind::presentation;
    if (juce::StringArray { "wav", "aif", "aiff", "flac", "mp3", "m4a", "ogg", "aac" }.contains (ext))
        return ResourceKind::audio;
    if (juce::StringArray { "mp4", "mov", "m4v", "webm", "mkv", "avi" }.contains (ext))
        return ResourceKind::video;
    if (juce::StringArray { "gdoc", "gsheet", "gslides" }.contains (ext))       return ResourceKind::googleDoc;
    if (juce::StringArray { "url", "webloc" }.contains (ext))                   return ResourceKind::web;

    return ResourceKind::other;
}

ResourceKind classifyUrl (const juce::String& url)
{
    const auto lower = url.trim().toLowerCase();
    const auto host = juce::URL (lower).getDomain();

    if (host.endsWith ("youtube.com") || host.endsWith ("youtu.be") || host.endsWith ("youtube-nocookie.com"))
        return ResourceKind::youtube;

    if (host == "docs.google.com" || host == "drive.google.com")
        return ResourceKind::googleDoc;

    return ResourceKind::web;
}

static double parseYouTubeTime (juce::String t)
{
    t = t.trim().toLowerCase();

    if (t.containsOnly ("0123456789."))
        return t.getDoubleValue();

    double total = 0.0, number = 0.0;
    bool any = false;

    for (auto c : t)
    {
        if (c >= '0' && c <= '9')
        {
            number = number * 10.0 + (c - '0');
            any = true;
        }
        else if (c == 'h') { total += number * 3600.0; number = 0.0; }
        else if (c == 'm') { total += number * 60.0;   number = 0.0; }
        else if (c == 's') { total += number;          number = 0.0; }
        else return 0.0;
    }

    return any ? total + number : 0.0;
}

std::optional<YouTubeLink> parseYouTubeUrl (const juce::String& urlText)
{
    auto text = urlText.trim();

    if (! text.containsChar (':'))
        text = "https://" + text;

    if (classifyUrl (text) != ResourceKind::youtube)
        return std::nullopt;

    const auto afterScheme = text.fromFirstOccurrenceOf ("://", false, false);
    const auto host = afterScheme.upToFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false).toLowerCase();
    const auto fragment = text.fromFirstOccurrenceOf ("#", false, false);
    const auto withoutFragment = text.upToFirstOccurrenceOf ("#", false, false);
    const auto path = afterScheme.fromFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false)
                                 .upToFirstOccurrenceOf ("#", false, false);
    const auto query = withoutFragment.fromFirstOccurrenceOf ("?", false, false);

    juce::StringPairArray params;

    for (auto& pair : juce::StringArray::fromTokens (query + "&" + fragment, "&", {}))
        if (pair.containsChar ('='))
            params.set (pair.upToFirstOccurrenceOf ("=", false, false), juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false)));

    juce::String id;
    const auto segments = juce::StringArray::fromTokens (path, "/", {});

    if (host.endsWith ("youtu.be"))
        id = segments[0];
    else if (segments[0] == "watch")
        id = params["v"];
    else if (juce::StringArray { "shorts", "embed", "live", "v", "e" }.contains (segments[0]))
        id = segments[1];

    if (id.length() < 6 || id.length() > 20 || ! id.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"))
        return std::nullopt;

    YouTubeLink link;
    link.videoId = id;
    link.startSeconds = parseYouTubeTime (params.containsKey ("t") ? params["t"] : params["start"]);
    return link;
}

ResourceKind kindOf (const Resource& r)
{
    return r.isLink() ? classifyUrl (r.url) : classifyFileName (r.fileName);
}

juce::StringArray parseTags (const juce::String& text)
{
    juce::StringArray result;

    for (auto tag : juce::StringArray::fromTokens (text, ",;", "\""))
    {
        tag = tag.trim();

        while (tag.startsWithChar ('#'))
            tag = tag.substring (1).trim();

        if (tag.isNotEmpty() && ! hasTag (result, tag))
            result.add (tag);
    }

    return result;
}

juce::String joinTags (const juce::StringArray& tags)
{
    return tags.joinIntoString (", ");
}

bool hasTag (const juce::StringArray& tags, const juce::String& tag)
{
    return tags.contains (tag.trim(), true);
}

juce::String badgeText (const Resource& r)
{
    if (r.isTake)
        return "TAKE";

    switch (kindOf (r))
    {
        case ResourceKind::youtube:    return "YT";
        case ResourceKind::googleDoc:  return r.isLink() ? juce::String ("GDOC")
                                                         : r.fileName.fromLastOccurrenceOf (".", false, false).toUpperCase();
        case ResourceKind::web:        return "WEB";
        case ResourceKind::other:
            if (! r.fileName.containsChar ('.'))
                return "FILE";
            break;
        case ResourceKind::pdf: case ResourceKind::image: case ResourceKind::text: case ResourceKind::document:
        case ResourceKind::spreadsheet: case ResourceKind::presentation: case ResourceKind::audio: case ResourceKind::video:
            break;
    }

    auto ext = r.fileName.fromLastOccurrenceOf (".", false, false).toUpperCase();

    if (ext == "JPEG") ext = "JPG";
    if (ext == "AIFF") ext = "AIF";

    return ext.substring (0, 4);
}

//==============================================================================
Resource* Topic::findResource (const juce::String& resourceId)
{
    for (auto& r : resources)
        if (r.id == resourceId)
            return &r;

    return nullptr;
}

namespace
{
    /** True if `word` starts a word in `text` ("am" finds "Amazing Grace", not "Hammer-on"). */
    bool hasWordStartingWith (const juce::String& text, const juce::String& word)
    {
        for (auto i = text.indexOfIgnoreCase (word); i >= 0; i = text.indexOfIgnoreCase (i + 1, word))
            if (i == 0 || ! juce::CharacterFunctions::isLetterOrDigit (text[i - 1]))
                return true;

        return false;
    }

    bool anyStartsWith (const juce::StringArray& list, const juce::String& word)
    {
        for (auto& s : list)
            if (hasWordStartingWith (s, word))
                return true;

        return false;
    }

    juce::String resourceName (const Resource& r)
    {
        return r.title.isNotEmpty() ? r.title : (r.isLink() ? r.url : r.fileName);
    }

    juce::StringArray searchWords (const juce::String& searchText)
    {
        juce::StringArray words;
        words.addTokens (searchText, " 	", "\"");
        words.trim();
        words.removeEmptyStrings();
        return words;
    }

    /** Where `word` matches: an empty string for places shown in the topic list (title, topic tags),
        a description for hidden ones, or nullopt when it doesn't match at all. */
    std::optional<juce::String> findWord (const Topic& t, const juce::String& word)
    {
        if (hasWordStartingWith (t.title, word) || anyStartsWith (t.tags, word))
            return juce::String();

        for (auto& r : t.resources)
            if (hasWordStartingWith (resourceName (r), word))
                return juce::String (juce::CharPointer_UTF8 ("in resource â")) + resourceName (r) + juce::String (juce::CharPointer_UTF8 ("â"));

        for (auto& r : t.resources)
            if (anyStartsWith (r.tags, word))
                return juce::String (juce::CharPointer_UTF8 ("tag on â")) + resourceName (r) + juce::String (juce::CharPointer_UTF8 ("â"));

        if (hasWordStartingWith (t.description, word))
            return juce::String ("in notes");

        for (auto& r : t.resources)
            if (hasWordStartingWith (r.description, word))
                return juce::String (juce::CharPointer_UTF8 ("in notes of â")) + resourceName (r) + juce::String (juce::CharPointer_UTF8 ("â"));

        return std::nullopt;
    }
}

bool resourceMatches (const Resource& r, const juce::String& searchText)
{
    for (auto& word : searchWords (searchText))
        if (! (hasWordStartingWith (resourceName (r), word) || hasWordStartingWith (r.url, word)
                || anyStartsWith (r.tags, word) || hasWordStartingWith (r.description, word)))
            return false;

    return true;
}

juce::String normaliseUrl (const juce::String& url)
{
    auto clean = url.trim();
    return clean.isEmpty() || clean.containsChar (':') ? clean : "https://" + clean;
}

bool Topic::matches (const juce::String& searchText) const
{
    for (auto& word : searchWords (searchText))
        if (! findWord (*this, word).has_value())
            return false;

    return true;
}

bool Topic::matchesTag (const juce::String& tag) const
{
    if (tag.isEmpty() || hasTag (tags, tag))
        return true;

    for (auto& r : resources)
        if (hasTag (r.tags, tag))
            return true;

    return false;
}

juce::String Topic::describeMatch (const juce::String& searchText, const juce::String& tag) const
{
    juce::StringArray places;

    if (tag.isNotEmpty() && ! hasTag (tags, tag))
        for (auto& r : resources)
            if (hasTag (r.tags, tag))
            {
                places.add ("#" + tag + juce::String (juce::CharPointer_UTF8 (" on â")) + resourceName (r)
                            + juce::String (juce::CharPointer_UTF8 ("â")));
                break;
            }

    for (auto& word : searchWords (searchText))
        if (auto place = findWord (*this, word); place.has_value() && place->isNotEmpty())
            places.addIfNotAlreadyThere (*place);

    return places.joinIntoString (", ");
}

//==============================================================================
Library::Library (const juce::File& rootFolder) : root (rootFolder)
{
    root.createDirectory();
    rescan();
}

bool Library::isIgnoredFile (const juce::File& f)
{
    const auto name = f.getFileName();

    return name.startsWithChar ('.') || name.startsWithChar ('_') || name.startsWith ("~$")
        || name.equalsIgnoreCase ("desktop.ini") || name.equalsIgnoreCase ("Thumbs.db")
        || name.endsWithIgnoreCase (".tmp") || name.endsWithIgnoreCase (".partial")
        || name.endsWithIgnoreCase (".crdownload") || name.endsWithIgnoreCase (".icloud")
        || f.isHidden();
}

bool Library::isConflictCopy (const juce::String& fileName)
{
    // Only the names cloud clients give a second copy of topic.xml, never our own temporary files:
    // OneDrive "topic-PCNAME.xml"; Dropbox "topic (x's conflicted copy).xml"; Google Drive "topic (1).xml";
    // iCloud "topic 2.xml" (and "topic2.xml").
    if (! fileName.startsWithIgnoreCase ("topic") || ! fileName.endsWithIgnoreCase (".xml")
        || fileName.equalsIgnoreCase (topicFileName))
        return false;

    const auto rest = fileName.substring (5, fileName.length() - 4).toStdString();
    static const std::regex pattern (R"(^(-.+| ?\(\d+\)| ?\d+| ?\(.*conflict.*\))$)", std::regex::icase);
    return std::regex_match (rest, pattern);
}

bool Library::isTemporaryTopicFile (const juce::String& fileName)
{
    // XmlElement::writeTo() (used before) wrote "topic_temp<hex>.xml" next to topic.xml; a failed swap left them behind.
    return (fileName.startsWithIgnoreCase ("topic_temp") && fileName.endsWithIgnoreCase (".xml"))
        || fileName.equalsIgnoreCase (topicTempFileName);
}

bool Library::isDefaultFolderName (const juce::String& folderName)
{
    static const std::regex pattern (R"(^New topic( \(\d+\))?$)", std::regex::icase);
    return std::regex_match (folderName.toStdString(), pattern);
}

juce::String Library::titleFromUrl (const juce::String& url)
{
    switch (classifyUrl (url))
    {
        case ResourceKind::youtube:    return "YouTube video";
        case ResourceKind::googleDoc:  return "Google document";
        case ResourceKind::pdf: case ResourceKind::image: case ResourceKind::text: case ResourceKind::document:
        case ResourceKind::spreadsheet: case ResourceKind::presentation: case ResourceKind::audio: case ResourceKind::video:
        case ResourceKind::web: case ResourceKind::other:
            break;
    }

    auto host = juce::URL (url.trim()).getDomain();

    if (host.startsWithIgnoreCase ("www."))
        host = host.substring (4);

    return host.isNotEmpty() ? host : url.trim();
}

//==============================================================================
bool Library::loadTopic (const juce::File& folder, Topic& topic)
{
    topic = {};
    topic.folder = folder;

    if (auto xml = juce::parseXMLIfTagMatches (folder.getChildFile (topicFileName), "TOPIC"))
    {
        topic.id = xml->getStringAttribute ("id");
        topic.title = xml->getStringAttribute ("title");
        topic.created = juce::Time::fromISO8601 (xml->getStringAttribute ("created"));
        topic.tags = parseTags (xml->getStringAttribute ("tags"));
        topic.inRotation = xml->getBoolAttribute ("rotation", false);
        topic.noGuitar = xml->getBoolAttribute ("noGuitar", false);

        if (auto* d = xml->getChildByName ("DESCRIPTION"))
            topic.description = d->getAllSubText();

        if (auto* d = xml->getChildByName ("PRIVATE_NOTES"))
            topic.privateNotes = d->getAllSubText();

        for (auto* e : xml->getChildWithTagNameIterator ("RESOURCE"))
        {
            Resource r;
            r.fileName = e->getStringAttribute ("file");
            r.url = e->getStringAttribute ("url");
            r.id = r.fileName.isNotEmpty() ? r.fileName : e->getStringAttribute ("id");
            r.title = e->getStringAttribute ("title");
            r.added = juce::Time::fromISO8601 (e->getStringAttribute ("added"));
            r.tags = parseTags (e->getStringAttribute ("tags"));

            if (auto* d = e->getChildByName ("DESCRIPTION"))
                r.description = d->getAllSubText();

            if (r.id.isEmpty() || (r.fileName.isEmpty() && r.url.isEmpty()) || topic.findResource (r.id) != nullptr)
                continue;

            if (r.fileName.isNotEmpty() && (r.fileName.containsAnyOf ("/\\") || r.fileName == ".."))
                continue;   // never let topic.xml point outside its folder

            topic.resources.push_back (std::move (r));
        }
    }

    if (topic.id.isEmpty())
        topic.id = "dir:" + folder.getFileName();

    if (topic.title.isEmpty())
        topic.title = folder.getFileName();

    if (topic.created == juce::Time())
        topic.created = folder.getCreationTime();

    // Files that topic.xml doesn't list yet (added from another device, by hand, or still syncing).
    auto files = folder.findChildFiles (juce::File::findFiles, false);
    files.sort();

    for (auto& f : files)
    {
        const auto name = f.getFileName();

        if (isTemporaryTopicFile (name))
        {
            // Left by an interrupted save: never a resource. Cleared once it's clearly stale.
            if (f.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::minutes (2))
                f.deleteFile();

            continue;
        }

        if (name.equalsIgnoreCase (topicFileName) || isIgnoredFile (f))
            continue;

        if (isConflictCopy (name))
        {
            topic.conflictFiles.add (name);
            continue;
        }

        if (topic.findResource (name) == nullptr)
        {
            Resource r;
            r.id = r.fileName = name;
            r.title = f.getFileNameWithoutExtension();
            r.added = f.getCreationTime();
            topic.resources.push_back (std::move (r));
        }
    }

    for (auto& r : topic.resources)
        r.isTake = ! r.isLink() && TakeInfo::infoFileFor (topic.getFile (r)).existsAsFile();

    return true;
}

void Library::rescan()
{
    topics.clear();
    inbox.clear();

    if (! root.isDirectory())
        return;

    for (auto& entry : juce::RangedDirectoryIterator (root, false, "*", juce::File::findFilesAndDirectories))
    {
        const auto& f = entry.getFile();

        if (isIgnoredFile (f))
            continue;

        if (entry.isDirectory())
        {
            Topic t;
            loadTopic (f, t);
            topics.push_back (std::move (t));
        }
        else
        {
            inbox.add (f);
        }
    }

    inbox.sort();
    sortTopics();
}

void Library::sortTopics()
{
    std::stable_sort (topics.begin(), topics.end(), [] (const Topic& a, const Topic& b)
    {
        if (a.created != b.created)
            return a.created > b.created;

        return a.title.compareNatural (b.title) < 0;
    });
}

juce::String Library::computeSignature() const
{
    juce::String sig;

    if (! root.isDirectory())
        return sig;

    for (auto& entry : juce::RangedDirectoryIterator (root, true, "*", juce::File::findFilesAndDirectories))
    {
        const auto relative = entry.getFile().getRelativePathFrom (root);

        // App data folders (".practice"...) change while you play: they aren't topics.
        if (relative.startsWithChar ('.') || relative.startsWithChar ('_'))
            continue;

        sig << relative << '|' << entry.getFileSize() << '|'
            << entry.getModificationTime().toMilliseconds() << '\n';
    }

    return juce::String::toHexString (sig.hashCode64()) + ":" + juce::String (sig.length());
}

juce::StringArray Library::getAllTags() const
{
    juce::StringArray all;

    for (auto& t : topics)
    {
        for (auto& tag : t.tags)
            if (! hasTag (all, tag))
                all.add (tag);

        for (auto& r : t.resources)
            for (auto& tag : r.tags)
                if (! hasTag (all, tag))
                    all.add (tag);
    }

    all.sortNatural();
    return all;
}

Topic* Library::findTopic (const juce::String& topicId)
{
    for (auto& t : topics)
        if (t.id == topicId)
            return &t;

    return nullptr;
}

const Topic* Library::findTopicForFile (const juce::File& file) const
{
    for (auto& t : topics)
        if (file.isAChildOf (t.folder))
            return &t;

    return nullptr;
}

juce::File Library::uniqueChild (const juce::File& folder, const juce::String& fileName) const
{
    auto name = juce::File::createLegalFileName (fileName).trim();

    if (name.isEmpty() || name.startsWithChar ('.') || name.startsWithChar ('_'))
        name = "File" + name;

    auto file = folder.getChildFile (name);
    const auto stem = file.getFileNameWithoutExtension(), ext = file.getFileExtension();

    // "name (2).pdf", "name (3).pdf"... like Explorer / Finder.
    for (int n = 2; file.exists() && n < 10000; ++n)
        file = folder.getChildFile (stem + " (" + juce::String (n) + ")" + ext);

    return file;
}

Topic* Library::createTopic (const juce::String& title, const juce::String& topicId)
{
    auto cleanTitle = title.trim();

    if (cleanTitle.isEmpty())
        cleanTitle = "New topic";

    auto folderName = juce::File::createLegalFileName (cleanTitle).trim().substring (0, 60).trim();

    if (folderName.isEmpty() || folderName.startsWithChar ('.') || folderName.startsWithChar ('_'))
        folderName = "Topic " + folderName;

    auto folder = uniqueChild (root, folderName);

    if (! folder.createDirectory())
        return nullptr;

    Topic t;
    t.id = topicId.isNotEmpty() && findTopic (topicId) == nullptr ? topicId : juce::Uuid().toDashedString();
    t.title = cleanTitle;
    t.created = juce::Time::getCurrentTime();
    t.folder = folder;

    if (! saveTopic (t))
        return nullptr;

    const auto id = t.id;
    topics.push_back (std::move (t));
    sortTopics();
    return findTopic (id);
}

std::unique_ptr<juce::XmlElement> Library::topicToXml (const Topic& t)
{
    auto xmlPtr = std::make_unique<juce::XmlElement> ("TOPIC");
    auto& xml = *xmlPtr;
    xml.setAttribute ("id", t.id);
    xml.setAttribute ("title", t.title);
    xml.setAttribute ("created", t.created.toISO8601 (true));

    if (! t.tags.isEmpty())
        xml.setAttribute ("tags", joinTags (t.tags));

    if (t.inRotation)
        xml.setAttribute ("rotation", true);

    if (t.noGuitar)
        xml.setAttribute ("noGuitar", true);

    xml.createNewChildElement ("DESCRIPTION")->addTextElement (t.description);

    if (t.privateNotes.isNotEmpty())
        xml.createNewChildElement ("PRIVATE_NOTES")->addTextElement (t.privateNotes);

    for (auto& r : t.resources)
    {
        auto* e = xml.createNewChildElement ("RESOURCE");

        if (r.isLink())
        {
            e->setAttribute ("id", r.id);
            e->setAttribute ("url", r.url);
        }
        else
        {
            e->setAttribute ("file", r.fileName);
        }

        e->setAttribute ("title", r.title);
        e->setAttribute ("added", r.added.toISO8601 (true));

        if (! r.tags.isEmpty())
            e->setAttribute ("tags", joinTags (r.tags));

        if (r.description.isNotEmpty())
            e->createNewChildElement ("DESCRIPTION")->addTextElement (r.description);
    }

    return xmlPtr;
}

bool Library::saveTopic (const Topic& t)
{
    const auto xml = topicToXml (t);
    t.folder.createDirectory();
    return writeTopicFile (t.folder, xml->toString());
}

bool Library::writeTopicFile (const juce::File& folder, const juce::String& text)
{
    const auto target = folder.getChildFile (topicFileName);

    // Unchanged: leave the file alone, so the cloud client has nothing to upload.
    if (target.existsAsFile() && target.loadFileAsString() == text)
        return true;

    // Write a temporary file whose name we and sync clients skip (dot prefix, .tmp), then swap it in.
    // (XmlElement::writeTo() uses "topic_temp<hex>.xml", which a cloud drive syncs and can leave behind.)
    const auto temp = folder.getChildFile (topicTempFileName);
    temp.deleteFile();

    {
        juce::FileOutputStream out (temp);

        if (! out.openedOk() || ! out.writeText (text, false, false, nullptr))   // as is: compared above next time
            return false;

        out.flush();

        if (out.getStatus().failed())
            return false;
    }

    // The cloud client may hold topic.xml open for a moment while it uploads it.
    for (int attempt = 0; attempt < 10; ++attempt)
    {
        if (temp.replaceFileIn (target))
            return true;

        juce::Thread::sleep (100);
    }

    // Still locked: overwrite in place rather than leave a second file in the folder.
    temp.deleteFile();
    juce::FileOutputStream out (target);

    if (! out.openedOk())
        return false;

    out.setPosition (0);
    out.truncate();
    out.writeText (text, false, false, nullptr);
    out.flush();
    return out.getStatus().wasOk();
}

bool Library::renameFolderAfterTitle (Topic& t)
{
    // Only folders still called "New topic": a folder the user named stays as it is. Songs are remembered by
    // their path (practice log, takes, stems), so a folder that holds audio isn't renamed either.
    if (! isDefaultFolderName (t.folder.getFileName()) || ! t.folder.isDirectory())
        return false;

    auto folderName = juce::File::createLegalFileName (t.title.trim()).trim().substring (0, 60).trim();

    if (folderName.isEmpty() || folderName.startsWithChar ('.') || folderName.startsWithChar ('_')
        || isDefaultFolderName (folderName))
        return false;

    for (auto& f : t.folder.findChildFiles (juce::File::findFiles, false))
        if (classifyFileName (f.getFileName()) == ResourceKind::audio)
            return false;

    const auto target = uniqueChild (root, folderName);

    if (! t.folder.moveFileTo (target))
    {
        juce::Logger::writeToLog ("Library: could not rename " + t.folder.getFullPathName() + " to " + target.getFileName());
        return false;
    }

    juce::Logger::writeToLog ("Library: renamed topic folder \"" + t.folder.getFileName() + "\" to \"" + target.getFileName() + "\"");
    t.folder = target;
    return true;
}

std::vector<Library::TopicFileVersion> Library::getTopicFileVersions (const Topic& t)
{
    std::vector<TopicFileVersion> versions;
    juce::StringArray names (topicFileName);
    names.addArray (t.conflictFiles);

    for (auto& name : names)
    {
        const auto file = t.folder.getChildFile (name);

        if (! file.existsAsFile())
            continue;

        TopicFileVersion v;
        v.fileName = name;
        v.saved = file.getLastModificationTime();

        if (auto xml = juce::parseXMLIfTagMatches (file, "TOPIC"))
        {
            v.title = xml->getStringAttribute ("title");
            for (auto* e : xml->getChildWithTagNameIterator ("RESOURCE"))
                v.resources += e != nullptr ? 1 : 0;
        }

        versions.push_back (v);
    }

    return versions;
}

bool Library::keepTopicFileVersion (const Topic& t, const juce::String& fileName)
{
    if (! fileName.equalsIgnoreCase (topicFileName))
    {
        const auto chosen = t.folder.getChildFile (fileName);
        const auto target = t.folder.getChildFile (topicFileName);

        if (! chosen.existsAsFile() || ! moveToBin (target) || ! chosen.moveFileTo (target))
            return false;
    }

    bool ok = true;

    for (auto& name : t.conflictFiles)
        if (! name.equalsIgnoreCase (fileName))
            ok = moveToBin (t.folder.getChildFile (name)) && ok;

    return ok;
}

bool Library::moveToBin (const juce::File& f)
{
    if (! f.exists())
        return true;

   #if ! JUCE_IOS
    if (f.moveToTrash())
        return true;
   #endif

    return f.isDirectory() ? f.deleteRecursively() : f.deleteFile();
}

bool Library::deleteTopic (const juce::String& topicId)
{
    auto it = std::find_if (topics.begin(), topics.end(), [&] (const Topic& t) { return t.id == topicId; });

    if (it == topics.end() || ! moveToBin (it->folder))
        return false;

    topics.erase (it);
    return true;
}

juce::File Library::copyIn (const juce::URL& source, const juce::File& folder)
{
    const auto sourceName = source.isLocalFile() ? source.getLocalFile().getFileName()
                                                 : juce::URL::removeEscapeChars (source.getFileName());
    auto target = uniqueChild (folder, sourceName);
    folder.createDirectory();

    bool copied = false;

    if (source.isLocalFile())
    {
        copied = source.getLocalFile().copyFileTo (target);
    }
    else if (auto in = source.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)))
    {
        juce::FileOutputStream out (target);
        copied = out.openedOk() && out.writeFromInputStream (*in, -1) >= 0;
        out.flush();
        copied = copied && out.getStatus().wasOk();
    }

    if (! copied)
    {
        target.deleteFile();
        return {};
    }

    // Stems already made for the original (kept in the app's data while it was outside the library) move along,
    // so the song doesn't have to be separated again.
    if (source.isLocalFile())
        adoptStems (source.getLocalFile(), target);

    return target;
}

bool Library::adoptStems (const juce::File& fromSong, const juce::File& toSong)
{
    const auto from = Stems::folderFor (fromSong, root);
    const auto to = Stems::folderFor (toSong, root);

    if (from == to || ! Stems::isComplete (from) || to.exists())
        return false;

    to.getParentDirectory().createDirectory();
    const auto ok = from.moveFileTo (to) || (from.copyDirectoryTo (to) && from.deleteRecursively());

    if (ok)
        juce::Logger::writeToLog ("Stems moved with the song: " + to.getFullPathName());

    return ok;
}

juce::File Library::addToInbox (const juce::URL& source)
{
    auto target = copyIn (source, root);

    if (target != juce::File())
        inbox.addIfNotAlreadyThere (target);

    return target;
}

Resource* Library::addFile (Topic& topic, const juce::URL& source)
{
    const auto target = copyIn (source, topic.folder);

    if (target == juce::File())
        return nullptr;

    Resource r;
    r.id = r.fileName = target.getFileName();
    r.title = target.getFileNameWithoutExtension();
    r.added = juce::Time::getCurrentTime();
    topic.resources.push_back (r);
    saveTopic (topic);
    return topic.findResource (r.id);
}

Resource* Library::moveInboxFile (Topic& topic, const juce::File& inboxFile)
{
    if (! inbox.contains (inboxFile) || ! inboxFile.existsAsFile())
        return nullptr;

    auto target = uniqueChild (topic.folder, inboxFile.getFileName());
    topic.folder.createDirectory();

    if (! inboxFile.moveFileTo (target))
        return nullptr;

    inbox.removeFirstMatchingValue (inboxFile);

    // A take's details go with it.
    const auto info = TakeInfo::infoFileFor (inboxFile);
    const auto isTake = info.existsAsFile();

    if (isTake)
    {
        TakeInfo::infoFileFor (target).getParentDirectory().createDirectory();
        info.moveFileTo (TakeInfo::infoFileFor (target));
    }

    // So do a song's stems.
    const auto stems = Stems::folderFor (inboxFile, root);

    if (stems.isDirectory())
    {
        const auto newStems = Stems::folderFor (target, root);
        newStems.getParentDirectory().createDirectory();
        stems.moveFileTo (newStems);
    }

    Resource r;
    r.id = r.fileName = target.getFileName();
    r.title = target.getFileNameWithoutExtension();
    r.added = juce::Time::getCurrentTime();
    r.isTake = isTake;
    topic.resources.push_back (r);
    saveTopic (topic);
    return topic.findResource (r.id);
}

Resource* Library::addLink (Topic& topic, const juce::String& title, const juce::String& url)
{
    auto cleanUrl = url.trim();

    if (cleanUrl.isEmpty())
        return nullptr;

    if (! cleanUrl.containsChar (':'))
        cleanUrl = "https://" + cleanUrl;

    Resource r;
    r.id = juce::Uuid().toDashedString();
    r.url = cleanUrl;
    r.title = title.trim().isNotEmpty() ? title.trim() : titleFromUrl (cleanUrl);
    r.added = juce::Time::getCurrentTime();
    topic.resources.push_back (r);
    saveTopic (topic);
    return topic.findResource (r.id);
}

bool Library::removeResource (Topic& topic, const juce::String& resourceId)
{
    auto it = std::find_if (topic.resources.begin(), topic.resources.end(),
                            [&] (const Resource& r) { return r.id == resourceId; });

    if (it == topic.resources.end())
        return false;

    if (! it->isLink() && ! moveToBin (topic.getFile (*it)))
        return false;

    if (! it->isLink())
    {
        TakeInfo::infoFileFor (topic.getFile (*it)).deleteFile();
        moveToBin (Stems::folderFor (topic.getFile (*it), root));
    }

    topic.resources.erase (it);
    return saveTopic (topic);
}

bool Library::moveResource (Topic& topic, const juce::String& resourceId, int delta)
{
    auto it = std::find_if (topic.resources.begin(), topic.resources.end(),
                            [&] (const Resource& r) { return r.id == resourceId; });

    if (it == topic.resources.end())
        return false;

    const auto from = (int) std::distance (topic.resources.begin(), it);
    const auto to = juce::jlimit (0, (int) topic.resources.size() - 1, from + delta);

    if (from == to)
        return false;

    auto moved = std::move (topic.resources[(size_t) from]);
    topic.resources.erase (topic.resources.begin() + from);
    topic.resources.insert (topic.resources.begin() + to, std::move (moved));
    return saveTopic (topic);
}

//==============================================================================
juce::File LibraryLocation::getDefaultFolder()
{
   #if JUCE_IOS
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Library");
   #else
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Practice Vibes").getChildFile ("Library");
   #endif
}

juce::File LibraryLocation::load()
{
    if (auto xml = juce::parseXMLIfTagMatches (getLibrarySettingsFile(), "LIBRARY"))
    {
        // The default folder is stored by name, not path: the iOS app container moves between installs.
        if (xml->getBoolAttribute ("default"))
            return getDefaultFolder();

        // iPad: a folder outside the app (cloud drive) is opened through its bookmark.
        if (const auto bookmark = xml->getStringAttribute ("bookmark"); bookmark.isNotEmpty())
        {
            juce::String refreshed;
            const auto folder = FolderAccess::resolve (bookmark, refreshed);

            if (refreshed.isNotEmpty() && folder != juce::File())
            {
                xml->setAttribute ("bookmark", refreshed);
                xml->setAttribute ("folder", folder.getFullPathName());
                xml->writeTo (getLibrarySettingsFile());
            }

            return folder;
        }

        const auto path = xml->getStringAttribute ("folder");

        if (juce::File::isAbsolutePath (path))
            return juce::File (path);
    }

    return {};
}

void LibraryLocation::save (const juce::File& folder)
{
    juce::XmlElement xml ("LIBRARY");

    if (folder == getDefaultFolder())
        xml.setAttribute ("default", true);
    else
        xml.setAttribute ("folder", folder.getFullPathName());

    xml.writeTo (getLibrarySettingsFile());
}

juce::File LibraryLocation::saveChosen (const juce::URL& chosenFolder)
{
   #if JUCE_IOS
    const auto bookmark = FolderAccess::makeBookmark (chosenFolder);

    if (bookmark.isEmpty())
        return {};

    juce::String refreshed;
    const auto folder = FolderAccess::resolve (bookmark, refreshed);

    if (! folder.isDirectory())
        return {};

    if (folder == getDefaultFolder())
    {
        save (folder);
        return folder;
    }

    juce::XmlElement xml ("LIBRARY");
    xml.setAttribute ("folder", folder.getFullPathName());     // for the log / menu only
    xml.setAttribute ("bookmark", refreshed.isNotEmpty() ? refreshed : bookmark);
    xml.writeTo (getLibrarySettingsFile());
    return folder;
   #else
    const auto folder = chosenFolder.getLocalFile();

    if (! folder.isDirectory())
        return {};

    save (folder);
    return folder;
   #endif
}

} // namespace pv
