# Working notes (state of 2026-10-22)

Hand-over notes for continuing the work from another machine. `.github/README.md` (the page GitHub shows) says what
the client is for players; this says how it works, where things stand, how to build and test it, and what was
learned on the device.

## Rules of this project
- UI text is **English**. The owner writes Polish.
- The repo is **private** (`origin` = github.com/itsmgxb24/yno-r36s, branch `main`; `upstream` = ynoproject/ynoengine).
- Commits are authored by the owner's git identity. **No co-author / "generated with" lines, no session links.**
- Never extract the room key (`PLAYER_PSK` / server `key.bin`) from the official client. Without it the client plays
  solo plus chat; the only way forward is the YNO developers giving a key (a message to them was offered, not sent).
- Only `EASYROMS` (exFAT) of the SD card is writable from the PC without root; `ROOTFS` and most of `BOOT` are not
  worth touching.

## Build, deploy, test
```sh
r36s/build/setup-container.sh ~/yno-container-store      # once; podman rootless, ~3 GB
podman --root ~/yno-container-store/root --runroot ~/yno-container-store/run exec yno-dev bash /work/r36s/build/build.sh
r36s/deploy.sh /run/media/$USER/EASYROMS/ports           # the card must be mounted; keeps yno.conf, games and saves
```
- Result: `~/yno-build-out/yno-r36s`. After a reboot the container's crun state is gone: `podman stop -t 0 yno-dev && podman start yno-dev`.
- Headless tests run the aarch64 binary inside the container (qemu binfmt) with `SDL_VIDEODRIVER=dummy`:
  `YNO_LOG=debug`, `YNO_SCREENSHOT=/path.png:<frame>` (+ `YNO_QUIT_AFTER_SHOT=1`), `YNO_HUB_PICK=<id>`,
  `YNO_HUB_ACTION=<id>:verify|redownload|all`, `YNO_HUB_UI=options|confirm|options-all|confirm-all`,
  `YNO_HUB_KEYS="ddy"` (inject keys in the list: u d a b y), `YNO_TEST_FETCH="<id>|<path>"`,
  `YNO_SAVESYNC_UI=conflict|sync` (show that save sync screen with made-up data), `YNO_CHAT_PINNED=1` (chat stays
  on screen); `YNO_LOG=debug|info` also prints `mem: rss`.
- Do not test network scripts with `unshare -Urn`: it still shows the host's sysfs and talks to the host's
  NetworkManager over D-Bus. Do not use `pkill -f` with a pattern that occurs in your own command line.

## What works on the device (reported by the owner)
- Yume Nikki plays well; hardware renderer, gamepad, RSS 46-114 MB. Download progress bar with speed (KB/s), text game
  menu, *Download all files* / *Verify* / *Re-download*.
- Causes found for broken textures in big games: (1) URLs with spaces/Japanese/brackets were rejected by libcurl
  (fixed: `YnoNet::EncodeUrl`); (2) files already on the card were requested again and replaced while in use (fixed:
  skip existing, `.all-files` marker after a complete download). With everything on the card, no file is reported missing.
- A game started without *Download all* still shows "Image not found" lines for files not downloaded yet: that is
  expected, run *Download all files* once (1.2 GB for 2kki, 34,651 files).
- Link speed through the ESP32-C3 (PPP over USB CDC, next to the router): **about 200 KB/s**. The PC reaches about
  574 KB/s with the same client (small files). 2kki: ~1 h 40 min on the console, ~35 min on a PC.

## Open items, in the order they were discussed
1. **Menu bug (maybe fixed):** Y on `.flow` opened the options of Yume Nikki. Not reproducible with injected keys; the
   page jump on left/right was removed and cursor moves are logged (`hub: cursor ...`, `hub: options for ...`). If it
   happens again, ask for `ports/yno-native/last-run.log`.
2. **Faster internet.** Ranked in the last discussion: bulk-download on the PC straight onto the card (a small PC-side
   downloader would be a good tool to add); a USB Wi-Fi dongle (kernel 4.4.189 has mt7601u, rt2800usb, rtl8192cu,
   rtl8723bu, rtl8821cu, rtl8812au, rtl88x2bu, zd1211rw; **no r8188eu**, so TL-WN725N v2/v3 will not work; check
   `/lib/firmware` for MT7601U / RT2870 files when the card is in); a USB Ethernet dongle (r8152, asix, ax88179,
   smsc95xx are modules); tuning the C3 PPP link (no firmware source is on this machine). ESP32-S3 as a USB NCM
   device is possible but only Full Speed (~1 MB/s ceiling) and needs custom firmware; a classic ESP32 would be a
   downgrade (no native USB; the kernel has cp210x and ftdi but **no ch341**).
3. **USB tethering from a phone does not work.** `r36s/device/usb-tether/usb-tether.sh` exists and is wired into
   `run.sh`, but the phone only charges and the kernel never sees a device. The DTB (`rk3326-r36s-linux.dtb`) has dwc2
   `dr_mode = "otg"` with the usb2phy `otg-port` disabled and the `OTG_SWITCH` regulator always on, so the port is a
   permanently powered host. A microcontroller enumerates by itself, a phone needs the USB mode set. The script
   should work unchanged for a USB Ethernet or Wi-Fi dongle (it looks for any network interface on the USB bus).
4. Server data is not gzip-compressed (Cloudflare, `application/octet-stream`), so compression gains nothing.
5. Features the owner asked for and not started: player list, parties, friends, badges, rankings, locations, login
   with an on-screen keyboard, other players on the map (needs the room key).
6. **Save sync (slot 1, like the website)** is in `src/native/yno_savesync.*`: checked from the game list before a game
   starts (conflict screen when both sides changed), uploaded after each save into slot 1. Needs `AUTH`; `SAVESYNC=0`
   switches it off. `.savesync` in the game directory remembers the last agreed server timestamp and save hash (the
   console clock is not trusted). Verified by the owner on 2026-10-22 with a real account, both ways (website to
   console and back). Other slots stay local: the server holds only one save per account.
   A game started directly (`run.sh <game>`) skips the check and so never uploads.

## Not in git
- The built binary (`yno-r36s`, rebuild it), `lib/liblcf` (cloned by `build.sh`, pinned commit).
- The first, Firefox-based port (`Yume Nikki Online.sh`, `yumenikki-online/`): it exists only on the SD card.
- ESP32 PPP bits (`ports/ESP32 Internet.sh`, `esp32-internet/r36s-ppp.sh`): they live on the card, not in this repo.

## How it differs from the browser version

| Area | Browser version | This client |
| --- | --- | --- |
| Game files | Emscripten FS, `emscripten_async_wget2` | normal directory, libcurl worker threads (`src/native/yno_net.*`, `src/async_handler.cpp`) |
| Room socket (`/room`) | Emscripten WebSocket | libcurl WebSocket (`src/native/yno_ws.*`, `src/multiplayer/yno_connection.cpp`) |
| JavaScript bridge | `src/web_api.cpp` (removed here) | `src/native/web_api_native.cpp` |
| Chat, player counter | `chat.js`, `session.js` (website) | `src/native/yno_session.*` (session socket) + `src/native/yno_overlay.*` (chat drawn in the engine) |
| Choosing a game | the hub page | `src/native/scene_yno_games.*`: a plain text menu of the 25 games with the players online, and per game *Download all files* / *Verify files* / *Re-download files* (`src/native/yno_games.*`) |
| Download progress | none (the browser shows its own) | a bar below the middle of the screen (`src/native/yno_overlay.*`) |
| Save sync (slot 1) | `savedata.js` (website), `/api/savesync` | `src/native/yno_savesync.*`, checked from the game list before a game starts |
| Controls | keyboard | gamepad mapping in `src/input_buttons_desktop.cpp` (Select never resets the game) |

File names are percent-encoded before they are requested (`YnoNet::EncodeUrl`): the browser does that on its own, libcurl rejects a URL with a space in it, and about 17% of the files of Yume 2kki have a space, brackets or Japanese characters in their name.

A file that is already on the card is not requested again (`async_handler.cpp`): the browser starts with an empty file system, the handheld does not, and replacing files while the game runs made them disappear for a moment.

This fork builds only the native client (`-DPLAYER_YNO_NATIVE=ON`, defines `YNO_NATIVE`); the browser
(Emscripten) build of upstream was removed. `EP_WEBLIKE_FS` (`src/native/yno_platform.h`) marks the places
where the engine assumes lazily downloaded files, as it did on the web.

## The room key

The server verifies every `/room` packet with a secret (`key.bin` on the server, `PLAYER_PSK` in the
official client) that is not part of any public source. Without it this client plays on its own and
still shows the chat and the player counter. If the YNO developers give you the key, put it in
`yno-native/room.key` on the card (`--yno-room-key-file`). Do not extract it from the official client.

## Internet over USB tethering

The stock kernel (4.4.189) already has `rndis_host`, `cdc_ncm`, `cdc_ether` and the usual USB Ethernet
drivers as modules, so an **Android** phone with *USB tethering* ON works without a kernel rebuild (an iPhone
would need `ipheth`, which is not built). NetworkManager normally configures the new interface; the script
falls back to `nmcli device connect` and `dhcpcd`. Both scripts leave a log in `usb-tether/last-run.log`.
