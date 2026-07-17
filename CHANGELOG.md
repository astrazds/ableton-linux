# Changelog

## 2026.07.17.2

- Replace WineASIO with PipeASIO 1.2.2, a native PipeWire ASIO driver. Removed the stale WineASIO entry. Defaults live in ~/.config/pipeasio/config.ini.
- Fixes for regressions in theme support.
- Fixed Webview corruption specific to the Ableton Learn View.
- Added upgrade path for the installer (see the reademe file for details).

## 2026.07.17.1

- Added ntsync. Without it, users reported about a core of CPU in wineserver usage while Live runs. Live now idles at approx 2%-5% of total CPU.