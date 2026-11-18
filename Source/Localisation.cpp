#include "Localisation.h"
#include "LanguageData.h"

namespace pv::i18n
{

namespace
{
    /** The built-in translation for a language code ("it" -> Resources/Languages/it.txt), or empty. */
    juce::String embeddedFileFor (const juce::String& code)
    {
        for (int i = 0; i < LanguageData::namedResourceListSize; ++i)
        {
            const auto* name = LanguageData::namedResourceList[i];

            if (juce::File::createFileWithoutCheckingPath (LanguageData::getNamedResourceOriginalFilename (name))
                    .getFileNameWithoutExtension().equalsIgnoreCase (code))
            {
                int size = 0;
                const auto* data = LanguageData::getNamedResource (name, size);
                return juce::String::createStringFromData (data, size);     // handles a byte-order mark
            }
        }

        return {};
    }

    juce::StringArray embeddedCodes()
    {
        juce::StringArray codes;

        for (int i = 0; i < LanguageData::namedResourceListSize; ++i)
            codes.add (juce::File::createFileWithoutCheckingPath (
                           LanguageData::getNamedResourceOriginalFilename (LanguageData::namedResourceList[i]))
                           .getFileNameWithoutExtension().toLowerCase());

        return codes;
    }

    juce::String languageNameIn (const juce::String& fileText, const juce::String& code)
    {
        juce::LocalisedStrings strings (fileText, false);
        return strings.getLanguageName().isNotEmpty() ? strings.getLanguageName() : code;
    }
}

juce::String translationFileFor (const juce::String& code, const juce::File& appData)
{
    const auto userFile = detail::userLanguageFolder (appData).getChildFile (code + ".txt");

    if (userFile.existsAsFile())
        return userFile.loadFileAsString();

    return embeddedFileFor (code);
}

juce::StringPairArray availableLanguages (const juce::File& appData)
{
    juce::StringArray codes (embeddedCodes());

    for (const auto& f : detail::userLanguageFolder (appData).findChildFiles (juce::File::findFiles, false, "*.txt"))
        codes.addIfNotAlreadyThere (f.getFileNameWithoutExtension().toLowerCase());

    codes.removeString ("template");
    codes.removeString ("en");
    codes.removeString (pseudoCode);
    codes.sort (true);

    juce::StringPairArray list;
    list.set ("en", "English");

    for (const auto& code : codes)
        list.set (code, languageNameIn (translationFileFor (code, appData), code));

    list.set (pseudoCode, "Test: long accented text");
    return list;
}

void initialise (const juce::String& choice, const juce::File& appData)
{
    auto& s = detail::state();
    const auto forced = juce::SystemStats::getEnvironmentVariable ("PV_LANGUAGE", {});     // developers: try a language without touching ui.xml
    const auto wanted = forced.isNotEmpty() ? forced : choice.isNotEmpty() ? choice : deviceLanguage();

    s.pseudo = wanted == pseudoCode;
    s.code = "en";
    juce::LocalisedStrings::setCurrentMappings (nullptr);

    if (s.pseudo)
    {
        s.code = pseudoCode;
        return;
    }

    if (wanted == "en" || wanted.isEmpty())
        return;

    const auto text = translationFileFor (wanted, appData);

    if (text.isEmpty())
        return;     // no translation for the device language: English

    juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (text, false));
    s.code = wanted;
}

} // namespace pv::i18n
