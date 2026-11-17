# The bug-fix release

This is the plan for a bug-fix release of R5 Operating System, after M13, Bluetooth plan 2 and Wi-Fi 6. It covers three kinds of work:

1. Haiku Trac tickets that still apply to R5.
2. Bugs in R5's own code.
3. Haiku coding style, and speed in the code where a measurement shows a gain.

The work is done in runs that go without supervision. One agent takes ticket after ticket from a queue, opens one pull request per ticket, and stops on its own. The owner reviews and merges afterwards.

Written 17 Nov 2026. Nothing in this document is built yet.

## 1. What is there to start from

- `tracker/haiku-open.csv`: open Haiku tickets (id, summary, status, type, priority, component, milestone).
- `tracker/r5-status.tsv`: R5's status per ticket (`open`, `in-progress`, `fixed`, `wontfix`, `not-applicable`). On 17 Nov 2026 it held 3,795 `open`, 8 `in-progress` and 133 `fixed`.
- `tracker/github.tsv`: the GitHub issue (label `haiku`) for each ticket.
- Scripts:
  - `scripts/tracker-set.py <id> <status> --branch --commit --notes`;
  - `scripts/tracker-report.py`, which writes `tracker/STATUS.md`;
  - `scripts/sync-github-issues.py`.
- Tests:
  - the fuzzers `scripts/fuzz-bt.sh`, `fuzz-wifi.sh` and `fuzz-midi2.sh`;
  - the speed measurement `scripts/qemu-m13-measure.py`.
- Build: `scripts/build-locked.sh`.

## 2. Where the code lives decides how it is fixed

| Code in | Fix |
|---|---|
| `haiku/`, unchanged by R5 | Copy the file to `distro/` first, keeping the Haiku header and copyright. Mark the R5 lines as an `SPDX-Snippet`, copyright 2026 R5OS Contributors, `MIT` (`AGENTS.md`, "Overlays and native source"). No new overlay, no `replace_once` |
| `distro/`, R5's own | Fix it in place, MIT |
| `distro/`, a Haiku copy | Fix it in place, inside a new or existing R5 snippet |
| Third-party code (GPL, BSD, ISC, Apache) | The smallest fix, keeping the file's license. Never copy code across licenses |
| `haiku/` code that R5 replaced (for example by a `distro/` copy or another program) | `not-applicable`, with the replacing file named in the notes |

## 3. Classes

| Class | What | Proof needed | In an unattended run |
|---|---|---|---|
| **A** | Applications, kits, servers in userland, the build, documentation | `build-locked.sh` passes, plus a unit test or host test where one can be written | Yes |
| **B** | Kernel, drivers, file systems, boot loader | As A, plus one short QEMU boot with SMAP on (starts, no panic, the one thing that changed), and a manual SMAP review | Yes |
| **C** | Needs real hardware (a GPU, Wi-Fi, Bluetooth, sound, a laptop) | A stick boot | No: the fix may be written, but stays a draft PR marked "needs hardware" |
| **X** | Areas the owner keeps: the GFX9 ring and everything under "GFX9 ring" in `AGENTS.md`, the Settings window layout, MMU, scheduler, and anything that changes an ABI | — | No: only the status `needs-owner`, with what was found |

Not in this release: tickets for architectures R5 does not target (32-bit x86, PowerPC, SPARC, m68k, ARM32), except to keep the tree building.

## 4. The queue

`tracker/bugfix-queue.tsv`, one line per item:

| Column | Meaning |
|---|---|
| `item` | `trac:<id>` for a Haiku ticket, `r5:<area>-<n>` for an R5 finding, `style:<path>`, `perf:<area>-<n>` |
| `class` | A, B, C or X |
| `area` | the component, such as `Tracker` or `app_server` |
| `path` | the main file |
| `prio` | 1 = crash, hang or data loss; 2 = wrong behaviour; 3 = cosmetic; 4 = style or speed |
| `run_status` | `todo`, `working`, `pr`, `not-applicable`, `parked`, `needs-hardware`, `needs-owner` |
| `pr` | the PR number |
| `notes` | one line |

`tracker/r5-status.tsv` stays the authority for Trac tickets. A merged fix sets it to `fixed` with `tracker-set.py` (`AGENTS.md`, "Tracker and changelog"). `run_status` only tracks the run.

## 5. Phases

### Phase 0: triage (unattended, read-only)

- Every `open` Trac ticket, in order of component, with the components R5 ships first:
  - Tracker, Interface Kit, app_server, Kernel, Deskbar, Terminal, MediaPlayer and so on;
  - WebPositive and the Web Kit last.
- For each ticket:
  - Does the code still exist in R5's tree?
  - Does the reported behaviour still follow from it, judged by reading the code?
  - Which class, which path, which priority?
- A ticket whose code is gone, or that R5 already fixed, gets `not-applicable` or `fixed` in `r5-status.tsv`, with the reason in the notes.
- In the same phase, one read-only review per R5 area. Each finding becomes an `r5:` line in the queue:
  - Bluetooth, Wi-Fi, the GPU interface and the compositor;
  - Settings and `libr5ui`;
  - multi-user and Login;
  - zeroconf and networking;
  - Media.
- Output: the queue, and one tracker commit (`tracker-report.py`, `r5-status.tsv`, `STATUS.md`).

### Phase 1: a short trial run

About 2 hours, class A, `prio` 1 and 2 only. The owner reads the PRs afterwards before a long run starts.

### Phase 2: long runs

Up to 6 hours each, class A and B, highest priority first. The run loop is in section 6.

### Phase 3: style

`haiku-format` (or `src/tools/checkstyle/`) on `distro/` files only. One PR per directory, titled "… - style only, no functional change". Never in the same commit as a fix. `haiku/` files are not reformatted.

### Phase 4: speed

- Only where a measurement shows a cost:
  - `scripts/qemu-m13-measure.py`;
  - a profile, or a host benchmark of the function.
- The PR states the number before and after, and how it was measured.
- No change "because it looks faster".

### Phase 5: hardware

All class C items with a written fix go on **one** stick:
- the stick card and a pre-stick check, as for every stick;
- the results go back into the queue.

### Phase 6: the release

Only when the owner says so:
- `distro/VERSION`, `distro/BUILD` and `distro/WHATSNEW`;
- the release notes.

## 6. The unattended run (brief for the agent)

Read `AGENTS.md` first and follow it, then this document. Work in your own worktree from `origin/main`. Build only with `scripts/build-locked.sh`. Do not run git from WSL on a checkout. Write the report to the owner in Dutch, short.

The loop:

1. Take the first `todo` line in `tracker/bugfix-queue.tsv` with class A or B, the highest priority first. Set it to `working`.
2. Read the code and check that the bug still exists.
   - If it does not: set `not-applicable` (or `fixed`) with the reason, and go to 1.
3. Make a branch from `origin/main`: `claude/fix-<item>`. Make the smallest fix, following section 2.
   - Add a test where one can be written: a host test, a fuzz target, or a check in an existing test.
   - No drive-by refactor, no reformat.
4. Prove it:
   - class A: `build-locked.sh`;
   - class B: also one QEMU boot with SMAP on, on its own copy of the image, and a hand review of every user copy (`user_memcpy()`).
   - Fuzz targets: 60 s each.
5. If it passes:
   - add a `CHANGELOG.md` line (problem, change, ticket, limits, how it was tested);
   - push by explicit ref; open one PR to `main`, titled "Area - what the user gets", with Changed / Tested / Not proven / Documentation;
   - write a comment on the GitHub issue (build, limits, how it was tested), and leave the issue open until the merge;
   - set `run_status` to `pr`.
6. If it does not pass within **45 minutes**, or the fix touches a class X area:
   - `parked` or `needs-owner`, with what was found;
   - discard the branch, and go to 1.
7. Every 10 items, and at the end: commit the queue file in its own PR `Tracker - bug-fix run <date>: queue status`.

Never:
- merge, write a stick, or change `distro/VERSION`, `distro/BUILD` or `distro/WHATSNEW`;
- stack PRs; each fix starts from `origin/main`;
- turn off a check, a test or SMAP to make something pass;
- claim a test that did not run.

The run stops:
- after the time limit the owner gives (default 6 hours);
- when no `todo` A or B lines are left;
- or when `build-locked.sh` fails three times in a row for reasons outside the item. Report that.

The final report has one line per PR (number, item, class, what was tested), then the `parked` and `needs-owner` items with one line each, then the time used.

## 7. Review

The owner cannot read 15 PRs line by line after every run. After each run, one review document lists all PRs of that run, with every open finding of earlier rounds. A reviewing agent fills it, and the owner merges in a batch.

## 8. Open choices for the owner

1. **Runs in Cursor or in Claude sessions.** Whether Cursor keeps one agent going for six hours depends on Cursor's own limits, which this plan does not know. The queue makes a run resumable either way: a new run simply takes the next `todo` line.
2. **One PR per ticket, or small batches per component.** Recommendation: one per ticket. A bad fix then blocks nothing else, and a review stays small.
3. **When phase 0 runs.** It is read-only and can run before M13, Bluetooth plan 2 and Wi-Fi 6 are finished. Recommendation: run it then, so the queue is ready when the release starts.
