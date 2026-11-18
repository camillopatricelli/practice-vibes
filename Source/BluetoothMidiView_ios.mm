#include "BluetoothMidiView.h"
#include "Theme.h"
#include "Localisation.h"

#include <juce_gui_extra/juce_gui_extra.h>

#import <UIKit/UIKit.h>
#import <CoreAudioKit/CoreAudioKit.h>

namespace pv
{

namespace
{
    UIColor* toUI (juce::Colour c)
    {
        return [UIColor colorWithRed: c.getFloatRed() green: c.getFloatGreen() blue: c.getFloatBlue() alpha: c.getFloatAlpha()];
    }

    /** CABTMIDICentralViewController (a UITableViewController) inside a card, coloured like the app. */
    class BluetoothMidiView final : public juce::Component
    {
    public:
        BluetoothMidiView()
        {
            getProperties().set ("overlayWidth", 560);
            getProperties().set ("overlayHeight", 660);

            hint.setText (tr ("Switch the device on (in pairing mode, if it has one), then tap it below to connect. "
                          "Once connected it appears as a MIDI input."), juce::dontSendNotification);
            hint.setFont (theme::textFont (15.0f));
            hint.setColour (juce::Label::textColourId, theme::muted());
            hint.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (hint);

            controller = [[CABTMIDICentralViewController alloc] init];
            applyTheme();
            nativeView.setView ([controller view]);
            addAndMakeVisible (nativeView);

            theme::addListener (this, [this] (const theme::Palette&) { applyTheme(); repaint(); });
        }

        ~BluetoothMidiView() override
        {
            theme::removeListener (this);
            nativeView.setView (nullptr);
            [controller release];
        }

        void paint (juce::Graphics& g) override
        {
            theme::paintCard (g, getLocalBounds().toFloat().reduced (1.0f));
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (20, 16);
            hint.setBounds (r.removeFromTop (48));
            r.removeFromTop (8);
            nativeView.setBounds (r);
        }

    private:
        void applyTheme()
        {
            // Light/dark text and system greys follow the app's theme, not the iPad's.
            if (@available (iOS 13.0, *))
                controller.overrideUserInterfaceStyle = theme::isDark() ? UIUserInterfaceStyleDark : UIUserInterfaceStyleLight;

            // Rows a step lighter than the card, amber accents.
            [[UITableViewCell appearanceWhenContainedInInstancesOfClasses: @[ [CABTMIDICentralViewController class] ]]
                setBackgroundColor: toUI (theme::raised())];

            UITableView* table = controller.tableView;
            table.backgroundColor = toUI (theme::card());
            table.separatorColor = toUI (theme::line());
            table.tintColor = toUI (theme::accent());
            [controller view].tintColor = toUI (theme::accent());
            [table reloadData];
        }

        CABTMIDICentralViewController* controller = nil;
        juce::UIViewComponent nativeView;
        juce::Label hint;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BluetoothMidiView)
    };
}

std::unique_ptr<juce::Component> createBluetoothMidiView()
{
    if (NSClassFromString (@"CABTMIDICentralViewController") == nil)
        return nullptr;

    return std::make_unique<BluetoothMidiView>();
}

} // namespace pv
