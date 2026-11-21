# Manual acceptance

The owner's check of a release candidate on the physical device, before the endurance
run of [stability.md](stability.md). One candidate: one firmware release and one catalog
state, both written down below. A row passes only when the panel, not a screenshot,
shows what the row expects.

Write the result of every row: `ok`, `fail` with what was seen, or `skip` with the reason.
A photograph of the panel is the evidence for a `fail`. Results of a completed pass go in
a dated record beside this file, as [acceptance-2026-11-20.md](acceptance-2026-11-20.md) does.

## Candidate

| Item | Value |
|---|---|
| Firmware release | |
| Firmware image ID (Device page) | |
| Recovery version | |
| Catalog commit | |
| Applications installed, with versions | |
| Font, language, power profile | |
| Date, who | |

## 1. Update and identity

| # | Do | Expect | Result |
|---|---|---|---|
| 1.1 | Device page, Firmware, Check for updates | The candidate is offered; no older or equal version is | |
| 1.2 | Install it; confirm on the device | Recovery screen, progress, restart into the candidate | |
| 1.3 | Device page after restart | Version and image ID are the candidate's; probation completes | |
| 1.4 | Compare settings and installed applications with before | Nothing lost, no application reset to defaults | |
| 1.5 | Installed apps, Update all | Every application reaches the catalog's version; ISS and Life open afterwards | |
| 1.6 | Look for GitHub activity in the menu and in Installed apps | Note whether it is still installed; remove it by hand if so | |

## 2. Buttons and navigation

| # | Do | Expect | Result |
|---|---|---|---|
| 2.1 | Short BACK in an application | One page back, or out of it | |
| 2.2 | Hold BACK past 600 ms and let go | The menu when the button is released, the stack cleared | |
| 2.3 | Hold BACK for two seconds, then press DOWN | The next application of the menu's order opens; no menu in between | |
| 2.4 | Hold BACK, press UP | The previous one | |
| 2.5 | Hold BACK, press DOWN three times | Three steps, system pages never among them | |
| 2.6 | From the last application, hold BACK and press DOWN | Wraps to the first | |
| 2.7 | After a switch, release BACK | Nothing more happens: no page back, no menu | |
| 2.8 | Hold BACK alone for five seconds | Nothing while it is held; the menu at the release | |
| 2.9 | Hold BACK and UP together for less than ten seconds | No restart | |
| 2.10 | In the menu: long OK, move a row, OK; then repeat 2.3 | The switch follows the new order | |
| 2.11 | The two-finger gesture on the case | Comfortable with one hand; note if not | |
| 2.12 | In the menu: hold BACK, press DOWN | The selection moves a row; no application opens | |

## 3. Air quality and the redrawn icons

| # | Do | Expect | Result |
|---|---|---|---|
| 3.1 | Open Air quality with a place set | Picture, index and particle readings in one group; four tiles under the rule | |
| 3.2 | Read the tile scale from arm's length | Tall and low steps are told apart | |
| 3.3 | Look at the flower, the molecule and the hero picture | Lines whole, no broken or faded strokes after a partial refresh | |
| 3.4 | Leave it on the screen through several data updates | No ghost of the old index around the new one | |
| 3.5 | A place with good air and one with bad, if reachable | Leaf, wind and mask each read as what they are | |
| 3.6 | Russian with the Jersey font | No caption touches a number or the rule | |

## 4. Session and background delivery

| # | Do | Expect | Result |
|---|---|---|---|
| 4.1 | Life: let it run, switch to the next application with BACK and DOWN, come back with BACK and UP | The same field and generation count, from where it stopped; it does not run while away | |
| 4.2 | ISS: open, switch away for some minutes, come back | The track and position are kept; the night screen is inverted whole | |
| 4.3 | Calendar or HA: go two pages in, wait for a background poll of another application, look again | The same page | |
| 4.4 | Leave an application with BACK from its first page, open it again | It starts from its first page | |
| 4.5 | After 4.1, a short BACK | The menu, not the application switched from | |
| 4.6 | Life: let it run, long BACK, open it from the menu | A new field: opening from the menu is a start anew | |

## 5. Every application, once

Open each, wait for its data, press every button it uses.

| Application | Shows data | Buttons | Russian | Result |
|---|---|---|---|---|
| air | | | | |
| calendar | | | | |
| clock | | | | |
| ha | | | | |
| iss | | | | |
| kit | | | | |
| life | | | | |
| metals | | | | |
| pomodoro | | | | |
| rates | | | | |
| status | | | | |
| stocks | | | | |
| stopwatch | | | | |
| sun | | | | |
| timer | | | | |
| weather | | | | |

## 6. Device page

| # | Do | Expect | Result |
|---|---|---|---|
| 6.1 | Catalog: install an application not installed | Progress at once, then it is in the menu | |
| 6.2 | Remove it | Gone from the menu and the capacity bar | |
| 6.3 | Change a setting of an application, Save | The application redraws with it | |
| 6.4 | A link setting with a default (Service status): clear one, Save, reload | It stays empty; the row is gone on the device | |
| 6.5 | Time: network time off, set a time, Save; then on again | The header follows both ways, each within seconds of Save and without a press | |
| 6.6 | Logging controls, Wi-Fi scan, appearance | Each does what it says | |
| 6.7 | The page on a phone | Usable without horizontal scrolling | |

## 7. Setup and recovery

Do these last: they disconnect the device. Do not confirm Factory reset or Forget
network on the shelf device unless this pass is meant to include them.

| # | Do | Expect | Result |
|---|---|---|---|
| 7.1 | Device page, Enter recovery; leave it without updating | Recovery screen in the saved language; back to main with settings intact | |
| 7.2 | BACK held three seconds at reset | Recovery | |
| 7.3 | A device never set up, or after a factory reset: join its access point from a phone, by the QR code | The setup screen with the QR code; the portal opens by itself on iOS and on Android. Write down the phone and the seconds from joining to the portal | |
| 7.4 | Settings, Wi-Fi, Forget network, OK | The device restarts into its default or last application, or the menu - not the setup screen. No `Perinkpheral-XXXX` network in a phone's list. The header's Wi-Fi icon is struck through; the clock runs; an application that needs the network says "No network". Settings, Wi-Fi: Network "Not set", Status "Off", and Connect to Wi-Fi in place of Forget network. About: Wi-Fi "Off", no address | |
| 7.5 | With Wi-Fi off: restart the device; then General, Date & time, set the time by hand | It starts the same way, still with no access point; the header shows the time set | |
| 7.6 | With Wi-Fi off: Settings, Wi-Fi, Connect to Wi-Fi; join a network through the portal | The device restarts to the setup screen with its access point up, as in 7.3. After joining it is on the network, the page opens at its address, and the applications and their settings are as before | |
| 7.7 | Flash the candidate from scratch (the flasher's fresh install, which erases the board), set up Wi-Fi, then look at the menu, the log and the device page; open each application; remove Clock on the page, then install it from the Catalog | The menu: Air quality, Calendar, Clock, ISS, Stocks, Stopwatch, Timer, Weather, then Settings and About; no Hello. The log says "installed" once for each of the eight. Each opens. Installed apps: the eight with the versions of `firmware/preinstalled.properties`' commit, Update where the catalog is newer; no default application. Removed, Clock leaves the menu and the Catalog offers Install; installed again, it is in the menu and opens | |

## 8. Alarms, notifications, power

| # | Do | Expect | Result |
|---|---|---|---|
| 8.1 | Timer for one minute, switch to another application | The notification over that application; OK dismisses it | |
| 8.2 | Pomodoro across a restart | It resumes as its screen says | |
| 8.3 | Power profile Auto, no presses for its idle time | The screen keeps its picture; a press wakes it at the first try | |

## After the pass

Any `fail` is a fix in the candidate's branch, not a new release, unless it blocks the
endurance run. The endurance run starts on the same image the pass was made on.
