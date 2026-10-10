# Beginner guide: install the marine instrument display

For most users, the easiest installation is now **download one prebuilt binary and flash it in a browser with ESPConnect**. You do not need VS Code, PlatformIO, Git, Python, or a compiler.

The VS Code/PlatformIO instructions remain below for developers, custom builds, and troubleshooting.

## Recommended method: ESPConnect + prebuilt release

### What you need

- A **Waveshare ESP32-S3-Touch-LCD-4**.
- A USB data cable.
- Chrome, Edge, Brave, or another Chromium browser with Web Serial support.
- The prebuilt **`esp32-n2k-touch-full.bin`** from this project's GitHub Release.

### Download the firmware

1. Open the [latest release](https://github.com/cram001/esp32-n2k-touch/releases/latest).
2. Expand **Assets** if GitHub has collapsed the list.
3. Download **`esp32-n2k-touch-full.bin`**.
4. Optionally download **`SHA256SUMS.txt`** if you want to verify the file checksum.

Do not download the repository ZIP and do not choose `firmware.bin` for a first installation. The `firmware.bin` file is only the application image. The **full** image also contains the bootloader, partition table and OTA metadata required for a predictable clean install.

### Flash with ESPConnect

1. Connect the display to the computer with a USB data cable.
2. Open [ESPConnect](https://thelastoutpostworkshop.github.io/ESPConnect/).
3. Click **Connect**.
4. When the browser opens the serial-device chooser, select the ESP32-S3 board and click **Connect**.
5. Open **Flash Firmware** in ESPConnect.
6. Choose the downloaded **`esp32-n2k-touch-full.bin`**.
7. Set the flash address/offset to **`0x0`**.
8. Start the flash.
9. Do not unplug the board while it is erasing/writing/verifying.
10. When the operation is complete, disconnect in ESPConnect and restart the board with the physical **BOOT** button released.

The full image is intended for **first installation or clean recovery**. Because it contains the low-flash boot/partition/OTA regions and spans the application-settings area, installing it can clear existing saved settings. Use it when that is acceptable.

If ESPConnect cannot connect, enter the ESP32-S3 download/bootloader mode: hold **BOOT**, press/release **RESET** if available (or reconnect USB while holding BOOT), then release BOOT and try **Connect** again.

After flashing, continue at [Check the first boot and set up the display](#8-check-the-first-boot-and-set-up-the-display).

---

## Build from source instead

The remainder of this guide explains the developer/source-build method. It takes you from a new computer setup to a local build using VS Code and PlatformIO. The main instructions are for Windows 10/11; Mac and Linux notes are at the end.

## What you are installing

- **GitHub** is the website holding this project's files.
- A **repository** (or **repo**) is the project and its change history.
- **Git** is a small program that copies and updates the repository on your computer.
- **Clone** means download a local copy using Git.
- **VS Code** is the desktop application where you open and build the project.
- **PlatformIO** is a VS Code extension that downloads the build tools and uploads to the board.
- **Build** turns the source code into firmware. **Upload/flash** writes that firmware to the ESP32.
- **Firmware** is the program the display runs, even when disconnected from your computer.

You will install Git and VS Code, install one extension, clone the project,
build it, then upload it over USB. Keep this guide open in your browser while working.

## 1. Gather the right hardware

You need:

- A **Waveshare ESP32-S3-Touch-LCD-4**: 4-inch, 480 x 480 touchscreen,
  16 MB flash and 8 MB PSRAM. Check the exact model printed on your board.
- A USB **data** cable that fits the board, plus a working computer USB port.
  A charging-only cable can light the screen but cannot upload code.
- Internet access for the first build and several GB of free disk space for tools.

This project is configured for that exact board. A plain ESP32, a different
screen size, or a model with a suffix such as 4B is not automatically compatible.
Do not change the board setting to guess your way through installation.
Use the [Waveshare board documentation](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4)
to identify its USB connector and BOOT/RESET controls.

For the initial test, use USB power on your desk without connecting the boat's
NMEA 2000 network. The display can boot without sensors; empty values show `--`.
Uploading replaces the factory demonstration or any application already on the
board. Keep a copy of firmware you may want to reinstall.

## 2. Install VS Code

1. Open [the official VS Code download page](https://code.visualstudio.com/Download).
2. Download the Windows installer for your computer (usually x64).
3. Open the downloaded installer. Accept the license and use the default options.
   If offered, enable **Add to PATH** and **Open with Code** for folders.
4. Finish installation and open **Visual Studio Code**.

Choose the desktop VS Code application, rather than the browser editor.
The free edition is sufficient; no paid subscription or AI assistant is required.
See [Microsoft's Windows setup instructions](https://code.visualstudio.com/docs/setup/windows)
if the installer differs on your computer.

## 3. Install Git

1. Download Git from [the official Git website](https://git-scm.com/downloads).
   Choose Windows and run the installer.
2. Leave the defaults. If asked how Git should be used, choose the option making
   it available **from the command line and third-party software**.
3. Close all VS Code windows and reopen VS Code so it sees the new installation.
4. Select **Terminal > New Terminal** from the menu. A text panel opens below the editor.
5. Type the following and press Enter:

   ```text
   git --version
   ```

You should see something like `git version ...`. If it says Git is not recognized,
restart VS Code or your computer, then try again.

Git is also needed to download this project's NMEA2000 library. You do not need
to create a GitHub account, fork the repo, set up SSH keys, or publish anything
to install this public project.

## 4. Install the PlatformIO extension

1. In VS Code, click **Extensions** on the left (the icon looks like blocks),
   or press **Ctrl+Shift+X**.
2. Search for **PlatformIO IDE**.
3. Select the official extension published by **PlatformIO**
   (extension identifier `platformio.platformio-ide`) and click **Install**.
4. Wait for installation and its first setup to finish. If VS Code asks to reload
   or restart, do so. A PlatformIO icon should appear on the left.

The project recommends this extension when opened, but a recommendation is not
an automatic installation. Install it yourself if it is missing. PlatformIO
includes its command-line tool and uses built-in Python where available; let
its installer finish before adding separate tools. The default setup downloads
the compiler, ESP-IDF framework and project libraries when needed. You do not
need Arduino IDE or a separate ESP-IDF installation.

If setup explicitly reports missing Python, follow its prompt or
[PlatformIO's Python instructions](https://docs.platformio.org/en/stable/faq/install-python.html),
then restart VS Code. The official
[PlatformIO VS Code guide](https://docs.platformio.org/en/stable/integration/ide/vscode.html)
explains the extension and its build/upload controls.

## 5. Clone this repository

1. In Windows File Explorer, create a local folder such as `C:\Projects`.
   Choose a folder you can write to **outside OneDrive, Dropbox or other cloud sync**.
   If Windows will not let you create that folder, choose another local folder
   you own outside cloud sync. Short folder paths help avoid Windows path-length issues.
2. In VS Code, press **Ctrl+Shift+P**. This opens the **Command Palette**,
   a search box for actions.
3. Type **Git: Clone** and select that action.
4. Paste this complete address and press Enter:

   ```text
   https://github.com/cram001/esp32-n2k-touch.git
   ```

5. Select `C:\Projects` (or your chosen parent folder) as the destination.
   Git creates a new `esp32-n2k-touch` folder inside it.
6. When VS Code asks whether to open the cloned repository, click **Open**.
7. If you see a workspace trust prompt, confirm trust after checking that you
   cloned the intended repository. Trust permits its build tools to run.
8. In the Explorer panel, check that you can see `README.md`, `platformio.ini`,
   `src` and `docs`. The open folder must be `esp32-n2k-touch`, not its parent
   or just the `src` folder.
9. The branch name at the lower left should be **main**. Use main for this walkthrough.
   A pull request (PR) proposes changes; an open PR is not yet part of main.

If you closed the project, use **File > Open Folder** to reopen that same folder.
Do not use PlatformIO's **New Project** wizard: this repository is already configured.
The [VS Code GitHub instructions](https://code.visualstudio.com/docs/sourcecontrol/github)
also describe cloning by URL.

## 6. Build the firmware

1. Keep your Internet connection active. Wait for PlatformIO to initialize the project.
2. Click the PlatformIO icon on the left.
3. Expand **Project Tasks > waveshare-touch-4 > General** and click **Build**.
   Alternatively, hover over the bottom toolbar icons and click **Build** (check mark).
4. Watch the output panel. The first build downloads tools and libraries and can
   take many minutes. Later builds are usually much faster.
5. Wait for **SUCCESS**. A screen full of compiler messages is normal;
   **FAILED** or an error means the build has not completed successfully.

The configured environment is `waveshare-touch-4`. Although `platformio.ini`
lists `esp32-s3-devkitc-1`, that is an intentional generic target with Waveshare
flash, PSRAM and display settings supplied by this project. Leave it unchanged.
Also keep the checked-in dependency versions and partition settings.

After a successful local build, the application image is:

```text
.pio/build/waveshare-touch-4/firmware.bin
```

That local `firmware.bin` is **application-only**. PlatformIO's normal USB Upload task writes the required bootloader, partition table and application pieces at their correct addresses.

For non-developers, GitHub Releases provide a separately generated **`esp32-n2k-touch-full.bin`** merged image specifically for ESPConnect first installation/recovery at address `0x0`. Do not confuse the two files.

## 7. Connect the board and upload

1. Connect the board's programming USB connector directly to your computer using
   the data cable. Leave other ESP boards disconnected to avoid choosing the wrong one.
2. On Windows, right-click Start and open **Device Manager**. Look under
   **Ports (COM & LPT)** and note the port that appears when you connect the board,
   for example `COM5`. The actual number on your computer may be different.
3. Close any serial monitor or other program using that port.
4. In VS Code, click **Project Tasks > waveshare-touch-4 > General > Upload**,
   or the bottom **Upload** arrow. This also builds if necessary.
5. Wait for the writing/verification messages and **SUCCESS**. Keep power and
   USB connected until it finishes.
6. Allow the board to restart. If it stays in download mode, press RESET once,
   or unplug and reconnect USB with BOOT released.

If no COM port appears, try a different known data cable and USB port first.
If Device Manager reports an unknown device, identify it and use the driver
specified by Waveshare for your board revision. Different USB interfaces use
different drivers; do not install random drivers just because another ESP32 uses them.

### If uploading waits at “Connecting...”

Enter the board's download mode manually:

1. Stop the failed upload and disconnect board power.
2. Hold **BOOT**, reconnect USB power, then release BOOT once the board is connected.
   If your revision has RESET, holding BOOT while pressing and releasing RESET
   is another way to enter download mode. Check the board documentation for its controls.
3. Check the COM port again; it can change in download mode.
4. Try **Upload** again. After success, restart with BOOT released.

BOOT is a physical board control, not a touch gesture. Do not erase flash as a
routine fix: erasing can remove saved settings, passwords and calibration.

## 8. Check the first boot and set up the display

1. Confirm you see the boot screen followed by an instrument page.
2. Note or photograph the **firmware version and build date/time** on the boot
   screen. These identify what you actually uploaded; the displayed build time
   comes from the computer that compiled it.
3. **Swipe up** on an instrument page to open Settings. There is no bottom Setup button.
4. Set your units and comfortable Day/Night brightness. Start with visible levels.
5. **Swipe down** on an instrument page to edit its layout and displayed values.
   Swipe left/right to change enabled pages.
6. If using Wi-Fi, choose Station, enable it, tap **Scan Networks**, select your
   2.4 GHz network and enter its password. Scanning happens only when requested.
   Keep manual SSID entry for a hidden network. Save and check for an IP address.
7. Choose the data input and configure any sensors using these guides:

   - [W2K-1 wireless input and gestures](w2k-input-gestures.md): gateway IP/port and TCP N2K ASCII setup.
   - [Source selection, instruments and depth calibration](sources-instruments-depth.md).
   - [SmartShunt setup](smartshunt.md): Instant Readout key and BLE settings.
   - [Configurable instrument pages](data-pages.md).

With no connected or configured source, `--` is expected. Values also become
`--` after 30 seconds without fresh data; received depth over 1000 m is rejected.
For a permanent wired NMEA 2000 installation, follow the project's
[hardware and marine installation notes](../README.md#hardware-baseline)
and [installation checks](development.md#before-permanent-vessel-installation).

If your installed firmware displays **TOUCH SCREEN FOR 3 SECONDS TO RESTORE
BRIGHTNESS**, hold the startup screen continuously for three seconds to restore
Day 80% / Night 20%. This requires firmware containing brightness recovery;
older builds do not gain it just because the guide mentions it.

## 9. Read logs when something goes wrong

1. In PlatformIO's Project Tasks, select **Monitor**, or use its serial-monitor
   toolbar button. The project sets the speed to **115200**.
2. If asked, select the board's current port. Press RESET to capture startup messages.
3. Copy the startup output and errors when asking for help. If there is no output,
   check the port, cable and the board's documented serial interface.
4. Stop the monitor before another upload (Ctrl+C in its terminal).

For commands below, use PlatformIO's own terminal: click its terminal toolbar
icon, or open the Command Palette and select **PlatformIO: Open PlatformIO Core CLI**.
Check that the terminal is in the folder containing `platformio.ini`.

```text
pio device list
pio device monitor --port COM5 --baud 115200
```

Replace `COM5` with your real port. On Mac/Linux it will be a device path instead.
If upload autodetection chooses the wrong port, use:

```text
pio run -e waveshare-touch-4 --target upload --upload-port COM5
```

The equivalent build command is `pio run -e waveshare-touch-4`. If `pio` is not
recognized in an ordinary terminal, open the PlatformIO terminal described above.

## 10. Install later updates

### Recommended: update with ESPConnect and the latest release

You do not need to install Git, VS Code or PlatformIO for later releases either.

1. Open the [latest GitHub Release](https://github.com/cram001/esp32-n2k-touch/releases/latest).
2. Expand **Assets** and download **`esp32-n2k-touch-full.bin`**.
3. Connect the display to the computer using a USB data cable.
4. Open [ESPConnect](https://thelastoutpostworkshop.github.io/ESPConnect/).
5. Click **Connect** and choose the ESP32-S3 serial device.
6. Open **Flash Firmware**.
7. Select the new **`esp32-n2k-touch-full.bin`**.
8. Set the flash address/offset to **`0x0`**.
9. Start the flash and leave the board connected until ESPConnect reports completion.
10. Restart with BOOT released and check the firmware version/build information on the startup screen.

This is the simplest and most predictable USB update method because it installs the complete bootloader/partition/OTA/application image rather than depending on which OTA slot was previously active.

**Saved settings:** flashing the full image can clear NVS/application settings. Before updating, note any important Wi-Fi credentials, SmartShunt encryption keys, source selections, page layouts, calibration values and other configuration you may need to re-enter.

Do **not** use the release `firmware.bin` at address `0x0`. It contains only the application image. The `esp32-n2k-touch-full.bin` file is the one intended for this simple ESPConnect procedure.

If ESPConnect cannot connect, use the same BOOT/RESET download-mode procedure described in the first-install section above.

### Developers: get newer code from main

1. Open your existing project in VS Code. Close the serial monitor.
2. If you have not edited project files, open the PlatformIO terminal and run:

   ```text
   git switch main
   git pull --ff-only
   ```

3. Build again and wait for SUCCESS. Upload by USB as before, or use OTA below.

If Git reports local changes or refuses the update, stop and save your work;
do not use reset/discard commands to force it. A separate fresh clone in another
folder is a simple way to build new code while retaining your original project.
An unmerged feature branch is a separate version; only use one when specifically instructed.

### Update an already running device over Wi-Fi (OTA)

OTA means **over-the-air update**. The first installation still uses USB.
For subsequent updates with the project's existing dual-slot layout:

1. Build the new firmware and retain the previous working download.
2. On the display, swipe up and open **Wi-Fi**. Choose/enable **Access Point**,
   set its name and password (8–63 characters), and save.
3. Connect your laptop's Wi-Fi to that display network. “No Internet” is expected;
   stay connected to it for the upload.
4. Open a browser and enter `http://` followed by the IP address shown on the
   display. Use that displayed address rather than guessing one.
5. Choose the new `.pio/build/waveshare-touch-4/firmware.bin` in the upload page,
   upload it and wait for validation. Select **Reboot** when offered.
6. Check the version on the next boot and test your pages/settings.

The other option is **Wi-Fi > Firmware Update** in Station mode, using a direct
HTTPS URL to a compatible application image; a GitHub source page URL is not a firmware image.
See [Wi-Fi/OTA notes](next-release-wifi-ota.md) for details.

OTA can retain a working slot and roll back a failed trial before confirmation.
It cannot promise recovery from every fault. USB installation can overwrite a
working slot and reset OTA metadata, so it is not the same rollback-safe trial.
Normal settings persist in NVS; do not erase NVS or change partitions during an update.

## Troubleshooting checklist

| What you see | What to try |
| --- | --- |
| No PlatformIO tasks | Check the extension finished installing, open the folder with `platformio.ini`, and reload VS Code. |
| Git not found | Install Git, restart VS Code, and check `git --version`. |
| Download/build error | Check Internet access and the first actual error. Keep the dependency versions; retry Build after correcting the cause. |
| Windows path-too-long or synced-file problems | Use a shorter local clone path outside OneDrive/cloud sync. |
| No port / upload cannot connect | Use a data cable, check Device Manager, try download mode and confirm the current port. |
| Port busy / access denied | Close serial monitors and other apps using the board, then retry. |
| Upload succeeds but screen stays blank | Restart with BOOT released, verify the exact board model and firmware version, then collect startup logs. |
| Repeated restarts | Try a reliable USB power source/cable and collect logs; do not keep erasing settings. |
| Instruments show `--` | Configure the selected data input/source and check it is sending fresh data. |
| Wi-Fi cannot join | Check 2.4 GHz Wi-Fi, password, and the disconnect reason shown in Settings. |

When requesting help, include your board's exact model/revision, computer OS,
firmware version/build time, what you clicked, and the complete error/startup
log. Remove passwords and SmartShunt encryption keys before posting publicly.

## Mac and Linux notes

The project and PlatformIO tasks are the same. Choose the appropriate VS Code
download. On Mac, use Command+Shift+P for the Command Palette and
Command+Shift+X for Extensions. Follow [Git's installer instructions](https://git-scm.com/downloads)
for your OS and use a local folder such as `~/Projects` outside cloud sync.

On Linux, PlatformIO requires the Python virtual-environment package
(`python3-venv` on Debian/Ubuntu) and may need USB access rules. Follow
[PlatformIO's system requirements](https://docs.platformio.org/en/stable/core/installation/requirements.html)
instead of running VS Code as root. Ports often look like `/dev/ttyACM0` or
`/dev/ttyUSB0`; on Mac they often start with `/dev/cu.`. Use `pio device list`
to find yours, replacing the Windows COM port in the example commands.
