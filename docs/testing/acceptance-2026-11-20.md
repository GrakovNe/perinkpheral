# Manual acceptance, 20 November 2026

The owner's pass over [acceptance.md](acceptance.md), rows read out by the owner and
written down as said, on firmware 0.1.63.

Three rows failed - 2.3, 4.1 and 6.5 - and one was skipped, 7.3. The three are fixed in
0.1.64, with the chord turned off in the menu. To pass on 0.1.64: rows 2.2, 2.3, 2.8, 2.12,
4.1, 4.5, 4.6 and 6.5 of [acceptance.md](acceptance.md) as it is after those fixes.

## Candidate

| Item | Value |
|---|---|
| Firmware release | 0.1.63; the device had 0.1.62 (image d173a04a...) before the pass |
| Firmware image ID (Device page) | 3f79997e30d223a53e3973359e90ee797022deada7d9076bae9ab4e1e44c7c0b (read over the API) |
| Recovery version | |
| Catalog commit | 1f26433 (Bad Apple removed) |
| Applications installed, with versions | Before: ha 0.3.17, stocks 0.2.10, life 0.1.3, calendar 0.4.15, air 0.1.6, rates 0.1.3, iss 0.1.10, metals 0.1.2, weather 0.2.15, pomodoro 0.1.2, stopwatch 0.1.11, sun 0.1.9, status 0.1.1, timer 0.3.10, clock 0.1.9; kit not installed. After 1.5 the same list: every installed application was already at the catalog's version |
| Font, language, power profile | Departure Mono, Russian, no power saving |
| Date, who | 2026-11-20, the owner |

## 1. Update and identity

| # | Do | Expect | Result |
|---|---|---|---|
| 1.1 | Device page, Firmware, Check for updates | The candidate is offered; no older or equal version is | ok |
| 1.2 | Install it; confirm on the device | Recovery screen, progress, restart into the candidate | ok |
| 1.3 | Device page after restart | Version and image ID are the candidate's; probation completes | ok; the API reports 0.1.63, on_probation false |
| 1.4 | Compare settings and installed applications with before | Nothing lost, no application reset to defaults | ok; the list of applications and versions is the same over the API |
| 1.5 | Installed apps, Update all | Every application reaches the catalog's version; ISS and Life open afterwards | ok |
| 1.6 | Look for GitHub activity in the menu and in Installed apps | Note whether it is still installed; remove it by hand if so | ok; not installed |

## 2. Buttons and navigation

| # | Do | Expect | Result |
|---|---|---|---|
| 2.1 | Short BACK in an application | One page back, or out of it | ok |
| 2.2 | Long BACK anywhere | The menu, the stack cleared | ok on 0.1.63; repeat on 0.1.64, where it acts at the release |
| 2.3 | Hold BACK, press DOWN | The next application of the menu's order opens; no menu in between | fail: the chord works only when UP or DOWN goes down within 600 ms of BACK. Held longer, as a user going through the applications does, the long BACK fires first and the menu opens. The firmware does what it was written to do; the design is wrong. Fixed in 0.1.64 (the long BACK at the release; no chord in the menu): rows 2.2, 2.3, 2.8 and the new 2.12 of acceptance.md are to be passed on it |
| 2.4 | Hold BACK, press UP | The previous one | ok on 0.1.63, with UP pressed within 600 ms |
| 2.5 | Hold BACK, press DOWN three times | Three steps, system pages never among them | ok on 0.1.63, pressed within 600 ms |
| 2.6 | From the last application, hold BACK and press DOWN | Wraps to the first | ok on 0.1.63, pressed within 600 ms |
| 2.7 | After a switch, release BACK | Nothing more happens: no page back, no menu | ok |
| 2.8 | Hold BACK alone past 600 ms | The menu, as in 2.2 | ok on 0.1.63; repeat on 0.1.64, where it acts at the release |
| 2.9 | Hold BACK and UP together for less than ten seconds | No restart | ok |
| 2.10 | In the menu: long OK, move a row, OK; then repeat 2.3 | The switch follows the new order | ok |
| 2.11 | The two-finger gesture on the case | Comfortable with one hand; note if not | ok: comfortable on the whole. A double press of UP or DOWN was considered and rejected by the owner: it takes the buttons from applications or delays every press. The chord stays until feedback from use |

## 3. Air quality and the redrawn icons

| # | Do | Expect | Result |
|---|---|---|---|
| 3.1 | Open Air quality with a place set | Picture, index and particle readings in one group; four tiles under the rule | ok |
| 3.2 | Read the tile scale from arm's length | Tall and low steps are told apart | ok |
| 3.3 | Look at the flower, the molecule and the hero picture | Lines whole, no broken or faded strokes after a partial refresh | ok |
| 3.4 | Leave it on the screen through several data updates | No ghost of the old index around the new one | ok |
| 3.5 | A place with good air and one with bad, if reachable | Leaf, wind and mask each read as what they are | ok |
| 3.6 | Russian with the Jersey font | No caption touches a number or the rule | ok |

## 4. Session and background delivery

| # | Do | Expect | Result |
|---|---|---|---|
| 4.1 | Life: let it run, switch to another application, come back | The same field continues; it does not start anew | fail: the generation count and the field start anew after a switch away and back. The application keeps its session correctly; Perinkpheral OS discards it: the chord opens the neighbour as the menu does (`wm_open_from_home`), which empties the stack, and a long BACK empties it too. No way of leaving that a user has keeps the session. Life does not run while away - one instance, by design |
| 4.2 | ISS: open, switch away for some minutes, come back | The track and position are kept; the night screen is inverted whole | ok |
| 4.3 | Calendar or HA: go two pages in, wait for a background poll of another application, look again | The same page | ok |
| 4.4 | Leave an application with BACK from its first page, open it again | It starts from its first page | ok |

## 5. Every application, once

Open each, wait for its data, press every button it uses.

| Application | Shows data | Buttons | Russian | Result |
|---|---|---|---|---|
| air | ok | ok | ok | ok |
| calendar | ok | ok | ok | ok |
| clock | ok | ok | ok | ok |
| ha | ok | ok | ok | ok |
| iss | ok | ok | ok | ok |
| kit | ok | ok | ok | ok; installed during the pass, 0.1.13 |
| life | ok | ok | ok | ok |
| metals | ok | ok | ok | ok |
| pomodoro | ok | ok | ok | ok |
| rates | ok | ok | ok | ok |
| status | ok | ok | ok | ok |
| stocks | ok | ok | ok | ok |
| stopwatch | ok | ok | ok | ok |
| sun | ok | ok | ok | ok |
| timer | ok | ok | ok | ok |
| weather | ok | ok | ok | ok |

## 6. Device page

| # | Do | Expect | Result |
|---|---|---|---|
| 6.1 | Catalog: install an application not installed | Progress at once, then it is in the menu | ok; kit 0.1.13 from the catalog |
| 6.2 | Remove it | Gone from the menu and the capacity bar | ok |
| 6.3 | Change a setting of an application, Save | The application redraws with it | ok |
| 6.4 | A link setting with a default (Service status): clear one, Save, reload | It stays empty; the row is gone on the device | ok |
| 6.5 | Time: network time off, set a time, Save; then on again | The header follows both ways | fail: the time is set, but the header shows the old one until a press. A change of the header counted as data and waited for the refresh policy. Fixed in 0.1.64: a clock or a setting set by hand is shown at once; to be passed on it |
| 6.6 | Logging controls, Wi-Fi scan, appearance | Each does what it says | ok |
| 6.7 | The page on a phone | Usable without horizontal scrolling | ok |

## 7. Setup and recovery

Do these last: they disconnect the device. Do not confirm Factory reset or Forget
network on the shelf device unless this pass is meant to include them.

| # | Do | Expect | Result |
|---|---|---|---|
| 7.1 | Device page, Enter recovery; leave it without updating | Recovery screen in the saved language; back to main with settings intact | ok |
| 7.2 | BACK held three seconds at reset | Recovery | ok |
| 7.3 | Setup access point from a phone (only on a device being set up) | The portal opens by itself on iOS and on Android | skip: the shelf device is set up; needs a device being set up anew |

## 8. Alarms, notifications, power

| # | Do | Expect | Result |
|---|---|---|---|
| 8.1 | Timer for one minute, switch to another application | The notification over that application; OK dismisses it | ok |
| 8.2 | Pomodoro across a restart | It resumes as its screen says | ok |
| 8.3 | Power profile Auto, no presses for its idle time | The screen keeps its picture; a press wakes it at the first try | ok |
