// Cheats: deliberate changes to what the game itself does, asked for by the player.
//
// Everything else in this program tries to be the iPod. This file is the one place that does
// not, and it is kept apart for that reason — nothing here is reached unless a player has turned
// it on in Settings ▸ Cheats, and every one of them is off by default and off in every test.
//
// A cheat here works on the *game's own state*, not on its save file. That is deliberate twice
// over: a save is checksummed and this program cannot write one the game would accept, and — far
// more importantly — a cheat that rewrote a save would still be in the player's save after they
// turned it off. Reaching into memory leaves nothing behind: turn the switch off, and the next
// frame the game is exactly the game again.
//
// The evidence for every address named in cheats.cpp is in that file, next to the address.
#pragma once

namespace lost::game {

// Apply whatever `platform::settings()` currently asks for. Called once per frame from the frame
// pump, *after* the game's frame vector has returned — a cheat that undoes something the game
// does during a frame has to run after the frame that does it, not before.
//
// Cheap and idempotent: with every cheat off it reads nothing and writes nothing.
void apply_cheats();

// Whether any cheat is on, for the one line the program prints when it starts a run with one.
[[nodiscard]] bool any_cheat_enabled();

}  // namespace lost::game
