# Safe Exit 1.1.0

This update is mostly about checking for new versions and keeping the interface private.

## What changed

- **Update check** — open Settings and click the version number. Safe Exit asks GitHub for the latest release and tells you if you are already on it. If a newer build exists, the button opens the download page.
- **Popup blur fix** — the "checking for updates" popup now uses the same blur as the other dialogs and covers the whole window instead of getting clipped by the dashboard panel.
- **Interface stays private** — the real UI is compiled into the official `.exe`. Building the public source gives you a simple placeholder screen on purpose, so the interface does not end up in the repo.
- **Build fix** — the private UI is generated automatically before compiling, so the exe cannot accidentally use an old screen. The generated file stays out of Git.
- **New app icon and version info** — the icon is a proper Windows icon now, and the file details show `1.1.0`.

## How to update

1. Download `Safe-Exit.exe` below.
2. Close Safe Exit if it is running.
3. Put the new exe where the old one was and replace it.
4. Run it and click **Yes** when Windows asks for permission.

Your settings are stored in `Documents\Safe Exit`, so your keys and window options stay the same after the update.

## Note

Windows might show a SmartScreen warning because the exe is not signed. Click **More info → Run anyway** if you downloaded it from this page.

Only use Safe Exit when you are actually up against a cheater. If you are not sure, leaving can still count as a loss.

SHA-256: `838146E1D83BF91171350C55CCF3FDC7183A63747219E0B4C2628DC4FBB8D9B5`
