# CrossInk — Gersh Edition

This repository is Gersh's maintained downstream of the official [`uxjulia/CrossInk`](https://github.com/uxjulia/CrossInk) firmware for the XTEINK X3/X4 family.

It exists to preserve a small, reviewable set of device-tested or build-validated modifications that are not yet available in official CrossInk releases. It is unofficial and is not affiliated with XTEINK or the official CrossInk maintainer.

## Lineage policy

- Canonical product source: `uxjulia/CrossInk`
- Current candidate base: official tag `v1.5.0-rc-2`
- Current custom source commit: `802f76fd09b49717756ca0ed771b34db750e65b7`
- CrossPoint-derived binaries are not used as release bases.
- New official CrossInk releases are audited first; custom changes are forward-ported manually when lifecycle or architecture changed.

## Custom feature set

- Quick Lock that leaves the current page visible while suppressing device input
- Reading-statistics pause/resume while Quick Lock is active
- Configurable Quick Lock timeout with deep-sleep restoration
- Quick Lock in Short Power, Long Power, and Power + Right menus
- Expanded configurable Power + Right actions while preserving immediate ordinary Right presses
- Held-Power and wake-path safeguards
- OTA-slot confirmation before sleep/restart so the device does not silently roll back after flashing
- Conservative OPDS catalog lifecycle and network-transfer tuning for the ESP32-C3/X3
- Browser upload backpressure and yielding changes

## Release channels

- **Candidate:** builds that pass source, host-test, firmware-build, image-integrity, identity, and size gates but still require physical X3 validation.
- **Stable:** candidates promoted only after successful flash, sleep/wake, Quick Lock, button, OPDS, upload, and settings-persistence checks on an X3.

The repository default branch may point to the newest candidate source. That does not make its firmware hardware-validated. Consult the attached GitHub Release notes before flashing.

## Current candidate

`v1.5.0-rc2-gersh.1`

Status:

- Exact official CrossInk RC-2 ancestry verified
- Focused Quick Lock/button tests passed: 7/7
- Clean ESP32-C3 X3/X4 build passed
- Firmware image fits the OTA app partition
- Embedded product identity is CrossInk/X3-X4
- Physical validation of this exact RC-2 candidate is still required

## Safety

1. Back up settings and reading state before flashing.
2. Do not flash an artifact whose checksum differs from its release notes.
3. Treat candidate releases as controlled tests.
4. Verify the About screen immediately after flashing and again after deep sleep/restart.
5. Keep a known-good rollback image available.

## Upstream contributions

Features are kept separable so clean pieces can still be proposed to official CrossInk or canonical CrossPoint where appropriate. A custom downstream release does not imply that a feature has been accepted upstream.
