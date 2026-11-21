# Documentation

Start with the [device guide](https://perinkpheral.dev/start) for assembly,
[USB flashing](https://perinkpheral.dev/flash) and
[Wi-Fi and application setup](https://perinkpheral.dev/setup).
The site's source lives in [perinkpheral-dev-portal](https://github.com/Perinkpheral/perinkpheral-dev-portal).

## Source references

| Topic | Maintained source |
|---|---|
| Architecture, constraints and current state | [CLAUDE.md](../CLAUDE.md) |
| Build, flash and bench recipes | [AGENTS.md](../AGENTS.md) |
| Application development guide | [English HTML](guide/source/guide.html), [Russian translation](guide/source/guide.ru.txt) |
| Perinkpheral OS ABI and compatibility | [host_api.h](../sdk/host_api.h), [frozen ABI notes](../sdk/abi/README.md) |
| Device HTTP API, including recovery | [OpenAPI](api/openapi.yaml) |
| Security model | [SECURITY.md](../SECURITY.md) |
| Test coverage and hardware results | [Manual acceptance](testing/acceptance.md), [stability testing](testing/stability.md), [load testing](testing/load.md), [20 November acceptance](testing/acceptance-2026-11-20.md) |
| Jersey replacement proposal | [Font comparison and pending panel trial](testing/jersey.md) |
| Panel and board findings | [Hardware bench notes](../hardware/README.md) |
| Enclosure source and printable files | [Rev 1](../hardware/step/rev1/), [exporter](../tools/case/README.md) |
| Simulator behavior and limits | [Simulator](../tools/simulator/README.md) |
| Release history | [CHANGES.md](../CHANGES.md) |

## Generated and local material

Guide PDFs are built from the sources above and attached to
[firmware releases](https://github.com/Perinkpheral/perinkpheral-os/releases/latest).
They are not committed. Fonts, icon tables, guide screenshots and frozen ABI
fixtures are deliberate tracked inputs or outputs; see [the checking recipes](../AGENTS.md).
Do not remove them as build debris.

Build products belong under `build/` or a tool's ignored build directory.
Raw bench captures and flash backups stay local under `build/`; retained historical
working notes and completed experiment captures may live in `build/archive/`.
Flash backups can contain credentials and are not documentation to publish.
Original case renders live in ignored `hardware/render/`; the portal keeps the
optimized images it serves. Current findings belong in the references above,
not in a second roadmap or changelog at the repository root.

The [repository overview](../README.md#related-repositories) links the applications,
IDE plugin, Home Assistant integration and site.
