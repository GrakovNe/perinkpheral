# Acceptance work, 2026-11-18

Work continued after midnight on 2026-11-19. This records delivery and remaining
checks, not a release-readiness certificate. The owner stopped the endurance run;
**do not restart it until the owner explicitly asks.**

## Enclosure and assembly

- Published the rev1 STEP source, seven print-oriented 3MF files and their ZIP in
  `hardware/step/rev1/`, with the reproducible exporter in `tools/case/`.
- Recentered the lettering after extending the frame around the base. Raised the
  ESP locating stops by 0.5 mm and updated the downloadable bottom and STEP source.
- Kept assembly simple: parts placed from above and screwed together. No retaining
  bridge across the soldered ESP board, wedges or required TPU parts were added.
- Reworked the bilingual print downloads and assembly order, including button caps
  before closing the enclosure. The user confirmed the assembled prototype works;
  the revised locating stops still need a physical assembly check.

## Perinkpheral OS and tools

- Fixed app auto-update settings, manual update queuing, immediate progress/result
  notices and stale catalog versions being offered as updates.
- Added app authors and personal-repository publishing. Updated the CLI/CLion
  workflow and reserved the Perinkpheral author identity for official apps.
- Simplified device/settings/log views: reset reason, fewer diagnostic fields,
  app-capacity bar, logging controls, Wi-Fi scanning and appearance settings.
- Reviewed recovery entry, OTA preparation, captive portal routing and preservation
  of settings/application data. Automatic captive-portal opening still needs phones.
- Added bounded response-header access for app authentication in firmware 0.1.51.
  Repeated cookies stay separate; overflow fails closed; ABI 3 remains compatible.
- Named the operating system **Perinkpheral OS**. Updated architecture/SDK prose,
  EN/RU guides, portal and IDE hints; network hosts and ABI names remain unchanged.

## Applications and integrations

- Audited application CPU, memory, network and storage use. Changes reduce repeated
  KV writes, unnecessary fetches/redraws and JSON fields; incomplete replies keep
  the last complete data. Life keeps running without a cycle/generation cutoff.
- Improved Pomodoro controls, Air spacing, currency charts and calendar details.
  Air's removed particle icons remain removed; pollen names remain present.
- Optimized Bad Apple decoding: measured about 12.6% less decoder time and lower
  static data use, with identical decoded frames. This is not a panel frame-rate claim.
- GitHub 0.1.3 includes the padded-calendar parser, verified with a 368-day response,
  and requests data after app updates without duplicating a request already made
  by the new instance. The owner confirmed that the calendar appeared on the
  physical panel after installation of 0.1.3.
- ISS 0.1.9 uses one light/dark screen palette according to day/night below the station,
  a thin dashed observed track, metre altitude and the approved centered telemetry.
  Its Russian description is exactly “МКС на мировой карте”.
- Reviewed Home Assistant pairing concurrency and isolation. Investigated the local
  Keenetic API; the application is still a proposal: [research and plan](../keenetic.md).
- Recorded the [Jersey replacement proposal](jersey.md). Cyrillic is mandatory;
  Tiny5 is a trial candidate. No production font has changed.

## Portal

- Corrected assembly/download copy and recovery instructions in both languages.
- Added SDK publishing instructions, checked the architecture diagram and improved
  route/language search metadata. Stock demo values no longer grow linearly.
- Changed the Telegram contact to `@perinkpheral` through the shared link source.
  Portal v0.1.25 is live; all 21 published HTML pages were checked for the new link.

## Delivery and remaining checks

The device is on release **0.1.54**, with **GitHub 0.1.3** and **ISS 0.1.9**.
The published firmware binary was verified against its release digest, installed
through recovery with physical confirmation, and checked by image identity with
probation complete. Settings and other installed applications matched their
pre-update snapshots; ISS settings were preserved. Public catalog package hashes
were also verified. These checks do not establish what the physical panel shows.

- GitHub's calendar is confirmed on the physical panel by the owner. The forced
  request returned HTTP 200; native tests and the published WASM accept the saved
  reply. The precise cause of the earlier stale error was not isolated. Tokens
  and private replies are not included in the repository.
- Check the new ISS palette/track and the ESP locating stops on the physical build.
- Repeated `HTTP_CLIENT invalid host` messages remain unattributed. An empty Status
  app URL is a code-backed candidate, not a confirmed diagnosis; the observed GitHub
  request itself returned HTTP 200.
- Wi-Fi power limiting has not produced another watchdog in the short observations,
  but frequent maintenance resets prevent a multi-hour stability conclusion. The
  continuous run remains stopped. Follow [the existing plan](stability.md) when asked.

Reviewed changes: firmware PRs [7](https://github.com/GrakovNe/perinkpheral/pull/7),
[8](https://github.com/GrakovNe/perinkpheral/pull/8),
[9](https://github.com/GrakovNe/perinkpheral/pull/9),
[10](https://github.com/GrakovNe/perinkpheral/pull/10);
apps PRs [6](https://github.com/GrakovNe/perinkpheral-apps/pull/6),
[7](https://github.com/GrakovNe/perinkpheral-apps/pull/7),
[8](https://github.com/GrakovNe/perinkpheral-apps/pull/8),
[9](https://github.com/GrakovNe/perinkpheral-apps/pull/9);
portal PRs [6](https://github.com/GrakovNe/perinkpheral-dev-portal/pull/6),
[7](https://github.com/GrakovNe/perinkpheral-dev-portal/pull/7);
[CLion PR 2](https://github.com/GrakovNe/perinkpheral-clion-plugin/pull/2).

## Whole-screen inversion

The owner requested that ISS invert the **whole display**, including Perinkpheral OS's
header and the telemetry area, instead of only the map. Firmware 0.1.54 adds
`screen_set_inverted(int32_t inverted)`, owned by the application scene. It composes
the ordinary light frame, then inverts the complete frame once. Navigation resets
or restores the preference without changing other applications. The API and its
lifetime rules are documented in the SDK and both language versions of the guide.
Host tests cover composition, background delivery and scene lifetime. ISS 0.1.9
passed eight simulator scenarios with twelve frames: the daytime frame stayed
unchanged, and night frames invert the header and telemetry with the map. Both
updates are installed; the new appearance still needs the owner's panel check.
