# WinDrop licensing

WinDrop is a fork, so it does not have one licence. Each part follows the licence of whoever wrote it, and this file says which applies where.

This is a good-faith summary, not legal advice. Where there is doubt, the text of each licence is what counts.

[Português](LICENSING.pt-BR.md)

## What belongs to this fork

Noncommercial use, with the source credited: the [PolyForm Noncommercial License 1.0.0](LICENSE-WINDROP.md).

It covers this fork's original work:

- the `mt7902/` folder (the radio for the MediaTek MT7902);
- the name WinDrop, the logo (`docs/assets/windrop.svg`) and the icon;
- the documentation in `README.md` and `docs/` (except `docs/UPSTREAM-README.md`);
- the scripts and tools this fork added in `scripts/` and `tools/`;
- the changes this fork made to files of the original project.

In practice:

- **You may:** use, study, modify and redistribute it for personal, study, research and non-profit purposes.
- **You may not:** use it commercially. That includes selling the program, selling a fork of it, or building it into a paid product or service.
- **You must:** keep the notice below and the text of the licence in every copy or derived work.

> Required Notice: Copyright (c) 2026 matheusbgodoi (https://github.com/matheusbgodoi/WinDrop)

Commercial use needs a separate licence, agreed with the author.

## What comes from other projects

These parts stay under their original licence. This fork cannot change them or add restrictions to them.

| Part | Origin | Licence |
|---|---|---|
| `app/`, `driver/`, `installer/`, `scripts/` (what was already there) | [winawdl](https://github.com/raph559/winawdl), by Raphaël Richaud | MIT, in [LICENSE](LICENSE) |
| `airdrop/` | winawdl, adapted from omdrop | MIT, in [airdrop/LICENSE](airdrop/LICENSE) |
| `awdl/` and `upstream/owl` | winawdl and [OWL](https://github.com/seemoo-lab/owl) | GPL-3.0 or later, in [awdl/LICENSE](awdl/LICENSE) |
| `radio/` and `upstream/devourer` | winawdl and devourer | GPL-2.0, in [radio/LICENSE](radio/LICENSE). Not shipped in WinDrop's installer |
| `wintun.dll` | [Wintun](https://www.wintun.net/), by WireGuard | the binaries' own licence, shipped in the installer |
| The MT7902's firmware | MediaTek, through linux-firmware | MediaTek's licence, shipped in the installer. Not in this repository |

What follows from that:

- `awdld.exe` is GPL-3.0. Whoever gets the installer may use, modify and redistribute that program under the GPL, commercially too. The noncommercial restriction does not reach that part.
- The same goes for what is MIT: the restriction covers only what this fork wrote.
- The code in `mt7902/` was written from a study of the Linux [mt76](https://github.com/openwrt/mt76) driver, which is BSD-3-Clause-Clear and ISC. The bring-up sequences and the command formats come from it.

## Trademarks

AirDrop, AWDL, iPhone, Mac and iCloud are trademarks of Apple. MediaTek is a trademark of MediaTek. This project is not affiliated with either.
