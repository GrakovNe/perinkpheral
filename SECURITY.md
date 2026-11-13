# Threats

What the device has that is worth taking, who could take it, what stands in the way today
and what does not. Written on 2026-11-13 from the code as it is, not from intentions: every
"holds" below names what enforces it, and every "does not hold" is a thing an attacker can
do now. `CLAUDE.md` is the specification; where this file says the code falls short of it,
this file is the list of work.

Nothing here has been tried by anybody who meant harm. It is a reading.

## What is worth taking

1. **The secrets of applications**: a Home Assistant token that can do anything in the
   house, a Plex token, a calendar's private address. In the `appkv` partition, in the
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
  it passes is checked before use (`appsys/app.c`); it cannot read or write the host's
  memory. A handler that does not return in 3 seconds is stopped.
- Its store is its own (`a.<name>`; a name has no dot), its files are its own folder, and
  a file's name cannot leave it. It cannot reach another application's secrets.
- It has no sockets. A request goes through the host, to a host its manifest names, and a
  redirect elsewhere is refused. `user@host`, a port and the case of letters do not get
  round the comparison.
- It cannot move the server the user gave it: `kv_set` on a `url` setting is refused
  (2026-11-13; before that it could, and send the user's token anywhere).
- The system API - reset, forgetting the network, removing applications - needs
  `"system": true`, which is asked about on the page and on the device's own screen.

Does not hold:
- **It can send its own secrets to any host its manifest names, and nobody is shown that
  list.** It reads the token with `kv_get`; `hosts` is its own word; the page says
  nothing about `hosts` before installing, and the catalog's index does not carry them.
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
  secrets, nothing is pointed out. Only `"system": true` is asked about.

### Somebody on the same network

A guest on the Wi-Fi, a compromised thing in the house, a page in the user's browser.

Holds:
- What cannot be taken back is asked about on the device's own screen: a firmware update,
  a factory reset, a change of the Wi-Fi network, installing an application with system
  access.
- A page of another site cannot use the user's browser against the device, for reading or
  for changing: `Origin` and `Host` are checked, which covers a name rebound to the
  device's address too.
- The page never reads a secret back.

Does not hold:
- **There is no password on the page.** Anybody who can reach port 80 may install and
  remove applications, add a catalog, change every setting, restart the device, read the
  log and what is set (but for secrets) - with a command line; the checks above are
  against browsers. The owner chose this scope on 2026-11-12.
- **They can replace a secret, and move a server.** Settings of an application are
  written without a question: point HA Companion's `server` at their own machine and the
  secrets are forgotten, as designed - but the user, seeing "Access denied", types the
  token in again, and now it goes to them. Adding a catalog and installing from it works
  the same way without the user at all.
- **The page is plain `http`.** A token typed into it crosses the network in the clear.
- In a build with the development tools (`APP_DEV_CONSOLE`, on by default): `/api/dev/flash`
  replaces the firmware with nobody asked, `/api/dev/kv` writes any key of any
  application, `/api/dev/screen` gives the screen. Such a build is for a bench, and the
  one device there is runs it.

### Somebody near the device with a radio, not on the network

- While the device has no network, and after two minutes of failing to join one, it puts
  up an **open** access point. Whoever is in range is then "somebody on the same network",
  with everything above. Jamming the home Wi-Fi for two minutes is enough to bring it up.
  Joining another network from there is asked about on the screen; nothing else is.

### Somebody with the device in their hands

- Nothing holds. The flash is not encrypted and the boot is not verified: a cable reads
  the Wi-Fi password and every token, and writes any firmware. The buttons reach
  everything in Settings. For a thing on a desk at home this is accepted; it should be
  said to whoever builds one.

### A server the device talks to

Holds: HTTPS is verified against the browsers' roots; an answer over 256 KB is cut off; a
JSON answer is picked apart by the host in a stream, with bounds; text that is not ASCII
is carried and not drawn.

Does not hold: plain `http` is allowed and is what a home server gets, so a token in a
header crosses the home network in the clear; a certificate of the user's own is decided
and not built.

### A companion page

A page of the application's author, opened from the device's page. It is given the
application's settings and what is set, without secrets, and hands back values the user
then saves. It could hand back a `server` of its own and wait for the token to follow: the
same trick as above, played on a user who trusts the form.

## What to do, in the order of what it buys

1. **Show what an application reaches, before installing and on an update**: its hosts,
   whether they are in the home network, whether it asks for secrets. The index carries
   them; the page says them in a sentence ("Sends data to: evil.example"); an update that
   adds a host is asked about. Without this the sandbox guards the device and not the
   user's token.
2. **Tie a secret to where it may go.** A `secret` setting names the hosts it may be sent
   to (by default the `url` setting beside it) and the host substitutes it only into
   requests for those; the application gets it from `kv_get` as "set" and not as bytes.
   Then a token cannot leave even to a host the manifest lists. Costs the applications
   that build a request body from a secret by hand - none does today.
3. **A question on the screen for what moves secrets**: a change of a `url` setting, a new
   catalog. They are rare, and they are what turns "can change settings" into "can take
   the token".
4. **Home Assistant: a secret of the device's own instead of the user's token** (the
   integration, item 2 of the plan). What is stolen then opens one address.
5. **The access point**: a password on it, shown on the panel as the QR code already is;
   or at least nothing but the Wi-Fi form served while it is up.
6. **A release build without the development tools**, and the device on the desk running
   one whenever nobody is developing on it.
7. Later, and larger: a password for the page; HTTPS on the page (a certificate nobody
   will trust); signatures for a catalog; encrypted flash. Each was looked at and left:
   the first two fight the "no accounts, set up from a phone" of the vision, the last
   two are for a product and not for a thing one builds.

## Decided, so that it is not argued again

No backend, no accounts, no telemetry. No signatures for applications before there is
somebody to sign. The page open to the home network for what can be taken back. The
battery's and the device's physical safety are the builder's.
