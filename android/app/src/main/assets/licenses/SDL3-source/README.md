# SDL Switch SPI compatibility patch

Base: SDL 3.2.28, upstream https://github.com/libsdl-org/SDL/tree/release-3.2.28
Official ZIP SHA256: `24a30069af514a6c6b773bdc8ccca8b321661b251381acc1daeebf8c8f4a109a`.
Upstream copyright and zlib license are retained in LICENSE.txt and in the patched source.
This is a Wiiz local modification, not an upstream SDL release or accepted upstream fix.

Only Windows builds apply `cmake/PatchSDLSwitch.cmake` after FetchContent. The patch
reuses SDL's Switch HIDAPI driver, calibration parser and all input decoding.
It replaces the SPI metadata equality check with an address/data-size check.
It adds no protocol implementation, dependency or ROM Mapper code.

Rationale: https://github.com/libsdl-org/SDL/issues/16434, including its verification
with SDL and RetroArch, describes third-party Switch controllers returning more
SPI data than requested. Rather than ignoring length entirely, Wiiz requires the
same address, at least the requested length, and the full reported payload within
the received report and SDL's data array. Short, mismatched or truncated replies
remain rejected. ACK and subcommand checks remain upstream.

BSP-D3 in Switch Bluetooth mode was measured on Windows: request 0x8010/22
returns 0x8010/24; request 0x603d/18 returns 0x603d/25. Both reports have 64 bytes.
Unmodified SDL fails with `Couldn't load stick calibration`. Patched SDL opens
the same device through `SDL_JOYSTICK_HIDAPI_SWITCH`, without changing its mode.
Tests cover exact/long replies and mismatched address, short data and truncation.
Physical input validation and limitations are recorded in docs/gamepad.md.

BSP-D3 additionally reports its four face buttons in labeled order in this
mode. The measured pair of SPI reply lengths (0x8010:22->24 and
0x603d:18->25) enables SDL's existing `m_bUseButtonLabels` / `RemapButton`
path for that device. Ordinary Pro calibration replies do not activate it.
Xbox drivers and Android builds are unchanged. Physical testing in the order
right, bottom, left, top produced game A, game B, Select, Start respectively.

`wiiz-switch-spi.patch` records the exact modified upstream source. The CMake
script and helper are installed with this record and the existing SDL license.
Android does not apply this patch; its existing Android input path is unchanged.
