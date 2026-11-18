#pragma once

// The one place PipeHat's version is set. Everything else reads it from here:
//   - CMakeLists.txt parses these lines for project(VERSION),
//   - resource.rc builds the VERSIONINFO resource from them (what Windows file
//     properties and the Notepad++ plugin list validator read), and
//   - PluginDefs.h derives HL7_PLUGIN_VERSION, which the About box and Check for
//     Updates compare against the latest release tag.
// v2.4.0 shipped reporting "2.3.0" because the version lived in three places, so
// Check for Updates told every v2.4.0 user an update was waiting. CI now fails if the
// DLL's FileVersion disagrees with this file, or a release tag disagrees with it.
//
// Keep PIPEHAT_VERSION_STR equal to MAJOR.MINOR.PATCH; the resource compiler cannot
// stringize, so it is written out and the CI check holds the two together.

#define PIPEHAT_VERSION_MAJOR 2
#define PIPEHAT_VERSION_MINOR 5
#define PIPEHAT_VERSION_PATCH 0
#define PIPEHAT_VERSION_STR "2.5.0"
