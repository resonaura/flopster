Flopster by Shiru & Resonaura


Overview

This is a VSTi/AU/VST3 that imitates noises of a floppy disk drive when
it is used for the favorite gimmick of the old MS-DOS days and modern
YouTube times - playing music. It is sample based, the plugin just controls
the samples in a particular way in order to produce more realistic sound.
You can also just use the samples themselves; they are available in the
/samples/ folder.

Original plugin and all samples recorded by Shiru.
macOS port (AU + VST3 + Standalone), cross-platform JUCE rewrite,
keyboard interface, and Standalone app by Resonaura.

You can tweak the samples, or make your own preset by adding a folder
prefixed with a new preset number. The plugin only supports uncompressed
44100 Hz 16-bit mono WAV files.



Supported Formats

  macOS   — AU, VST3, Standalone app
  Windows — VST3, Standalone app



Features

There is not much to control besides volume of different sounds and pitch
deviations, so the GUI only features a few simple knobs and a sample status
display, in form of LEDs that show which kind of sound is currently active
and where the floppy drive head is positioned.

A built-in pixel-art keyboard is shown at the bottom of the plugin window.
In Standalone mode, computer keyboard keys are labelled on each key so you
can play without a MIDI controller.

All kinds of sound that the plugin can play are controlled by note range
and velocity (there is a threshold around the velocity value given below):

- Spindle motor sound. Use note C-6 with any velocity.

- Singular head steps. They do not have musical pitch; they can be used as
  a percussive instrument instead. There are 80 steps, each one sounds
  slightly different. Any note below C-6 with velocity ~15 will play a
  particular step sound, always the same, so you can create a subtle
  pattern out of it. Alternatively, note D-6 with any velocity plays steps
  in sequence.

- Continuous sounds made of repeating singular head steps. Sounds fine for
  really low notes, but gets obviously artificial at musical range pitches.
  Any note below C-6, velocity ~40.

- Continuous sounds made of the head buzz, only available in the C-3..B-5
  range. This is the most common way to make music with a floppy drive.
  The head just moves back and forth, alternating direction every step,
  thus remaining in place. Velocity ~60.

- Continuous sounds made of the head seek, only available in C-3..B-5
  range. This is a more natural way for a floppy drive to work — the head
  moves 80 steps up to the last position, then 80 steps back to the first.
  It produces an even cleaner sound in the middle of movement, but with
  loud clicks when the direction changes.
  Velocity ~120 starts from the initial position; velocity ~90 remembers
  the last head position and resumes from there.

- Four extra sounds: disk push into drive (E-6), insert (F-6),
  eject (G-6), and pull out of drive (A-6).



Built-in Keyboard (Standalone & Plugin)

The pixel-art keyboard spans C3..E6. You can click keys with the mouse.
In Standalone mode, computer keyboard keys are also supported.

FL Studio-style keyboard layout:

  Upper keyboard row (one octave up):
    Key:  Q  2  W  3  E  R  5  T  6  Y  7  U  I  9  O  0  P
    Note: C5 C#5 D5 D#5 E5 F5 F#5 G5 G#5 A5 A#5 B5 C6 C#6 D6 D#6 E6

  Lower keyboard row:
    Key:  Z  S  X  D  C  V  G  B  H  N  J  M  ,  L  .  ;  /
    Note: C4 C#4 D4 D#4 E4 F4 F#4 G4 G#4 A4 A#4 B4 C5 C#5 D5 D#5 E5

Velocity sent from computer keyboard keys is ~80, which maps to
"head buzz" mode — the most musical sound.



Building from Source

Prerequisites:
  macOS  — Xcode Command Line Tools, CMake, Ninja, Git
  Windows — CMake, Visual Studio 2022 (or Ninja + clang/MSVC), Git

JUCE 8 is fetched automatically if not already present.

macOS (builds AU + VST3 + Standalone):

  cd plugin
  bash build.sh             # Release build
  bash build.sh --debug     # Debug build
  bash build.sh --rebuild   # Clean rebuild

  To install into the system (strips Gatekeeper quarantine, validates AU):

  bash install.sh           # normal install
  bash install.sh --rebuild # rebuild then install

Windows (builds VST3 + Standalone):

  cd plugin
  build.bat                 # Release build
  build.bat --debug         # Debug build
  build.bat --rebuild       # Clean rebuild

  To install:

  install.bat               # copies VST3 to %CommonProgramFiles%\VST3\
  install.bat --rebuild     # rebuild then install

On Windows with Visual Studio installed, the scripts detect it
automatically and use the VS generator. Ninja is used as fallback.



Install Paths

  macOS:
    AU         ~/Library/Audio/Plug-Ins/Components/Flopster.component
    VST3       ~/Library/Audio/Plug-Ins/VST3/Flopster.vst3
    Standalone /Applications/Flopster.app

  Windows:
    VST3       %CommonProgramFiles%\VST3\Flopster.vst3
               %LOCALAPPDATA%\Programs\VstPlugins\Flopster.vst3
    Standalone Desktop\Flopster.exe



License

The plugin and its source code come without any warranty. You can
redistribute it and/or modify it under the terms of the WTFPL.
See http://www.wtfpl.net/ for more details.



History

v1.22 (Resonaura port)
      - Full cross-platform JUCE 8 rewrite (macOS AU + VST3 + Standalone,
        Windows VST3 + Standalone)
      - Built-in pixel-art keyboard with mouse and computer keyboard input
      - FL Studio-style keyboard layout in Standalone mode
      - CMake-based build system, automated install scripts
      - Gatekeeper quarantine removal on macOS

v1.21 16.01.20 - Two more sample sets, volume balanced out across the sets
v1.2  13.01.20 - Checks for sample data presence and integrity, three
                 more sample sets
v1.1  28.07.19 - GUI, synth engine improvements, detune and octave shift,
                 pitch bend support, second sample set
v1.01 03.06.19 - x64 support
v1.0  30.06.17 - Initial release



Contact

Original author (Shiru):
  Mail:    shiru@mail.ru
  Web:     http://shiru.untergrund.net
  Support: https://www.patreon.com/shiru8bit

macOS/cross-platform port (Resonaura):
  GitHub:  https://github.com/resonaura
