# Load testing

What the device does under more work than a home gives it, after the endurance run of
[stability.md](stability.md) has passed on the same image. Endurance asks whether it
lasts; this asks where it bends and whether it comes back.

Nothing here is automated: `tools/cli` has no workload runner, and under the feature
freeze of 2026-11-20 none is added to the product. The scenarios use the existing
commands and a shell loop on the bench computer.

## Builds

Presses can be injected only into a development build (`/api/dev`, see stability.md).
A development build differs from the release in size and timing, so:

- scenarios without injected presses (L1, L2, L4, L5) run on the **release** image of
  the candidate;
- L3 runs on the development build of the same commit, and its result is about
  navigation and memory, not about the release image's timing.

Run on a bench device. The owner's shelf device is used only with the owner's agreement.

## What is measured

Before, during and after every scenario, once a minute, from `/api/status`:
`free_memory`, `least_free_memory`, `largest_free_block`, `memory_refusals`,
`failed_allocations`, `reset_reason`, `last_panic`, `uptime_s`, storage use.
`tools/perinkpheral soak` collects them and the log; give each scenario its own
directory and write the scenario's name in `phase.txt`.

Return to the same quiet state - the menu, no request in flight - for five minutes
before and after, and compare the two.

## Scenarios

| # | Load | How | Duration |
|---|---|---|---|
| L1 | Every network application installed at once, each at its shortest poll interval | Install the whole catalog; where an application has a refresh setting, set its shortest | 6 h |
| L2 | The device page under requests | Four parallel loops on `/api/status`, `/api/apps`, `/api/log` and `/` from the bench computer, while L1's applications poll | 1 h |
| L3 | Navigation without rest | A loop of `tools/perinkpheral press` through the menu and every application, a press every two seconds, long BACK every tenth | 2 h |
| L4 | Storage at its limits | Install until the capacity bar is full, remove and install again twenty times; an application that fills its store and files | 1 h |
| L5 | A bad network | L1 with the access point switched off for 1, 10 and 30 minutes, and with DNS answering nothing | 3 h |
| L6 | Everything | L1, L2 and L5's outages together | 2 h |

L2 and L5 exercise what a home network does by accident; they are not attacks, and
[SECURITY.md](../../SECURITY.md) promises no defence against a hostile home network.

## Failure

The failures of stability.md, and:

- the page stops answering for more than a minute while the device still runs;
- a refused request that is not reported to its application as one of `HOST_FAILED_*`;
- quiet-state `free_memory` or `largest_free_block` lower after a scenario than before
  it by more than the measurement's own spread, taken from two quiet readings;
- an application that shows old data as current after the network returns;
- an installation left half done after L4.

Slower is not a failure: one request at a time is the design. Record the longest wait
between a press and its frame in L3, and the longest delay of a poll in L1 and L6.

## Record

A dated file beside this one: the image ID, the application set, each scenario's
readings before and after, the worst values during it, every reset or refusal with
its log lines, and what was changed on the device by hand.

No run of this plan is recorded yet.
