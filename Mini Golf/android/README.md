# Building Mini Golf for Android

Everything you need to go from a fresh checkout to the game running on an Android handheld.
Nothing here needs root, a package manager, or an Android Studio install — the three pieces of
toolchain are downloads that unpack into your home directory and can be deleted afterwards.

Written out in full because the Windows and Switch builds hide their toolchains in a container
and this one does not. If you would rather not read it: install the three things in the next
section, then run `tools/android-build.sh install`.

## What you need

| | |
|---|---|
| A 64-bit Linux or macOS machine | to build on. The commands below are Linux; on a Mac the NDK and SDK downloads are the `darwin` ones and the paths are the same. |
| A Java runtime | 17 or newer. `d8` and `apksigner` are Java programs. You do **not** need a JDK — nothing here compiles Java. |
| An arm64 Android device | API 21 (Android 5.0) or newer, with USB debugging on. |
| Your own copy of the game | the `88888` folder, as ever. Not included, not obtainable here. |

`adb` comes with the platform-tools you install below, or from your distribution
(`android-tools` on Fedora, `adb` on Debian and Ubuntu).

## The three downloads

They can live anywhere; the build script looks in `~/Android` by default, and takes
`ANDROID_NDK`, `ANDROID_SDK` and `SDL3_ANDROID` from the environment if you put them elsewhere.

### 1. The NDK

The C++ cross-compiler. SDL asks for r28c or newer.

```sh
mkdir -p ~/Android && cd ~/Android
curl -LO https://dl.google.com/android/repository/android-ndk-r28c-linux.zip
unzip -q android-ndk-r28c-linux.zip     # 689 MB down, 2.2 GB unpacked
rm android-ndk-r28c-linux.zip
```

That gives you `~/Android/android-ndk-r28c`.

### 2. The SDK platform and build-tools

Not the whole of Android Studio — just `aapt2`, `d8`, `zipalign` and `apksigner`, plus one
platform to compile against. The command-line tools include the downloader that fetches them.

```sh
cd ~/Android
curl -LO https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
unzip -q commandlinetools-linux-11076708_latest.zip -d cmdline
rm commandlinetools-linux-11076708_latest.zip

export JAVA_HOME=$(dirname $(dirname $(readlink -f $(command -v java))))
yes | ./cmdline/cmdline-tools/bin/sdkmanager --sdk_root="$HOME/Android/Sdk" \
    "platform-tools" "platforms;android-35" "build-tools;35.0.0"
```

The `yes |` accepts the SDK licences. That gives you `~/Android/Sdk`, about 1 GB, and `adb` in
`~/Android/Sdk/platform-tools`.

### 3. SDL3, built for Android

SDL publishes an Android build as an `.aar`, which is a Gradle package. This project does not use
Gradle, and the `.aar` is helpfully also a Python script that unpacks itself into an ordinary
prefix — so run it as one.

```sh
mkdir -p ~/Android/sdl3 && cd ~/Android/sdl3
curl -LO https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-devel-3.4.16-android.zip
unzip -q SDL3-devel-3.4.16-android.zip
python3 SDL3-3.4.16.aar -o prefix
```

That gives you `~/Android/sdl3/prefix`, holding SDL's headers, `libSDL3.so` for each ABI, its
Java classes as a `.jar`, and a CMake package that describes them.

If you take a different SDL version, the only thing that has to match is the directory name:
the build script globs the `.jar`, so `SDL3-3.5.0.aar` unpacked to the same `prefix` needs no
change anywhere.

## Building

```sh
cd "Mini Golf"
tools/android-build.sh install
```

`install` is optional — without it you get `build-android/minigolf.apk` and nothing is touched on
the device. `clean` throws the build tree away first.

If one of the three downloads is missing the script names the exact path it looked for and
stops, rather than failing somewhere deep in CMake.

## Putting the game's files on the device

The app looks for its `88888` folder in its own external storage:

    /sdcard/Android/data/org.ipodrecomp.minigolf/files/88888

If it isn't there, the app says so on screen — with the path, and with what is actually wrong if
some of the files are there but damaged. There is no file browser on this build, on purpose: the
Switch build does the same thing for the same reason, and a folder of 122 files is not something
Android's document picker is good at handing over.

Since Android 11, `adb push` cannot always write into `Android/data` directly. Push to a
scratch directory and copy it across on the device:

```sh
adb push /path/to/88888 /data/local/tmp/
adb shell "mkdir -p /sdcard/Android/data/org.ipodrecomp.minigolf/files"
adb shell "cp -r /data/local/tmp/88888 /sdcard/Android/data/org.ipodrecomp.minigolf/files/"
adb shell "rm -rf /data/local/tmp/88888"
```

A file manager on the device works too, if it can reach `Android/data`.

## The launcher icon

Optional. Without one the app installs with Android's default icon and the build says so.

The icon is the game's own artwork, so it is not in this repository — the same rule the Switch
build's icon follows. Supply a square image and run:

```sh
tools/android-icon.py path/to/your-image.png
```

which writes the three files `tools/android-build.sh` looks for: `android/icon.png` for Android 7
and older, and `android/icon-foreground.png` and `android/icon-background.png`, which together
make an *adaptive* icon — two layers the launcher masks into whatever shape it uses, so the game
gets a circle where its neighbours are circles instead of sitting there as a square.

The game ships an icon of its own, if you want the authentic one. In your copy of the game's
folder, `m1a/MiniGolfIcon.raw.lcd5` is the 55×55 icon the iPod itself displayed. It is a raw
image: a 16-byte header of three little-endian 32-bit words — width, height, row stride in bytes
— then the four ASCII bytes `565L`, then the rows, each pixel a little-endian 16-bit RGB565.
`Minigolf.raw.lcd5` beside it is the 320×216 boot splash in the same format.

## Signing

`tools/android-build.sh` signs with a debug key it generates once at `~/Android/debug.keystore`.
That is enough to install and play, and it is not enough to publish: an app signed with a debug
key cannot be updated by one signed with anything else, so a real release wants a real key. That
is the same decision as signing the macOS and Windows builds — see `../../RELEASING.md` — and
this script does not make it for you.

To sign with your own key, set `ANDROID_KEYSTORE` and the build will use it, or re-sign the
finished APK yourself with `apksigner`.

## Why there is no Gradle, and no Java

SDL ships `org.libsdl.app.SDLActivity` already compiled inside its `.aar`, and that activity
loads a library called `SDL3`, then one called `main`, and calls `SDL_main` in the second. So
the port is built as `libmain.so` — `CMakeLists.txt` renames it on Android — and the app needs
no class of its own. What is left is four commands, which is what the build script runs:

1. `d8` turns SDL's Java classes into a `classes.dex`.
2. `aapt2` compiles the icon into a resource table and links it with `AndroidManifest.xml`.
3. `zip` adds the dex and the two `.so` files.
4. `zipalign` and `apksigner` align and sign it.

Adding Gradle would mean a JDK, the Android Gradle Plugin, a wrapper and a daemon, to save those
four commands. The one thing you give up is that `android:icon` has to be added to the manifest
by the script rather than kept in the file, because a manifest that names an icon will not link
without one, and a checkout with no artwork still has to build.

## What is different about the Android build

Everything below is in the shared code, guarded by `__ANDROID__`, and none of it changes the
game:

- **The window is fullscreen**, which is also what puts Android's status and navigation bars
  away.
- **No on-screen keyboard.** Asking for text input raises the IME over the game, so names are
  spelled on the wheel, as on the Switch.
- **The data directory** comes from `SDL_GetAndroidExternalStoragePath()`, since an app may
  write only where the system says and no path in `platform/paths.cpp` could have guessed it.
- **Fatal messages go to the log** (`adb logcat`), because stderr reaches nobody.
- **The battery** comes from `SDL_GetPowerInfo()`. Android refuses an app `/sys/class/power_supply`,
  which is where the Linux build reads it.
- **The music decodes through MediaCodec.** The `.m4a` tracks are AAC, which SDL does not decode,
  so `MediaExtractor` parses the container and `MediaCodec` decodes it — the device's own codec,
  which is the same bargain macOS and Windows make with theirs. It needs `libmediandk`, linked on
  the Android branch of `CMakeLists.txt`, and nothing above `music_decoder.h` knows about it.
  Linux and the Switch still have no decoder and play the sound effects only.
- **The audio trace goes to the log.** `MINIGOLF_TRACE_AUDIO` writes to stderr, which reaches
  nobody here, so on Android it goes through `SDL_Log` and comes out of `adb logcat`.
Not on that list, though it is the thing this port most often gets asked about: the compiler is
told `-ffp-contract=off`. Clang on ARM would otherwise fuse a multiply and an add into one
instruction that rounds once instead of twice, shifting about one pixel in five hundred by a
single value — invisible to play, and enough to stop a frame from this build matching a frame
from another machine's, which is one of the ways this port is checked. It is not an Android
setting: it is on `ipod_core` in `common/CMakeLists.txt`, so every build of every title on every
platform rounds the same way, and nothing here has to ask for it.

## If it doesn't work

**`adb devices` shows nothing.** USB debugging off, or the cable is charge-only, or you haven't
accepted the host's key on the device's screen.

**The app installs and immediately closes.** Look at `adb logcat`. If the last line from the game
is `Finished main function` then the game exited on its own rather than crashing — most likely it
could not find its files, which it should have said on screen first.

**`INSTALL_FAILED_UPDATE_INCOMPATIBLE`.** An older copy is installed that was signed with a
different key. `adb uninstall org.ipodrecomp.minigolf` first.

**It builds but the link fails on SDL.** The SDL3 CMake package was not found and the build
refused to fall back to your machine's own SDL, which describes the machine you are building on
rather than the one you are building for. Check `SDL3_ANDROID` points at the unpacked prefix,
not at the `.aar`.

## Removing it all afterwards

```sh
rm -rf ~/Android/android-ndk-r28c ~/Android/Sdk ~/Android/cmdline ~/Android/sdl3
adb uninstall org.ipodrecomp.minigolf
```
