#pragma once

#include <juce_core/juce_core.h>

/** Translations of the user interface.

    Every piece of text the user sees goes through one of these (log lines, file names and XML values stay English):

        tr ("Start")                                    plain text
        tr ("Bar {n} of {total}", bar, total)           named slots, filled in the order they appear in the English text
        trn ("{n} bar", "{n} bars", count)              plurals: {n} is the count
        trc ("short month", "May")                      same English word, different meanings / translations
        i18n::date (when, "%a %d %b")                   dates: the format itself is translatable, names come from tr()
        i18n::decimal (1.5, 1)                          "1.5" or "1,5" depending on the language

    The English text is the key. A translation file is a JUCE LocalisedStrings file (see Tools/i18n_strings.py,
    which collects every key into Resources/Languages/template.txt):

        language: Italiano
        countries: it
        "Start" = "Avvia"
        "Bar {n} of {total}" = "Battuta {n} di {total}"
        "{n} bar|{n} bars" = "{n} battuta|{n} battute"            plural forms of the language, separated by |
        "short month::May" = "mag"

    Translators may reorder the {slots}; they must keep their names. Words on the do-not-translate list
    (Looper, Loop, note names C D E...) stay as they are in every language.

    The language is chosen once at start-up (ui.xml "language": empty = the device language). Changing it needs a
    restart. "pseudo" is a test language: accented, ~30% longer text in brackets, so unwrapped strings (plain
    English) and clipped labels are easy to spot before any real translation exists.
*/
namespace pv::i18n
{

inline const juce::String pseudoCode { "pseudo" };

namespace detail
{
    struct State
    {
        juce::String code { "en" };     // language in use: "en", "it", ..., or pseudoCode
        bool pseudo = false;
    };

    inline State& state()
    {
        static State s;
        return s;
    }

    /** Languages that have a translation file: embedded ones (none yet) + <app data>/languages/<code>.txt. */
    inline juce::File userLanguageFolder (const juce::File& appData) { return appData.getChildFile ("languages"); }

    inline juce::String lookUp (const juce::String& key, const juce::String& fallback)
    {
        if (auto* m = juce::LocalisedStrings::getCurrentMappings())
            return m->translate (key, fallback);

        return fallback;
    }

    /** Accented look-alikes for a-z / A-Z, so pseudo text stays readable. */
    inline juce::juce_wchar accent (juce::juce_wchar c)
    {
        static const juce::String lower = juce::String (juce::CharPointer_UTF8 (
            "\xc3\xa5\xc6\x80\xc3\xa7\xc3\xb0\xc3\xa9\xc6\x92\xc4\x9d\xc4\xa5\xc3\xae\xc4\xb5\xc4\xb7\xc4\xbc\xe1\xb8\xbf"
            "\xc3\xb1\xc3\xb6\xc3\xbe\xc7\xab\xc5\x95\xc5\xa1\xc5\xa3\xc3\xbb\xe1\xb9\xbd\xc5\xb5\xe1\xba\x8b\xc3\xbd\xc5\xbe"));
        static const juce::String upper = juce::String (juce::CharPointer_UTF8 (
            "\xc3\x85\xc6\x81\xc3\x87\xc3\x90\xc3\x89\xc6\x91\xc4\x9c\xc4\xa4\xc3\x8e\xc4\xb4\xc4\xb6\xc4\xbb\xe1\xb8\xbe"
            "\xc3\x91\xc3\x96\xc3\x9e\xc7\xaa\xc5\x94\xc5\xa0\xc5\xa2\xc3\x9b\xe1\xb9\xbc\xc5\xb4\xe1\xba\x8a\xc3\x9d\xc5\xbd"));

        if (c >= 'a' && c <= 'z') return lower[(int) (c - 'a')];
        if (c >= 'A' && c <= 'Z') return upper[(int) (c - 'A')];
        return c;
    }

    /** Pseudo version of a template: letters accented, {slots} and %x date codes untouched, ~30% longer, in brackets. */
    inline juce::String pseudoOf (const juce::String& text)
    {
        if (text.isEmpty())
            return text;

        juce::String out ("[");
        int letters = 0;

        for (int i = 0; i < text.length(); ++i)
        {
            const auto c = text[i];

            if (c == '{')
            {
                const auto close = text.indexOfChar (i, '}');

                if (close > i)
                {
                    out << text.substring (i, close + 1);
                    i = close;
                    continue;
                }
            }

            if (c == '%' && i + 1 < text.length())
            {
                out << text.substring (i, i + 2);
                ++i;
                continue;
            }

            if (juce::CharacterFunctions::isLetter (c))
                ++letters;

            out << juce::String::charToString (accent (c));
        }

        return out + juce::String::repeatedString ("~", juce::jmax (1, (letters * 3 + 9) / 10)) + "]";
    }

    /** The template for a key in the current language (English when there's no translation). */
    inline juce::String templateFor (const juce::String& key, const juce::String& english)
    {
        if (state().pseudo)
            return pseudoOf (english);

        return lookUp (key, english);
    }

    /** "{name}" slots in the order they first appear. */
    inline juce::StringArray slotNames (const juce::String& english)
    {
        juce::StringArray names;

        for (int i = english.indexOfChar ('{'); i >= 0; i = english.indexOfChar (i + 1, '{'))
        {
            const auto close = english.indexOfChar (i, '}');

            if (close < 0)
                break;

            names.addIfNotAlreadyThere (english.substring (i + 1, close));
        }

        return names;
    }

    inline juce::String toText (const juce::String& s)  { return s; }
    inline juce::String toText (const char* s)          { return juce::String (juce::CharPointer_UTF8 (s)); }
    inline juce::String toText (char c)                 { return juce::String::charToString (c); }
    template <typename T>
    juce::String toText (const T& v)                    { return juce::String (v); }

    inline juce::String fill (juce::String text, const juce::StringArray& names, const juce::StringArray& values)
    {
        for (int i = 0; i < names.size() && i < values.size(); ++i)
            text = text.replace ("{" + names[i] + "}", values[i]);

        return text;
    }

    /** CLDR-style plural category, reduced to what our languages need: index of the form to use. */
    inline int pluralIndex (const juce::String& code, juce::int64 n, int numForms)
    {
        if (numForms <= 1)
            return 0;

        if (code == "fr" || code == "pt")
            return (n == 0 || n == 1) ? 0 : 1;

        return n == 1 ? 0 : 1;     // en, it, es, de...
    }
}

//==============================================================================
/** The language in use ("en", "it"... or pseudoCode). */
inline juce::String currentLanguage()     { return detail::state().code; }
inline bool usesDecimalComma()
{
    const auto& c = detail::state().code;
    return c == "it" || c == "es" || c == "fr" || c == "de" || c == "pt";
}

/** The device language as a two-letter code ("it" for "it-IT"). */
inline juce::String deviceLanguage()
{
    auto code = juce::SystemStats::getUserLanguage().toLowerCase();
    return code.upToFirstOccurrenceOf ("-", false, false).upToFirstOccurrenceOf ("_", false, false).substring (0, 2);
}

/** The translation file text for a language, or empty when there's none. Embedded files come later (BinaryData);
    a file in <app data>/languages/<code>.txt wins, so a translator can try a translation without a new build. */
inline juce::String translationFileFor (const juce::String& code, const juce::File& appData)
{
    const auto userFile = detail::userLanguageFolder (appData).getChildFile (code + ".txt");

    if (userFile.existsAsFile())
        return userFile.loadFileAsString();

    return {};
}

/** Languages the user can pick, as code + name in that language. English and the test language always. */
inline juce::StringPairArray availableLanguages (const juce::File& appData)
{
    juce::StringPairArray list;
    list.set ("en", "English");

    for (const auto& f : detail::userLanguageFolder (appData).findChildFiles (juce::File::findFiles, false, "*.txt"))
    {
        const auto code = f.getFileNameWithoutExtension().toLowerCase();

        if (code == "template" || code == "en")
            continue;

        juce::LocalisedStrings strings (f.loadFileAsString(), false);
        list.set (code, strings.getLanguageName().isNotEmpty() ? strings.getLanguageName() : code);
    }

    list.set (pseudoCode, "Test: long accented text");
    return list;
}

/** Call once at start-up, before any component exists. choice = ui.xml "language" (empty = the device language). */
inline void initialise (const juce::String& choice, const juce::File& appData)
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

/** For tests: switch without files. mappings may be null (English). */
inline void setLanguageForTests (const juce::String& code, juce::LocalisedStrings* mappings)
{
    detail::state().code = code;
    detail::state().pseudo = code == pseudoCode;
    juce::LocalisedStrings::setCurrentMappings (mappings);
}

//==============================================================================
/** Marks English text that is stored or compared as data (built-in pattern names, plugin kinds...) but shown
    with tr (value) where it's displayed. Returns the text unchanged; Tools/i18n_strings.py collects it as a key. */
constexpr const char* key (const char* english) noexcept   { return english; }

/** A number with a fixed number of decimals, with the language's decimal separator. */
inline juce::String decimal (double value, int places)
{
    auto s = juce::String (value, places);
    return usesDecimalComma() ? s.replaceCharacter ('.', ',') : s;
}

/** Like decimal(), but drops trailing zeros: 120 / 92.5 / 1.25. */
inline juce::String number (double value, int maxPlaces)
{
    auto s = juce::String (value, maxPlaces);

    if (s.containsChar ('.'))
        s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");

    return usesDecimalComma() ? s.replaceCharacter ('.', ',') : s;
}

} // namespace pv::i18n

namespace pv
{

/** Translated text; "{name}" slots are filled with args in the order the slots first appear in the English text. */
template <typename... Args>
juce::String tr (const juce::String& english, const Args&... args)
{
    auto text = i18n::detail::templateFor (english, english);

    if constexpr (sizeof... (Args) == 0)
        return text;
    else
        return i18n::detail::fill (text, i18n::detail::slotNames (english), { i18n::detail::toText (args)... });
}

/** Translated text where the same English words need different translations: the key is "context::english". */
template <typename... Args>
juce::String trc (const juce::String& context, const juce::String& english, const Args&... args)
{
    auto text = i18n::detail::templateFor (context + "::" + english, english);

    if constexpr (sizeof... (Args) == 0)
        return text;
    else
        return i18n::detail::fill (text, i18n::detail::slotNames (english), { i18n::detail::toText (args)... });
}

/** Plural: {n} is the count; other slots follow it in order of appearance in the English text.
    The key is "one|other"; a translation lists its language's forms separated by |. */
template <typename... Args>
juce::String trn (const juce::String& one, const juce::String& other, juce::int64 n, const Args&... args)
{
    const auto key = one + "|" + other;
    juce::StringArray forms;

    if (i18n::detail::state().pseudo)
        forms.addArray ({ i18n::detail::pseudoOf (one), i18n::detail::pseudoOf (other) });
    else
        forms.addTokens (i18n::detail::lookUp (key, key), "|", {});

    const auto chosen = forms[i18n::detail::pluralIndex (i18n::detail::state().code, n, forms.size())];

    auto names = i18n::detail::slotNames (other);
    names.removeString ("n");
    names.insert (0, "n");
    return i18n::detail::fill (chosen, names, { juce::String (n), i18n::detail::toText (args)... });
}

namespace i18n
{
    /** A date with a translatable strftime-style format; %a %A %b %B become translated names. */
    inline juce::String date (const juce::Time& t, const juce::String& englishFormat)
    {
        static const char* shortDays[]   = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        static const char* longDays[]    = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
        static const char* shortMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        static const char* longMonths[]  = { "January", "February", "March", "April", "May", "June", "July", "August",
                                             "September", "October", "November", "December" };

        // The format itself is a key, so a language can reorder it ("%d %b" -> "%b%d日").
        const auto format = detail::lookUp ("date format::" + englishFormat, englishFormat);
        juce::String out;

        for (int i = 0; i < format.length(); ++i)
        {
            if (format[i] != '%' || i + 1 >= format.length())
            {
                out << juce::String::charToString (format[i]);
                continue;
            }

            const auto code = format[++i];

            switch (code)
            {
                case 'a': out << trc ("short weekday", shortDays[t.getDayOfWeek()]); break;
                case 'A': out << trc ("weekday", longDays[t.getDayOfWeek()]); break;
                case 'b': out << trc ("short month", shortMonths[t.getMonth()]); break;
                case 'B': out << trc ("month", longMonths[t.getMonth()]); break;
                default:  out << t.formatted (juce::String ("%") + juce::String::charToString (code)); break;
            }
        }

        return out;
    }
}

} // namespace pv
