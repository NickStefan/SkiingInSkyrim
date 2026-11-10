# Third-party notices

Skiing in Skyrim is free software under the GNU General Public License, version 3 or (at your option) any later
version; the full text is `LICENSE.txt`. The source code for this build is linked in `commitlink.txt`, and a
source archive is uploaded next to each release.

`SkiingInSkyrim.dll` contains code from the projects below, under their own GPL-compatible licenses. Their license
texts are in `licenses/`.

## CommonLibSSE-NG v15.7.0

https://github.com/alandtse/CommonLibSSE-NG/tree/v15.7.0, statically linked from its official prebuilt bundle.

- GPL-3.0-or-later: `licenses/CommonLibSSE-NG-COPYING.txt`
- Its Modding Exception and GPL-3.0 Linking Exception: `licenses/CommonLibSSE-NG-EXCEPTIONS.md`
- The MIT license its original code was released under: `licenses/CommonLibSSE-NG-MIT.txt`

The prebuilt library is compiled with these, also under their own licenses:

| Library | License | Text |
|---|---|---|
| Hacker Disassembler Engine 64 (hde64) | BSD 2-Clause | `licenses/hde64.txt` |
| [rapidcsv](https://github.com/d99kris/rapidcsv) | BSD 3-Clause | `licenses/rapidcsv.txt` |
| [Xbyak](https://github.com/herumi/xbyak) | BSD 3-Clause | `licenses/xbyak.txt` |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | `licenses/nlohmann-json.txt` |
| [toml11](https://github.com/ToruNiina/toml11) | MIT | `licenses/toml11.txt` |
| [SimpleIni](https://github.com/brofield/simpleini) | MIT | `licenses/SimpleIni.txt` |
| [DirectXMath](https://github.com/microsoft/DirectXMath) | MIT | `licenses/DirectXMath.txt` |
| [DirectXTK](https://github.com/microsoft/DirectXTK) | MIT | `licenses/DirectXTK.txt` |
| [OpenVR](https://github.com/ValveSoftware/openvr) headers | BSD 3-Clause | `licenses/openvr.txt` |

Not every library is necessarily used by this plugin; their notices are included in case any of their code is.

## Compiled into the plugin

| Library | License | Text |
|---|---|---|
| [{fmt}](https://github.com/fmtlib/fmt) 12.2.0 | MIT | `licenses/fmt.txt` |
| [spdlog](https://github.com/gabime/spdlog) 1.17.0 | MIT | `licenses/spdlog.txt` |

## API headers

These headers are compiled in so the plugin can talk to the mods at runtime. The mods themselves are separate
downloads and are not included.

- [True Directional Movement](https://github.com/ersh1/TrueDirectionalMovement) API header
  (`TrueDirectionalMovementAPI.h`), GPL-3.0 (`LICENSE.txt`) with its modding exception
  (`licenses/TrueDirectionalMovement-EXCEPTIONS.txt`).
- [SKSE Menu Framework](https://github.com/QTR-Modding/SKSE-Menu-Framework-3) consumer header
  (`SKSEMenuFramework.h`), GPL-3.0 (`LICENSE.txt`).

## Not included

SKSE, Address Library, SKSE Menu Framework and True Directional Movement are separate installs. The Microsoft
Visual C++ runtime is installed by the player. No other mod's code, meshes, textures, animations or sounds are
included; the skis currently use a vanilla mesh from the player's own game.
