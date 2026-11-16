#include "Theme.h"

namespace pv::theme
{
namespace
{
    // Taken from the approved mock-up (notes/mockups/practice-restyle.html, look "Warm stage").
    const Palette warmDark
    {
        juce::Colour (0xff17110f),  // background
        juce::Colour (0xff241a16),  // card
        juce::Colour (0xff2f241f),  // raised
        juce::Colour (0xff2a201b),  // field
        juce::Colour (0x1fffd6aa),  // line
        juce::Colour (0xfff6ebe1),  // text
        juce::Colour (0xffb19d8e),  // muted
        juce::Colour (0xfff5a524),  // accent
        juce::Colour (0xffffbf5e),  // highlight
        juce::Colour (0xff1b120d),  // onAccent
        juce::Colour (0xff3a2d26),  // track
        juce::Colour (0xff5a4538),  // wave
        juce::Colour (0xffffb547),  // wavePlayed
        juce::Colour (0xffff6f61),  // wavePlayed2
        juce::Colour (0xff1f1714),  // waveBackground
        juce::Colour (0xfffff4e6),  // playhead
        juce::Colour (0xffff6f61),  // record
        juce::Colour (0xff7bd88f),  // good
        juce::Colour (0xff17110f),  // thumb
        juce::Colour (0xff000000),  // shadow
    };

    const Palette warmLight
    {
        juce::Colour (0xfff3eadf),  // background
        juce::Colour (0xfffffaf3),  // card
        juce::Colour (0xffece0d0),  // raised
        juce::Colour (0xffe9ddcc),  // field
        juce::Colour (0x246e4623),  // line
        juce::Colour (0xff2b1d14),  // text
        juce::Colour (0xff86705e),  // muted
        juce::Colour (0xfff5a524),  // accent
        juce::Colour (0xffb5700a),  // highlight (the mock-up's deeper "ink" amber: readable on paper)
        juce::Colour (0xff2b1d14),  // onAccent
        juce::Colour (0xffe6d8c6),  // track
        juce::Colour (0xffdccab4),  // wave
        juce::Colour (0xfff7a72a),  // wavePlayed
        juce::Colour (0xfff0705e),  // wavePlayed2
        juce::Colour (0xfffffaf3),  // waveBackground
        juce::Colour (0xff2b1d14),  // playhead
        juce::Colour (0xffe5484d),  // record
        juce::Colour (0xff2f9e55),  // good
        juce::Colour (0xfffffaf3),  // thumb
        juce::Colour (0xff4a2c14),  // shadow
    };

    /** The roles in the order a stored colour is matched against them (first match wins), so roles that share an
        RGB in one palette (text / playhead / onAccent in light) map to the one components actually store. */
    constexpr juce::Colour Palette::* matchOrder[]
    {
        &Palette::text, &Palette::muted, &Palette::background, &Palette::card, &Palette::raised, &Palette::field,
        &Palette::line, &Palette::accent, &Palette::highlight, &Palette::onAccent, &Palette::record, &Palette::good,
        &Palette::track, &Palette::shadow, &Palette::wave, &Palette::wavePlayed, &Palette::wavePlayed2,
        &Palette::waveBackground, &Palette::playhead, &Palette::thumb,
    };

    struct State final : private juce::DarkModeSettingListener
    {
        Mode mode = Mode::dark;
        const Palette* current = &warmDark;
        std::vector<std::pair<void*, std::function<void (const Palette&)>>> listeners;
        bool listeningToSystem = false;

        ~State() override
        {
            // Desktop may already be gone at static destruction: only unregister while it exists.
            if (listeningToSystem && juce::Desktop::getInstanceWithoutCreating() != nullptr)
                juce::Desktop::getInstance().removeDarkModeSettingListener (this);
        }

        void update()
        {
            const bool wantSystem = mode == Mode::system;

            if (wantSystem != listeningToSystem)
            {
                if (wantSystem) juce::Desktop::getInstance().addDarkModeSettingListener (this);
                else            juce::Desktop::getInstance().removeDarkModeSettingListener (this);

                listeningToSystem = wantSystem;
            }

            const bool dark = mode == Mode::dark || (mode == Mode::system && juce::Desktop::getInstance().isDarkModeActive());
            const auto* next = dark ? &warmDark : &warmLight;

            if (next == current)
                return;

            const auto* old = current;
            current = next;

            const auto copy = listeners;    // a listener may remove itself
            for (auto& [owner, fn] : copy)
                fn (*old);
        }

        void darkModeSettingChanged() override  { update(); }
    };

    State& state()
    {
        static State s;
        return s;
    }

    void remap (juce::Component& c, const Palette& from, const Palette& to)
    {
        static const juce::String prefix ("jcclr_");

        // Collect first: setColour() changes the property set being iterated.
        std::vector<std::pair<int, juce::Colour>> changes;
        const auto& props = c.getProperties();

        for (int i = 0; i < props.size(); ++i)
        {
            const auto name = props.getName (i).toString();

            if (! name.startsWith (prefix))
                continue;

            const auto stored = juce::Colour ((juce::uint32) static_cast<int> (props.getValueAt (i)));
            const auto rgb = stored.withAlpha ((juce::uint8) 255);

            for (auto role : matchOrder)
            {
                if ((from.*role).withAlpha ((juce::uint8) 255) == rgb)
                {
                    // The role's own alpha (line) times whatever alpha the stored colour added on top.
                    const float extra = (from.*role).getFloatAlpha() > 0.0f ? stored.getFloatAlpha() / (from.*role).getFloatAlpha() : 1.0f;
                    changes.emplace_back (name.substring (prefix.length()).getHexValue32(),
                                          (to.*role).withMultipliedAlpha (juce::jlimit (0.0f, 1.0f, extra)));
                    break;
                }
            }
        }

        for (auto& [id, colour] : changes)
            c.setColour (id, colour);

        // A text field's placeholder colour isn't a colour property (and has no getter): give it the new muted text.
        // Text already typed keeps the colour it was typed in: re-colour it too.
        if (auto* editor = dynamic_cast<juce::TextEditor*> (&c))
        {
            if (const auto placeholder = editor->getTextToShowWhenEmpty(); placeholder.isNotEmpty())
                editor->setTextToShowWhenEmpty (placeholder, to.text.withAlpha (0.38f));

            editor->applyColourToAllText (editor->findColour (juce::TextEditor::textColourId), false);
        }

        for (auto* child : c.getChildren())
            remap (*child, from, to);
    }
}

const Palette& palette()   { return *state().current; }
bool isDark()              { return state().current == &warmDark; }
Mode getMode()             { return state().mode; }

void setMode (Mode m)
{
    state().mode = m;
    state().update();
}

juce::String modeName (Mode m)
{
    switch (m)
    {
        case Mode::system: return "system";
        case Mode::light:  return "light";
        case Mode::dark:   break;
    }

    return "dark";
}

Mode modeFromName (const juce::String& name)
{
    if (name == "system") return Mode::system;
    if (name == "light")  return Mode::light;
    return Mode::dark;
}

void addListener (void* owner, std::function<void (const Palette&)> fn)
{
    state().listeners.emplace_back (owner, std::move (fn));
}

void removeListener (void* owner)
{
    auto& l = state().listeners;
    l.erase (std::remove_if (l.begin(), l.end(), [owner] (auto& p) { return p.first == owner; }), l.end());
}

void refreshComponents (juce::Component& root, const Palette& oldPalette)
{
    if (&oldPalette != &palette())
        remap (root, oldPalette, palette());

    root.repaint();
}

} // namespace pv::theme
