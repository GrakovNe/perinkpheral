# Threats

What the device has that is worth taking, who could take it, what stands in the way today
and what does not. Checked against the current code: every
"holds" below names what enforces it, and every "does not hold" is a thing an attacker can
do now. `CLAUDE.md` is the specification; where this file says the code falls short of it,
the accepted scope is recorded below.

Nothing here has been tried by anybody who meant harm. It is a reading.

## What is worth taking

1. **The secrets of applications**: a Home Assistant token that can do anything in the
   house, a calendar's private address. In the `appkv` partition, in the
   clear.
2. **The Wi-Fi password.** In the `nvs` partition, in the clear.
3. **The device itself**: what runs on it, what it shows, whether it still answers.
4. **A foothold in the home network**: the device makes requests from inside it.
5. **What the screen shows**: a calendar, who is at home, what plays.

## Who, and what they can do

### An application the user installed

It is not trusted, and it is the attacker the design was built against.

Holds:
- It runs in the interpreter with 64 KB of its own. Every pointer, length and coordinate
  it passes is checked before use (`firmware/components/appsys/app.c`); it cannot read or write the host's
  memory. A handler that does not return in 3 seconds is stopped.
- Its store is its own (`a.<name>`; a name has no dot), its files are its own folder, and
  a file's name cannot leave it. It cannot reach another application's secrets.
- It has no sockets. A request goes through the host, to a host its manifest names, and a
  redirect outside that list is refused. Polls use their manifest URLs;
  code requests use `hosts`. Cross-host redirects drop application headers. `user@host`, a port and the case of letters do not get
  round the comparison.
- It cannot move the server the user gave it: `kv_set` on a `url` or `link` setting is refused. Queued destinations are
  authorized again before sending; headers use the same current settings view.
- The system API - reset, forgetting the network, removing applications - needs
  `"system": true`, which is asked about on the page and on the device's own screen.

Does not hold:
- **It can send its own secrets to any host its manifest names, and nobody is shown that
  list before installation.** It reads the token with `kv_get`; `hosts` is its own word; the page says
  nothing about `hosts` before installing, and the catalog's index does not carry them
  (once it is installed, the Info button on its tile does).
  An application that asks for a Home Assistant token and lists `evil.example` beside
  `{{server}}` installs with one click. This is the largest hole there is.
- It can ask for the screen every 30 seconds, write lines into the log that look like
  the host's, and wear the flash with writes to its store and files. Nuisance, not theft.
- It can ask the home network: `"hosts": ["192.168.1.1"]` is a manifest like any other.
  Plain `http` to a router or a camera, from inside. Again nobody is shown.

### A catalog, or whoever controls one

Holds: a source is `https://` and nothing else (2026-11-13); every file is taken only with
the size and SHA-256 its index gives; the official source cannot be removed.

Does not hold:
- There are no signatures. Whoever can write to a source - its owner, or somebody who took
  the account over - serves whatever they like, and an Update is one click. The official
  source is a GitHub repository: its safety is that account's.
- An update is not shown to differ from what was installed: new hosts, a new request for
  secrets, nothing is pointed out. Only `"system": true` and a certificate authority of its
  own are asked about.

### Somebody on the same network

A guest on the Wi-Fi, a compromised thing in the house, a page in the user's browser.

Holds:
- What cannot be taken back is asked about on the device's own screen: a firmware update,
  a factory reset, a change of the Wi-Fi network, installing an application with system
  access.
- A page of another site cannot use the user's browser against the device, for reading or
  for changing: `Origin` and `Host` are checked, which covers a name rebound to the
  device's address too.
- The page never reads stored secrets or custom links back. Hidden settings are
  a form rule; their values remain available through the API unless secret or link.

Does not hold:
- **There is no password on the page.** Anybody who can reach port 80 may install and
  remove applications, add a catalog, change every setting, restart the device or enter
  recovery, read the log and what is set (but for secrets) - with a command line; the checks above are
  against browsers. The owner chose this scope on 2026-11-12.
- Entering recovery does not require physical confirmation and interrupts normal
  applications until main is started again. Replacing main still requires a new
  physical OK after recovery has shown the image's version. Main refuses direct
  firmware uploads; recovery cannot write its own image, the bootloader or the
  partition table through its HTTP API.
- **They can replace a secret, and move a server.** Settings of an application are
  written without a question: point HA Companion's `server` at their own machine and the
  secrets are forgotten, as designed - but the user, seeing "Access denied", types the
  token in again, and now it goes to them. Adding a catalog and installing from it works
  the same way without the user at all.
- **The page is plain `http`.** A token typed into it crosses the network in the clear.
- In a build with the development tools (`APP_DEV_CONSOLE`, on by default): `/api/dev/flash`
  refuses direct updates (recovery requires physical OK), `/api/dev/kv` writes any key of any
  application, `/api/dev/screen` gives the screen. Such a build is for a bench; releases omit it.

### Somebody near the device with a radio, not on the network

- While the device has no network, and after two minutes of failing to join one, it puts
  up an **open** access point. Whoever is in range is then "somebody on the same network",
  with everything above. Jamming the home Wi-Fi for two minutes is enough to bring it up.
  From there, as from any network, what cannot be undone - another network, a firmware,
  a reset - is asked about on the screen; the rest is not.
- Recovery always exposes the open `perink-recovery` access point at `192.168.4.1`,
  including while it is connected to the saved Wi-Fi. Its HTTP surface is limited
  to the upload page, status and firmware upload. It applies its own Host/Origin
  checks and physical confirmation; it has no settings, application or development
  API. It reads saved settings and glyph packs without modifying them.

### Somebody with the device in their hands

- Nothing holds. The flash is not encrypted and the boot is not verified: a cable reads
  the Wi-Fi password and every token, and writes any firmware. The buttons reach
  everything in Settings. For a thing on a desk at home this is accepted; it should be
  said to whoever builds one.

### A server the device talks to

Holds: HTTPS is verified against the browsers' roots; an answer over 256 KB is cut off, 4 MB for one handed on in pieces; a
JSON answer is picked apart by the host in a stream, with bounds; text that is not ASCII
is drawn from glyph packs, spelled in ASCII or shown as `?`, never run.

Does not hold: plain `http` is allowed, so a token sent over it crosses the network in the
clear. Packaged authority roots are supported for that application only; a setting for
a user-supplied server certificate is not implemented.

### A companion page

A page of the application's author, opened from the device's page. It is given the
application's setting declarations and values except secrets and links, and hands back values the user
then saves. Hidden settings have no form fields and cannot be filled this way. It could hand back a `server` of its own and wait for the token to follow: the
same trick as above, played on a user who trusts the form.

## The level, and what follows from it

The owner's word (2026-11-13): this is a display on a desk at home. Nobody jams a Wi-Fi
network or sits in the house's network to take a token from it, and the device is not
defended against that. What is written above stays as a description of how things are,
not as a list of work.

One attacker is real at this level, because it needs nobody near the house: **an
application from somebody else's catalog**. Against it:

1. **The owner's decision, 2026-11-13.** **What an application is, in words, for whoever
   wants to look**: a button on its tile beside the gear opens its manifest as a person
   reads it - where it connects, how often, what it keeps. Not before installing: an
   application has to install in one click, or nobody will use the catalog.
2. **Home Assistant** uses an integration-issued secret for a device screen, rather than
   the user's general access token. The integration is in the separate
   `perinkpheral-hacs` repository; this repository does not test an HA server.

Accepted and not to be built: a password or a question for settings and catalogs, a
password on the access point, a secret an application cannot read, HTTPS on the page,
signatures, encrypted flash. The development tools stay in the build while the project is
being developed; a release is built without them.

## Decided, so that it is not argued again

No backend, no accounts, no telemetry. The level above: a thing at home, not a product
to be defended against its own network. No signatures for applications before there is
somebody to sign. The page open to the home network for what can be taken back. The
battery's and the device's physical safety are the builder's.
