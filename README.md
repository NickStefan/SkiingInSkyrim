# Skiing in Skyrim

Player-only skiing movement for Skyrim SE 1.6.640, as an SKSE plugin built on
CommonLibSSE-NG (alandtse v15.7.0, official prebuilt MSVC bundle).

## Build (Linux / SteamOS)

Builds use MSVC 14.51 (`cl.exe` 19.51.36260, matching the CommonLib bundle) under
Proton 10's Wine. Everything is fetched into this directory (`deps/`, `toolchain/`,
`build/`); nothing is installed system-wide.

```sh
python3 tools/fetch_toolchain.py   # once; downloads ~2.5 GB and accepts the VS Build Tools license
python3 tools/build.py             # builds build/mod/SKSE/Plugins/SkiingInSkyrim.dll
python3 tools/build.py test        # builds and runs src/core unit tests under Wine
python3 tools/build.py clean
```

`SIS_PROTON` or `SIS_WINE` select another Wine; `JOBS` limits parallel compiles.
The build uses its own Wine prefix in `build/wine`, with `S:` mapped to this directory.

## Install

Copy the contents of `build/mod` into the game's `Data` folder (or a mod manager
folder). Requires SKSE, Address Library for SKSE Plugins and SKSE Menu Framework.
True Directional Movement is optional; if installed, skiing takes control of the
player's facing from it while skiing mode is on.
The log is written to `SkiingInSkyrim.log` in the SKSE log directory of the game's
Proton prefix (`Documents/My Games/Skyrim Special Edition/SKSE`).

## Acknowledgements

- [Better Facial Animations](https://gitlab.com/cyberquach/better-facial-animations) by Cyberquach, for showing
  CommonLibSSE-NG plugins can be built with MSVC under Wine.
- [msvc-wine](https://github.com/mstorsjo/msvc-wine) by Martin Storsjö, which fetches the toolchain.
- Ice is Slick, for general inspiration with hooking movement and physics.

## License

GPL-3.0-or-later; see [LICENSE](LICENSE). What's compiled into the DLL and under which
license is in [dist/THIRD-PARTY-NOTICES.md](dist/THIRD-PARTY-NOTICES.md), with the license
texts in `dist/licenses/`; the build copies them, the player docs in `dist/` and a link to
the commit into `build/mod/SKSE/Plugins/SkiingInSkyrim/`.

To release: commit and push, build, then `python3 tools/build.py source` for the source zip
to upload next to the mod (the GPL requires the matching source to be available).
