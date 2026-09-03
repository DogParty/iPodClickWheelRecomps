# iPod Click Wheel Recomps

## Some Backstory

In 2006, Apple announced that the classic iPods were able to run third party games. Between 2006 to 2009, 54 total iPod games were released for purchase on the iTunes Store. By 2011, Apple stopped selling the games. For years, many of the games were considered lost media but the [iPod Clickwheel Games Preservation Project](https://github.com/Olsro/ipodclickwheelgamespreservationproject/tree/main) finally amassed all of the games and came up with an ingenious way to run them all on your own iPod. 

On one hand, it's amazing that these games were discovered, preserved, and playable on original hardware. On the other hand, that the games are still tied to hardware that's at least 17 years old. iPods are dying every day so it's not the perfect situation.

## The Recomp Project

This project aims to bring these games over from the aged hardware to modern machines. At the moment, six games have been decompiled and recompiled to run on current hardware. Each folder here is
one of those games, translated from the ARM code the iPod ran into C++ that a modern machine can compile. They all share a common library to ease recompilation and porting to new platforms. 

For the first time in the last 20 years, these games are now playable on more than just an iPod! The six games support macOS and Windows while Mini Golf also supports being run on the Switch. Adding support for new platforms is pretty easy; just look at how each recomp's platform works. 

One difficult part about running these games on modern hardware is input. Each of these games expect a click wheel. Some expect it to be spun in certain directions, tapped at certain points, and/or clicked to access the buttons corresponding to its cardinal directions. The recomps aim to map it to what your machine does have: arrow keys, a gamepad stick, or the D-pad. I wouldn't describe it as perfect at the moment but it suffices. You can also rebind keys on macOS and Windows.

Every game needs its own *decrypted* game files, taken from your own copy. Nothing here contains any of those files nor will I help you create or find them.

Each game's folder has its own README with details on how to build it, how it was decompiled, what is still recompiled rather than rewritten, and how to run it.

## The games

| Folder | Platforms | Game folder |
|---|---|---|
| `Mini Golf` | macOS, Windows, Switch | `88888` |
| `Cubis2` | macOS, Windows | `99999` |
| `HoldEm` | macOS, Windows | `33333` |
| `Lost` | macOS, Windows | `1B200` |
| `Sims Bowling` | macOS, Windows | `1500C` |
| `Vortex` | macOS, Windows | `12345` |

When opening the game for the first time on macOS and Windows, you will be asked for the game's folder. The game then checks every file against the sizes and checksums it shipped with, and keeps it beside your saves from then on.

`common/` is the shared half: the software renderer, the runtime, the platform layer, the recompiler and the build tooling. 

Mini Golf is the furthest along, being the only one with no machine-translated code left in it and the only one that runs on a Switch.

## Recomp Improvements

Of course we have to actually improve these 20 year old games! Some improvements include:

1. Render scale
   The games were originally meant for a 320x240 display so the assets are low quality. To help, I've added the ability to scale the resolution up to 8x the native resolution. This doesn't magically make everything crisp but it softens edges and can make assets look cleaner on modern displays.
2. Render text at the native display resolution
   Before, the text would render at 320x240 which caused it to look blocky and blurry. Now, they render at your display's resolution (or the viewport) rather than the iPod's resolution which makes text look a lot better. 
3. Cheats
   Some games support cheats now! You can unlock all courses in Mini Golf and all chapters in Lost, for example.
4. Change FPS
5. Rebind keys

## Screenshots

| Mini Golf | Cubis 2 |
|---|---|
| ![Mini Golf](docs/screenshots/mini-golf.png) | ![Cubis 2](docs/screenshots/cubis-2.png) |

| Lost | Vortex |
|---|---|
| ![Lost](docs/screenshots/lost.png) | ![Vortex](docs/screenshots/vortex.png) |

| The Sims Bowling | Texas Hold'em |
|---|---|
| ![The Sims Bowling](docs/screenshots/sims-bowling.png) | ![Texas Hold'em](docs/screenshots/texas-holdem.png) |

## AI disclosure

Look, I've been a software developer for over 15 years now. I have a full time job, a family, and a full life. I wish I could tell you that I spent years reverse engineering games from a never-before-reversed platform like the iPod but that would be a lie. I heavily relied on Claude for this project. It has still taken me weeks to get to this point and a lot of real direction from a real SWE (for example, "I'm noticing magenta on the edges of the assets in this menu, I imagine that should be an alpha value we're not parsing correctly. Can you decompile the original code's graphics call on the menu and veridy the color channel it's using").

If that's not enough for you then that's okay. It's enough for me and hopefully someone else out there. At the end of the day, this project represents a world first: iPod Classic games running on modern operating systems. I would much rather have the project exist than not. 

For The Sims fans, this unlocks some small Sims games that have been stuck on the iPod. (Only Sims Bowling has been recompiled, at the moment). For fans of the TV show LOST, there's an original game here that maps out to the first season. For Apple fans, their first version of Texas Hold'em is here and the players look _extremely_ 2006.

## Legal

These games belong to their publishers. Nothing in this repository is their code, art, music or levels, and no release contains any of it.

What is here is a port: host code, a decompilation of the game logic, and for five of the six titles a machine translation of the game's own binary that is generated on your machine, from your copy, and is never committed.

This is a hobby project, offered free, with no affiliation with or endorsement by anyone who owns the originals.

If any of the original rights holders are upset about this, please contact me. I just want the world to be able to play your 20 year old games.

## Licence

GPL-3.0-or-later for the games and the shared runtime, MIT for the tools in `common/tools/`. See `LICENSING.md` for the split and what it does and does not cover, and `RELEASING.md` for how the
downloads are built.
