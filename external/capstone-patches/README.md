# capstone and tiny-std patches

The firmware links two submodules, Michal Schulz's forks of capstone and tiny-std, at the commits
Emu68 pinned:

| submodule | repository | commit | patches |
|---|---|---|---|
| `external/capstone` | https://github.com/michalsc/capstone | `63f7f3ac` | `external/capstone-patches/0001-*.patch`, `0002-*.patch` |
| `external/tiny-stl` | https://github.com/michalsc/tiny-std | `6772983` | `external/tiny-stl-patches/0001-*.patch` |

The patches make both build for the freestanding big-endian ARMv7 firmware, and fix capstone's
M68K printer on 32-bit hosts.

```sh
git clone https://github.com/michalsc/capstone.git external/capstone
git -C external/capstone checkout 63f7f3ac
(cd external/capstone && for p in ../capstone-patches/0*.patch; do patch -p1 < "$p"; done)

git clone https://github.com/michalsc/tiny-std.git external/tiny-stl
git -C external/tiny-stl checkout 6772983
(cd external/tiny-stl && patch -p1 < ../tiny-stl-patches/0001-bare-metal-tweaks.patch)
```
