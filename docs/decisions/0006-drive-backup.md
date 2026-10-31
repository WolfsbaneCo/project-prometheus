# 0006: Google Drive backup

- **Status:** Accepted for Phase 3 (implemented in `apps/prometheus64`).
- **Date:** 2026-10-26
- **Brief sections:** 9, 10 (cross-device resume), 14 (Phase 3)
- **Owner's choices:** a future Android version must be able to carry on from Drive; cross-device resume uses in-game saves only, never save states.

## How it works

| Part | Where | What it does |
|---|---|---|
| Sign-in and Drive calls | `src/google.cpp` | Google's installed-app sign-in (browser, return to `127.0.0.1`, PKCE) and the few Drive v3 calls |
| Token storage | `src/secret_store.cpp` | Windows Credential Manager; elsewhere a file only the user can read |
| Sync engine | `src/backup.cpp` | Compares the library with Drive on a worker thread; never writes the library itself |
| Screen and glue | `src/app_backup.cpp` | The Backup screen (B), the status line, applying saves fetched from Drive |

In Drive, the app's own folder mirrors the library:

```
Memories/N64/<GAME>/history/<time>__<id>/...   each in-game save, exactly as the library keeps it
Memories/N64/<GAME>/current.json               the newest save: its id, when, which device, which saves that device had
Memories/N64/<GAME>/states/<device>/...        that device's save states
```

## Choices and why

- **Saves are compared by content.** The library already names every in-game save by a hash of its content. Two devices holding the same save agree on its id, so "is this the same save?" never depends on clocks or file names.
- **One question per game: who moved on?** Each device remembers the last save it and Drive agreed on.
  - If only this device moved on (or it carried on from Drive's newest save), it uploads.
  - If only another device moved on (or that device had this device's save when it saved), this device fetches Drive's newest save. The library takes it as its current save, and the one it replaces stays in history.
  - If both moved on separately, the Backup screen asks which to keep; the other stays in history on both sides. Nothing is overwritten without asking (brief 9.2).
  - "Had this save" comes from the `seen` list in `current.json`, the ids the uploading device held. A device's own uploads therefore never count as Drive having carried on from it.
- **Saved versions never change once uploaded.** They go up under a temporary name and are renamed when complete, so another device never takes half an upload. Drive keeps the last 10 per game, like the library.
- **In-game saves only, across devices** (owner's choice). Save states are backed up into the device's own folder and never fetched by another device, because a state only loads in the exact same emulator core.
- **When.** Brief 9.2 asks for a backup when the app closes and every hour. The app also checks at start, so another computer's progress is there before you play. After a failure (offline, say) it retries every 5 minutes.
- **Both ways when a game starts** (owner's request). Starting a game checks that one game against Drive at once, in the background (one game, no save states: a second or two), so a computer that has been open for hours still plays the latest progress. If Drive's save is newer and it arrives while the game is still booting (within 15 seconds) with nothing saved since it started, the save goes into the library and the game starts again with it: a moment on its first screens, no progress lost. Later than that, a save is never swapped under a running game (the game has already read its own, and would mix the two at its next save); it waits, and quitting the game checks again: if the game didn't save, the newer save comes down; if it did, both moved on and the player chooses, as always.
- **The worker never writes the library.** Saves fetched from Drive are unpacked to a separate folder; the main thread puts them in the library through the same `store_current` the game uses. A history entry is written under a temporary name and renamed, so the worker never reads a half-written one.
- **One folder for every console** (owner's choice): "Memories" holds "N64" now and "GBA" when the app plays GBA games, in Drive as on the computer.
- **The folders are found by identity, not by place.** Each computer remembers the N64 folder's Drive id and keeps using it wherever the player moves it and whatever they rename it. A computer that doesn't know it yet, such as a new one, searches for the hidden properties set when the folders were made, anywhere in the Drive: `prometheus=n64` on N64, `prometheus=library` on Memories. Only a folder in the bin or deleted is replaced. An earlier version's single "N64 Saves" folder (`prometheus=saves`) is moved into Memories and renamed N64, with its history.
- **`drive.file` only** (brief 9.4). The app sees only what it made. The folders made by one computer are found by the others because it is the same app; an Android client created in the same Google Cloud project is the same app too ([setup](../google-drive-setup.md)).
- **External, published app.** It is open to anyone with a Google account, not only one Workspace, and published, so sign-ins don't expire after 7 days. The client ID comes from CI secrets, never from the repository. A build without it simply has backup switched off.
- **Account choice** (brief 9.4). The screen suggests a personal Google account for game saves. The app works with any account.

## Not done

- Syncing settings and control profiles (brief 9.5).
- Sharing saves between devices on a home network, and pairing (brief section 10).
- Optional encryption with a key only the player holds (brief 9.4).
- Choosing the number of versions kept; it is 10, the brief's default.
