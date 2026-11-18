# Keenetic application proposal

Research on 2026-11-18. No router settings, firmware or applications changed. Login and `show` requests only. This is a proposal, not an installed application.

## Feasibility and the tested router

The owner's router identifies itself as **Ultra (NC-1812), KeeneticOS 5.01.C.8.0-2**, ARM64. Local HTTP at the supplied address accepts the legacy challenge-response login. Authenticated RCI reads and a three-command POST batch worked. No cloud, Entware, proxy or extra router package was needed.

The application has not been implemented. The SDK prerequisite was completed in
firmware 0.1.51: `answer_header()` reads an application's own response headers
inside its data callback, including authentication challenges and repeated
`Set-Cookie` headers. It is an additive ABI 3 call; `host_answer_info_t` is unchanged.

Capture is bounded to 1024 bytes. If the full header set does not fit, every
`answer_header()` lookup returns `HOST_ERR_NO_SPACE`; `answer_info()` still works.
There is no automatic cookie jar. Keep the Keenetic handshake in the application,
bind its cookie to the configured server/account and preserve it across WASM
unloads through application KV only when the session changes. Never write the
cookie or a password to the log. Destination and redirect checks still apply.

## Confirmed data

All paths below are under the local router. Example values are observations, not a performance test or a simultaneous snapshot.

| What | Read / fields | Interpretation and observed result |
|---|---|---|
| CPU | GET `/rci/show/system`: `cpuload` | Percentage; varied from 41 to 7 during the reads. |
| RAM | same: `memory`, `memtotal`, `memfree`, `membuffers`, `memcache` | `630664/1048576` KiB, about 60%. The numerator equals total minus free/buffers/cache here. Do not call Linux free memory the whole available RAM. |
| Router uptime | same: `uptime` | Seconds, returned as a string; around 29 days 11 hours. |
| Tracked connections | same: `conntotal - connfree` | 32768−32385 = 383. This is connection-tracking occupancy, not clients or TCP-only connections. Do not fetch the whole NAT table just to count it. |
| Active clients | GET `/rci/show/ip/hotspot`: `host[].active` | 99 saved records, 56 active. Count true flags, not array length. Stream the response; discard names, addresses and MACs if displaying only a count. |
| Local Wi-Fi associations | GET `/rci/show/associations`: `station[]` | 8 records. RSSI and negotiated txrate/rxrate are available. These are not Internet throughput, and the result is not verified as a whole-mesh client count. |
| Selected WAN | GET `/rci/show/internet/status`: `gateway.interface`; interface metadata | PPPoE0. `internet`, `reliable`, `gateway-accessible`, `dns-accessible` and `captive-accessible` are available. Link-up alone does not prove Internet access. |
| WAN traffic | POST `/rci/`, `show.interface{name,stat}` | PPPoE0 supplies `rxbytes`, `txbytes`, `rxpackets`, `txpackets`, errors/drops, fractional-second `timestamp`, `last-overflow`, `rxspeed`, `txspeed`. Byte counters exceed 32 bits. |
| Radio temperatures | GET `/rci/show/interface`: WifiMaster0/1 `temperature` | 61 and 58 degrees. These are radio temperatures, not CPU temperature. Channel/bandwidth and PHY bitrate also exist. |

For this router, a 3.018-second measurement gave about 98,904 received bytes/s and 33,402 sent bytes/s. The second reported speeds were 836,810 and 271,704: consistent with **bits/s**, approximately 0.84 and 0.27 Mbit/s. The exact averaging window of these speed fields is not established. Prefer counter deltas when an explicit averaging interval is required; label it accordingly.

A working batch, 172 JSON bytes without whitespace:

```json
[{"show":{"system":{}}},{"show":{"interface":{"name":"PPPoE0","stat":{}}}},{"show":{"internet":{"status":{}}}}]
```

RCI can return HTTP 200 with an error inside a command's `status` array: inspect each result. The documented `show interface rrd` exists, but the attempted nested JSON argument forms were rejected on this device. RRD history is therefore **not part of the verified MVP**. A failed experimental GET with `?name=PPPoE0&stat` was also rejected; use the tested batch shape.

## Login and permissions

On the tested 5.01 firmware:

1. GET `/auth` returns 401, a session cookie, `X-NDM-Realm` and `X-NDM-Challenge`.
2. Compute the hexadecimal MD5 of `username:realm:password`, then the hexadecimal SHA-256 of `challenge + md5_hex`.
3. POST `/auth` with JSON `login` and `password` (the SHA-256 value), retaining the cookie.
4. Continue RCI reads with the session cookie. Authentication succeeded with HTTP 200; the body contained `ndsso` and `method` fields, whose contents are not needed or retained in this report.

Renew once after a session 401, then retry the read once. A refused login must stop the loop until settings change or an explicit retry. Network failures back off. Do not renew authentication every minute if the cookie still works.

A dedicated user with `http` and `readonly` tags is the documented least-privilege candidate. Those tags were not assigned and a non-admin login was not tested, so exact access to all metrics remains to be verified before recommending that account configuration. The supplied administrator account was used only for these reads. Do not ask for SSH, HTTP Proxy, manager or WAN administration privileges unless a verified missing permission requires it.

The protocol is verified only on the owner's **5.01**. The RCI manual documents many reads from 2.x onward, but that does not prove application compatibility. Official 5.2 release notes say HTTP authentication changed and access-token support was added. Do not claim 5.2 compatibility without its actual handshake fixture.

Credentials belong in the app's secret setting. The web page does not read it back; the app can access it. `SECURITY.md` explicitly says appkv is unencrypted. Never log passwords, challenges, cookies or HTTP auth payloads. Keep cookies/session state in volatile scene memory where possible; checkpoint only non-secret readings at a coarse cadence. Local HTTP hides the plaintext password in this handshake but is not encrypted transport. HTTPS must validate its certificate; no silent verification bypass.

## Proposed screens

**Overview**: a quiet 2-column, 3-row dashboard in the existing host style:

| Left | Right |
|---|---|
| Download / Входящий, Mbit/s | Upload / Исходящий, Mbit/s |
| CPU / Процессор, % | RAM / Память, % |
| Devices / Устройства, active count | Connections / Соединения, conntrack count |

Keep the host header. Internet failure gets a short status in the header. A separate **Router / Роутер** page can show uptime, WAN uptime, used/total RAM and the two radio temperatures. CPU temperature is omitted because it was not found. The model/OS belongs on that second page, not above the main readings.

UP/DOWN cycle the two pages, OK refreshes, BACK exits. An optional traffic graph can follow after the basic app: 30 stored samples, no invented history, gaps when the app was not collecting. Do not require it for the first version.

Settings: **Router address / Адрес роутера**, **Username / Пользователь**, **Password / Пароль** (secret), **Refresh / Обновление** (1, 5 or 15 minutes; default 1 minute). Default WAN selection follows `gateway.interface`, with fallback to connected/defaultgw interface metadata. A later manual selector must list actual interfaces instead of asking users to type an internal ID.

## Sampling and resource plan

- One batch per configured interval while visible; no permanent 5-second loop. Background refresh at five minutes or the slower configured interval. OK requests one batch and coalesces pending work.
- Fetch version and interface discovery once per session and after a WAN identity change; avoid refetching all Wi-Fi details every minute. Counts may be in the regular batch and parsed as a stream.
- Show a dash for unknown speed before a second counter sample. Calculate `8 * delta_bytes / delta_seconds` with 64-bit arithmetic and each interface's timestamp. Current router-provided speed is an optional fast first reading, with its different averaging semantics documented.
- Treat reboot, lower uptime, interface replacement, timestamp going backwards, changed overflow marker or decreasing counters as a new baseline; never draw a giant spike. Do not guess a 32-bit wrap when width is unknown. This router's counters are at least 64-bit values.
- Do not sum PPPoE and its Ethernet carrier, or every interface marked global. This router also has VPN interfaces; adding them double-counts traffic. WAN RX means download; LAN/AP directions need their own labels and are not interchangeable.
- Preserve the last good non-secret readings on a failed batch. Mark loss of contact; do not present stale network speed as current. Missing optional fields stay absent rather than zero.
- Use streamed JSON and fixed limits, not a DOM or a whole client table in RAM. The SDK has one 64 KiB WASM page with an 8 KiB stack, 512-byte request bodies, four request headers, 255-byte expanded header values and serialized HTTP requests. The demonstrated batch fits; credentials/cookie bounds need tests.
- Target one request in flight and a small state machine. Remember that response delivery can load a different WASM instance: session/challenge/parser state must survive Perinkpheral OS unloading the application. No background flash write per sample.

## Remaining checks before implementation

1. Implement and preview the two-screen layout against firmware 0.1.51 or later.
2. Verify a user with the documented read-only tags without changing permissions silently.
3. Test malformed/partial responses, auth expiry and denial, missing fields, client-count truncation, reboot/wrap, WAN failover and 32/64-bit counter fixtures.
4. Build and measure actual WASM/code-arena use, then simulator previews with both languages/fonts. The memory figures above are platform limits, not a measured Keenetic app build.

## Primary sources

- Keenetic 5.0 CLI reference, including RCI appendix, `show system`, interface statistics/RRD, hotspot, Internet status and user tags: https://storage.googleapis.com/docs.help.keenetic.com/cli/5.0/en/cli_manual_kn-1912.pdf
- Earlier RCI documentation for comparison, not proof of current login compatibility: https://storage.googleapis.com/docs.help.keenetic.com/cli/4.1/en/cli_manual_kn-2410.pdf
- Official HTTP Proxy API article: https://support.keenetic.com/sprinter/kn-3711/en/55035-using-api-methods-through-the-http-proxy-service.html . Its Digest example is for the proxy service; it is not evidence that local `/auth` uses HTTP Digest.
- Official 5.2 notes on changed HTTP authentication and tokens: https://support.keenetic.com/hero/kn-1012/en/35833-latest-preview-release.html
- Official 5.2 notes on read-only restrictions: https://support.keenetic.com/carrier-dsl/kn-2111/en/6350-latest-preview-release.html
- Actual router responses were checked during the read-only bench session. Credential, cookie and raw client-response files were deleted after probing; only the observations above are retained here.
- Local SDK/security: `sdk/host/net.h`, `firmware/main/poller_net.c`, `SECURITY.md`, `CLAUDE.md`.
