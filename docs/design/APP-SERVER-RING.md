# Design: shared-memory link between applications and app_server

Design of 26 Nov 2026, item 2 of phase 3 of the performance audit. The owner's choices are under "Choices made" and "Decided later". Built on 27 Nov 2026; how it was built is under "As built".

## Why

Every Haiku application talks to app_server through `BPrivate::LinkSender` and `LinkReceiver` (libbe, `src/kits/app`). LinkSender collects commands in a buffer, flushes it with `write_port()`, and app_server's `MessageLooper` takes it with `read_port()`.

Each flush costs:
- two system calls;
- two copies through the kernel (into the port and back out);
- a wake-up of the other thread through the scheduler.

A wait for an answer adds a second round of the same: `FlushWithReply()` is used 24 times in `BView` alone (string width, `Sync()`, view bounds and the like).

The goal is the same drawing with fewer system calls, fewer copies and less waiting, for every application and without recompiling. That means a smoother feel and less CPU and battery use, weighted equally (owner).

## Choices made

| Point | Choice |
|---|---|
| Which applications | All of them, automatically, through libbe. No change to the libbe ABI or to any public header. |
| Direction | Both ways. Commands go from app to app_server; replies go from app_server to app. |
| Safety | app_server copies each batch out of the ring into its own memory first, and only then parses it, as today. |
| Measure first | No. We build directly. The test still counts how many port messages were saved, so that what the change does is reported honestly. |
| Off switch | A switch in Settings. Ports always remain as the fallback. |
| Builder | A visible Claude session. Claude also reviews. |

## How it works

1. **One ring pair per link.** There is one for the application link (`BApplication`) and one for each window link (`BWindow`). Each pair is an area of, for example, 64 KiB with two single-producer single-consumer rings: app → server and server → app. A link is used by one thread at a time (the window's looper thread, under its lock), so a single writer is already guaranteed.
2. **Writing.** `LinkSender::Flush()` writes the batch into the ring instead of `write_port()`. A batch that does not fit, or a message bigger than the ring, takes today's route (port, or the existing area for very large messages). Order is kept, because the ring falls back to the port only after it has been drained up to that point.
3. **Waking.** The reader is only woken when the ring goes from empty to non-empty. That wake-up is a small port message on the existing port, so app_server's `MessageLooper` keeps waiting on one port, as today. During a stream of drawing commands, app_server keeps reading without any system call.
4. **Replies.** `FlushWithReply()` writes the request into the ring and waits for the answer in the server → app ring. It first waits briefly (a few microseconds) without sleeping, then on the reply port as today. On a multi-core machine many short replies come back during that brief wait.
5. **Events stay as they are.** Mouse, keyboard and other `BMessage`s to a window go through the `BLooper` port, which is part of the messaging system, not of the link. They are out of scope.

## Safety

The area is writable by the application, so app_server reads nothing in place:
- it takes the read and write positions as numbers and checks them (bounded by the ring size);
- it copies the batch into its own buffer, and only then parses it with the existing `LinkReceiver` code.

A broken or hostile application can therefore not crash or hang app_server. The worst case is that its own window draws nonsense, or that app_server closes the link and that application falls back to ports.

**The area must only be shared with the one application.** In Haiku, an area that another team may clone (`B_CLONEABLE_AREA`) can be cloned by *any* team. A second application could then inject drawing commands into someone else's window.

Proposal: a small, additive R5 kernel call with which the owner of an area maps it into exactly one other team (MIT, in `distro/kernel`, documented in `docs/LIBR5.md` or the kernel notes). The ring area is never cloneable.

## Off and fallback

- The **Settings switch** (open point 1) writes a setting that app_server reads. It takes effect for applications started after the change; already running applications keep their link until they close.
- **Safe mode** always turns the ring off.
- **The port route never goes away.** It is the fallback whenever the ring cannot be set up, for an application with its own old libbe, and when app_server closes a link that broke a rule.

## Compatibility

- No public API changes, and no change to the libbe ABI. `LinkSender` and `LinkReceiver` are private (`headers/private/app`).
- An application that uses `BPrivate::PortLink` directly keeps the port route.
- Changed Haiku files are copied into `distro/` with SPDX-Snippet tags; the new code is MIT.
- Applications with their own old libbe (none are known in R5) keep ports.

## Tests (done when)

- `scripts/build-locked.sh` passes.
- One QEMU boot reaches the desktop with the ring on. It shows on screen:
  - Tracker, Deskbar and Settings;
  - Aomi scrolling;
  - a few native applications drawing correctly.
- The syslog has no app_server crash, and shows a counter of port messages saved.
- With the switch off, the same boot runs on ports.
- A test application writes garbage into its own ring, and app_server stays up.
- On the 5625U stick: Aomi and scrolling feel, together with the other performance work.

## Decided later (26 Nov, owner)

1. **Switch**: a new Settings page **Performance** under System. It is an add-on (`add-ons/Settings`, `Addon` rule) and administrator-locked. Its place in the list follows the alphabetical rule; the other pages, their order and the five fixed descriptions are not touched. It can later hold the other speed switches.
2. **Kernel call**: yes. A small, additive R5 call with which the owner of an area maps it into exactly one other team. The ring area is never cloneable.
3. **Default**: on in the dev build, off in Safe mode, off with the switch.

## As built (27 Nov 2026)

Code: `distro/desktop/src/kits/app/LinkRing.cpp` and `headers/private/app/LinkRing.h` (new, MIT), the R5 snippets in `LinkSender`, `LinkReceiver`, `BApplication::_ConnectToServer()` and `BWindow`, and on the app_server side `MessageLooper::_SetUpLinkRing()` with `AS_R5_SET_UP_LINK_RING` in `ServerApp` and `ServerWindow`. The kernel call is `_kern_r5_share_area()` (`docs/KERNEL.md`, "One-team areas"). The switch is the Performance page in Settings (`distro/settings/PerformanceModule.cpp`).

- **Set-up.** After `AS_CREATE_APP` (application link) and after each `AS_CREATE_WINDOW` / `AS_CREATE_OFFSCREEN_WINDOW` (window link), libbe sends `AS_R5_SET_UP_LINK_RING`. app_server answers through the port: an error (switch off, Safe mode, the reply port not owned by the link's team), or the id of the ring area in the client's team and the two sizes. Only then does it put the ring on its side of the link. One extra round trip per window.
- **Area.** 52 KiB: one page of indices and flags, 32 KiB application to app_server, 16 KiB app_server to application. app_server creates it, without `B_CLONEABLE_AREA`, and maps it only into the team that owns the client's reply port (the kernel names that owner).
- **Records.** A `Flush()` is one record: size, kind, the batch, padded to 8 bytes. Positions are free-running 32-bit counters; the ring sizes are powers of two. One marker always stays free.
- **Waking.** The reader sets a flag before it waits on its port and checks the ring again; the writer, after publishing, sends a wake (`'_PTW'`, no data, never waiting) only when it finds that flag set (Dekker order, full barriers). During a stream nobody sleeps, so nothing is sent.
- **Order.** A batch that does not fit goes to the port with a 12-byte prefix (`'r5RF'`), and then a marker goes into the ring. The reader takes ring records in order and, at a marker, the one prefixed port batch. Port batches without the prefix (app_server's own messages, a window's `AS_CREATE_WINDOW` on the application port) are taken as before, after the ring records that came before them.
- **app_server never waits for a client.** Its replies go into the ring only after the client has written into the ring once, and only when they fit; otherwise the port. If not even a marker fits, it closes that direction; the client takes what is still there and then reads the port. The client waits, with short sleeps, when its ring is full, as `write_port()` waits on a full port.
- **Safety.** app_server keeps its own read position, reads each record header once, checks it (kind, size up to 64 KiB, within what was written), and copies the batch into its own buffer before `LinkReceiver` parses it. A record that makes no sense closes the direction (syslog line "link ring closed after bad data"); the program's later batches come through the port. A batch whose message sizes do not add up costs only that batch (code `B_BAD_DATA`, which no dispatcher knows) instead of ending the window. `HasMessages()` reads port messages ahead, so `ServerWindow`'s inner loop, which holds the Desktop lock, never waits on a port after it said yes.
- **Replies.** `FlushWithReply()` waits up to 20 µs without sleeping (several processors only), then on the reply port as before.
- **Fallback.** The port route stays: without a ring, for a link that was pointed at another port (an app_server restart), after a fault, and for programs that use `PortLink` directly.
- **Switch.** `app_server_ring on|off` in the administrator's `~/config/settings/kernel/drivers/performance`, read again when the file changes, for links set up after it. Without the line: on in a `-dev` build (`R5_OS_VERSION`), off in a release. Safe mode: always off.
- **Counter.** app_server writes at most every 10 s, when a link closes or after 16384 records, one line to the syslog and the serial line: rings open and set up, batches and wakes in each direction, ordered port batches, closed rings, and "port messages saved" = batches through the ring minus wakes.
- **Not in scope.** `BMessage` events to a `BLooper` port, the Desktop link, and `PortLink` objects that programs create themselves.
