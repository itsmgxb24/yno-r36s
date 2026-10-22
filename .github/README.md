# Yume Nikki Online on the R36S

A native client for [YNOproject](https://ynoproject.net) (Yume Nikki and the other 24 games included in the
project) on the **R36S** handheld running dArkOS.

>This is a fork of [ynoproject/ynoengine](https://github.com/ynoproject/ynoengine) (EasyRPG Player with YNO
>multiplayer). Unofficial, not made or supported by the YNOproject team.

## What it does

- **All YNO games** in a simple list, with the number of players online in each.
- **Downloads on demand**: a game fetches the files it needs while you play, or all at once with
  *Download all files*. *Verify files* and *Re-download files* repairs a broken copy. Saves are never touched.
- **Chat**: the latest messages appear at the bottom of the screen (read only for now).
- **Save sync**: with a YNO account set in `yno.conf`, save slot 1 follows your account, just like on the
  website. Save on the console, continue on the PC or phone, and back. If both sides changed, the console asks
  which save to keep.
- **Gamepad controls** mapped for the R36S. Select + Start quits. Controls settings will be added.
- **Internet**: the launcher checks for availble WiFi adapters before launching. Offline mode will be added.

## Download

You can download the client from our [releases](https://github.com/itsmgxb24/yno-r36s/releases) page.

## Building

1. Build it (needs rootless podman and about 3 GB):
   ```sh
   r36s/build/setup-container.sh ~/yno-container-store
   podman --root ~/yno-container-store/root --runroot ~/yno-container-store/run \
     exec yno-dev bash /work/r36s/build/build.sh
   ```
2. Put the SD card in the PC and copy the client to it (your settings, games and saves are kept):
   ```sh
   r36s/deploy.sh /run/media/$USER/EASYROMS/ports
   ```
3. On the R36S run **Yume Nikki Online (native) from the Ports menu**.

### Save sync setup

Log in on ynoproject.net in a PC browser and open the developer tools (F12). Select Storage, then Cookies, and click on `https://ynoproject.net`.
copy the value of the `auth` cookie and put it into `ports/yno-native/yno.conf` on your SD card.

Only slot 1 is synchronised, this is a limitation of the YNO server and wont be changed.

Keep your `auth` token private, as it gives access to your account.

The `SAVESYNC=0` turns save file sync off.

## More info

- [`r36s/NOTES.md`](../r36s/NOTES.md): current state, testing, open items.
- [`r36s/device/yno-native/README.txt`](../r36s/device/yno-native/README.txt): controls and settings.

## Licence

GPL-3.0-or-later, like EasyRPG Player (see [`COPYING`](../COPYING)).
Yume Nikki and other games belong to their authors. Our client downloads them from the YNOproject servers and does not include them.
