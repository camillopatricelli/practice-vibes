#pragma once

#include <juce_core/juce_core.h>

#ifndef PV_BUILD_NUMBER
 #define PV_BUILD_NUMBER ""
#endif

/** Version, build number and platform of this build, for the About screen, the log and diagnostic info.
    PV_BUILD_NUMBER is the CI run number (cmake -DPV_BUILD_NUMBER=...); empty in local builds. Plain English:
    callers translate what they show. */
namespace pv::build
{
    inline juce::String version()       { return JUCE_APPLICATION_VERSION_STRING; }

    /** The CI run number, or empty for a local build. */
    inline juce::String number()        { return PV_BUILD_NUMBER; }

    /** "0.1.0 build 123" or "0.1.0 local build" (log and diagnostic info, not translated). */
    inline juce::String versionLine()
    {
        return version() + (number().isNotEmpty() ? " build " + number() : juce::String (" local build"));
    }

    /** CPU architecture this binary runs as (on a universal Mac app: the slice that is running). */
    inline juce::String architecture()
    {
       #if JUCE_ARM && JUCE_64BIT
        return "arm64";
       #elif JUCE_ARM
        return "arm";
       #elif JUCE_INTEL && JUCE_64BIT
        return "x86_64";
       #elif JUCE_INTEL
        return "x86";
       #else
        return "unknown";
       #endif
    }

    inline juce::String platform()
    {
        return juce::SystemStats::getOperatingSystemName() + ", " + architecture();
    }
}
