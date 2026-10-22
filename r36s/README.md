# YNOproject native client for the R36S

A native (no browser) client for [Yume Nikki Online](https://ynoproject.net) and the other games of the
YNOproject, for the R36S handheld running dArkOS (RK3326, 640x480, 1 GB RAM, kernel 4.4, no keyboard).

It is a fork of [ynoproject/ynoengine](https://github.com/ynoproject/ynoengine), the EasyRPG Player
fork the website runs as WebAssembly. The engine was already portable: only the parts that talk to
the browser had to be replaced.

## What changed compared to upstream

| Area | Browser version | This client |
| --- | --- | --- |
| Game files | Emscripten FS, `emscripten_async_wget2` | normal directory, libcurl worker threads (`src/native/yno_net.*`, `src/async_handler.cpp`) |
| Room socket (`/room`) | Emscripten WebSocket | libcurl WebSocket (`src/native/yno_ws.*`, `src/multiplayer/yno_connection.cpp`) |
| JavaScript bridge | `src/web_api.cpp` | `src/native/web_api_native.cpp` |
| Chat, player counter | `chat.js`, `session.js` (website) | `src/native/yno_session.*` (session socket) + `src/native/yno_overlay.*` (chat drawn in the engine) |
| Choosing a game | the hub page | `src/native/scene_yno_games.*`: a plain text menu of the 25 games with the players online, and per game *Download all files* / *Verify files* / *Re-download files* (`src/native/yno_games.*`) |
| Download progress | none (the browser shows its own) | a bar below the middle of the screen (`src/native/yno_overlay.*`) |
| Controls | keyboard | gamepad mapping in `src/input_buttons_desktop.cpp` (Select never resets the game) |

File names are percent-encoded before they are requested (`YnoNet::EncodeUrl`): the browser does that on its own, libcurl rejects a URL with a space in it, and about 17% of the files of Yume 2kki have a space, brackets or Japanese characters in their name.

A file that is already on the card is not requested again (`async_handler.cpp`): the browser starts with an empty file system, the handheld does not, and replacing files while the game runs made them disappear for a moment.

Everything is behind `-DPLAYER_YNO_NATIVE=ON` / `YNO_NATIVE`; the Emscripten build is untouched.
`EP_WEBLIKE_FS` (`src/native/yno_platform.h`) marks the places where the engine assumes lazily
downloaded files, as on the web.

## Status

Verified on a PC (the aarch64 binary under qemu, with a dummy video driver) against the real servers:
game data download, the Yume Nikki and .flow title screens, the game list with online counts, the
session socket and live chat messages.

**Not verified on the device yet:** KMSDRM video, audio, the gamepad, speed, memory use.

Not done: other players on the map (needs the room key, see below), player list / parties / friends,
badges / rankings / locations, login with an on-screen keyboard and save synchronisation.

### The room key

The server verifies every `/room` packet with a secret (`key.bin` on the server, `PLAYER_PSK` in the
official client) that is not part of any public source. Without it this client plays on its own and
still shows the chat and the player counter. If the YNO developers give you the key, put it in
`yno-native/room.key` on the card (`--yno-room-key-file`). Do not extract it from the official client.

## Building

Needs rootless podman and about 3 GB (the container is a Debian trixie amd64 cross toolchain with the
arm64 libraries, the same userland as the R36S).

```sh
r36s/build/setup-container.sh [podman storage dir]
podman exec yno-dev /work/r36s/build/build.sh     # -> ~/yno-build-out/yno-r36s (7 MB)
```

All runtime libraries it needs are already present on dArkOS.

## Installing on the card

```sh
r36s/deploy.sh /run/media/$USER/EASYROMS/ports
```

Then start **Ports -> Yume Nikki Online (native)**. See `r36s/device/yno-native/README.txt` (Polish) for
controls and settings. If there is no internet the launcher first looks for a phone with USB tethering / a USB Ethernet adapter
(`usb-tether/usb-tether.sh`, also a separate **Ports -> USB Tethering** entry), then for the ESP32 PPP link. It
sets the clock from the server and quits on Select+Start (gptokeyb).

### Internet over USB tethering

The stock kernel (4.4.189) already has `rndis_host`, `cdc_ncm`, `cdc_ether` and the usual USB Ethernet
drivers as modules, so an **Android** phone with *USB tethering* ON works without a kernel rebuild (an iPhone
would need `ipheth`, which is not built). NetworkManager normally configures the new interface; the script
falls back to `nmcli device connect` and `dhcpcd`. Both scripts leave a log in `usb-tether/last-run.log`.

## Development aids

Environment variables of the client: `YNO_LOG=debug|info` (log level, also prints `mem: rss`),
`YNO_SCREENSHOT=/path.png:frame` + `YNO_QUIT_AFTER_SHOT=1` (headless screenshot), `YNO_HUB_PICK=<game id>`
(pick a game on the list without input), `YNO_CHAT_PINNED=1` (chat stays on screen).

## Licence

GPL-3.0-or-later, like the engine it is based on (see `COPYING`).
