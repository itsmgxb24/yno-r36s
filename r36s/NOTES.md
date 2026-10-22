# Working notes (state of 2026-10-22)

Hand-over notes for continuing the work from another machine. The README says what the client is; this says
where things stand, how to build and test it, and what was learned on the device.

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
  `YNO_SAVESYNC_UI=conflict|sync` (show that save sync screen with made-up data).
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
