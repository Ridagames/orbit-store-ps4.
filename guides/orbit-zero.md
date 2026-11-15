# Use Orbit Zero with your PS4

Orbit Zero is Orbit’s app for Mac, Windows and Linux. With your PS4 it can:

- download games on your computer and send them to your PS4 over your home network, then install them;
- open Vikingfile and AkiraBox pages on your computer, where their check passes in seconds;
- start Orbit on your PS4.

**Needs Orbit Store 0.0.2 or later on your PS4 and Orbit Zero 1.9.1 or later on your computer.** The computer and the PS4 must be on the same home network, over Wi-Fi or Ethernet.

## Install Orbit Zero

Download the `Orbit-Zero-*` package for your computer from the [latest release](https://github.com/saawant12/orbit-store-ps4/releases/latest): macOS DMG, Windows installer or ZIP, or Ubuntu/Debian DEB. ARM64 is for Apple Silicon Macs and ARM computers; x64 (amd64 for DEB) is for Intel and AMD computers. The [Orbit Zero installation guide](https://github.com/saawant12/orbit-store-ps5/blob/main/guides/orbit-zero-installation.md) covers first-launch permissions on each system.

## Start Orbit from Orbit Zero

If Orbit isn’t running on your PS4 yet, start GoldHEN, then in Orbit Zero:

1. Open **Settings → Updates & startup**.
2. Enter your PS4’s IP address and choose **PS4**.
3. Select **Start Orbit on PS4**.

Zero gets Orbit for PS4 from this repository’s latest release, checks it, and sends it to the ELF loader on port 9021. If no ELF loader is running, Zero first starts elfldr through GoldHEN’s payload loader on port 9090. Orbit then starts and Zero connects to it.

If Orbit doesn’t start, close other homebrew apps such as ezRemote, then try again. You can also open Orbit from the Games row as usual.

## Pair Orbit Zero with your PS4

1. Open Orbit on your PS4.
2. In Orbit Zero, open **Settings → Devices**, enter your PS4’s IP address and select **Connect to Orbit**.
3. Enter the six-digit code shown on your PS4.

Zero remembers the pairing. Next time it reconnects by itself.

To control computer downloads from the TV app, open **App settings → Devices → Orbit Zero** on your PS4 and turn it on.

## Download games

With a PS4 paired, Orbit Zero’s **Browse** and **Add-ons** show only PS4 games. Choose a game and a drive on your PS4:

- **Computer** downloads the game on your computer first, then sends it to your PS4 and installs it. Your PS4 doesn’t need internet access for this. Keep Orbit Zero open until the transfer finishes. **Stream & install**, which installs straight from the computer copy, isn’t available for PS4 yet.
- For **Vikingfile** and **AkiraBox**, Zero opens the page on your computer, where its check usually passes by itself in seconds. Complete any check that stays, then select the download if the page asks. Zero then downloads the game on your computer and sends it to your PS4.

To let your PS4 download games itself when it can reach the source, open **Settings → Downloads** in Orbit Zero and set **Download preference** to **Prefer PS4**. For Vikingfile and AkiraBox, Zero then hands the download link it caught to your PS4, and the download appears in Orbit’s Downloads on your PS4. If your PS4 can’t use the link, Zero downloads the game on your computer instead.

## PS5 and PS4 together

Orbit Zero uses one console at a time. Each console you pair gets its own profile, with its own pairing, games list and downloads. Your download folder, TorBox, Archive.org sign-in and language are shared.

- **Switch consoles:** **Settings → Devices → Consoles**, or the console buttons in the Connection panel on **Console**. Switching needs no new code.
- **Add a console:** select **Add another console**, then connect and pair it.
- Finish or cancel the transfers to one console before switching to the other: they need that console to finish.
- **Unpair** forgets only that console’s profile.

## Troubleshooting

- **Zero can’t find Orbit:** check that Orbit is open on your PS4 and that the IP address matches the one in Orbit’s settings. Both devices must be on the same network.
- **Neither an ELF loader nor GoldHEN’s payload loader answered:** start GoldHEN and turn on its payload loader in **GoldHEN → Servers Settings → Enable PayLoader Server**.
- **The pairing code isn’t accepted:** use the code currently shown on your PS4. Show a new one from Orbit Zero if it has changed.
