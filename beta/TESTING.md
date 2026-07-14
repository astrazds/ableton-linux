# Ableton Wine beta test plan

(note: in progress work)

## Goal

Test the same Ableton Wine build on different x86-64 Linux systems and record what works.

A build passes on a system only when a tester can:

1. start with an empty Wine folder at `~/.wine-ableton`;
2. follow the published instructions without extra commands;
3. install, authorise and use Ableton Live 12;
4. complete the tests assigned to their machine; and
5. produce a useful test report.

If a tester needs an undocumented fix, that step has failed. Add the fix to the package or instructions, publish a new build and test it again.

## Systems needed

The beta needs a useful mix of systems:

- Ubuntu or Mint, Fedora, Arch and Debian;
- GNOME and KDE;
- X11 and Wayland with XWayland;
- AMD, Intel and NVIDIA graphics;
- 100%, 125%, 150% and 200% display scaling;
- built-in and external audio devices; and
- at least one MIDI controller.

One machine can cover several items. Record the exact distribution, kernel, desktop, session, graphics driver, display scale, audio device and MIDI devices for every test. Passing on one distribution does not prove that every related distribution works.

Results from NixOS, Sway, Hyprland, COSMIC and unusual audio setups are useful, but list them separately until they complete the same tests.

## Test rounds

| Round | Description | Done |
| - | - | - |
| Initialisation | prepare a numbered build and test it with an empty Wine folder. | Done |
| Multi-user pilot | two other people install that build and complete the basic tests. | In progress |
| Ecosystem test | test the build across the systems and devices listed above. | - |
| Pre-release test | repeat full tests on new and existing linux systems, minimal intervention or diagnostics needed | - |

Increment the build number in this repository whenever the dev project itself changes.

## Pre-test preparation

- Back up Ableton projects and any existing Wine folder.
- Use a physical x86-64 Linux machine.
- Use an empty Wine folder or a new user account.
- Connect the audio, MIDI and display hardware being tested.
- Record the build number before changing anything.
- Keep the system configuration unchanged until the report is saved.

Stop testing if a step has extremely weird results that could damage a project, generate unsafe audio output or puts private data in a report.

## Tests

Use the [quick start](README.md) for the automated tests. Then run the checks below. Only skip a check when the required software or hardware is not available.

| ID | Check | Pass result |
| --- | --- | --- |
| 01 | Install | Patched Wine and Live install using the published commands. Live opens again after authorisation. |
| 02 | Launch | Live starts from the launcher five times and closes cleanly each time. |
| 03 | Projects | The demo set and a copy of an older set open and play. Edit, undo, redo, save, close and reopen the copy. |
| 04 | Recovery | Force-close Live while using a disposable set. Live recovers the set and the next normal launch works. |
| 05 | Windows | Resize, maximise, restore and use full screen. Menus, text input and plug-in windows remain usable. |
| 06 | Files | Open, Save As, folder selection, Cancel and audio export all work. Reopen the exported file. |
| 07 | Plug-ins | Scan the agreed VST3 plug-ins, open their windows, save a set containing them and reopen it. |
| 08 | Max for Live | Open the agreed devices, change settings, save the set and reopen it. |
| 09 | Audio | Select WineASIO, play for ten minutes at 48 kHz and 256 frames, then record and play audio. |
| 10 | MIDI | Test notes, controls and output. Unplug and reconnect the controller while Live is open. |
| 11 | Stability | Use the demo set, plug-ins and controls for 30 minutes without a crash, hang or lost device. |
| 12 | Update | Install a newer build, switch back to the previous build, then remove and reinstall the package. The Wine folder and Live settings remain. |
| 13 | Report | The session report names the build and system, contains the results and contains no private files or values. |

### Audio tests

Every audio test machine runs 48 kHz at 256 frames. We also want to test 44.1, 48 and 96 kHz at 128, 256 and 512 frames on suitable machines.

The session collector records the active rate, buffer size, device, channel
layout and audio dropouts (`xruns`). Reviewers determine the result from the
report; testers are not asked to type up a hardware inventory.

Where available, at least one external interface must test eight or more channels. Test device reconnection at a safe monitoring level.

Slow hardware may glitch at demanding settings. A crash, hang, lost device or wrong channel order is always a failure.

## Results and issues

The test runner uses these results:

| Term | Meaning |
|-|-|
| `PASS` | the check worked |
| `FAIL` | the check did not work |
| `WARN` | the setup may need attention |
| `REVIEW` | a person must decide |
| `REGRESS` | this worked before, and now doesn't |
| `SKIP` | the check could not run for some reason |
| `INFO` | details were recorded without a pass or fail result |

Planned: the test suite will generate issues and post them to https://repos.parare.al/ automatically. Today the script saves reports locally and you file issues by hand (see [tester-kit/README.md](tester-kit/README.md)).

Each new issue includes:

- build number;
- test ID;
- exact system details;
- steps that reproduce the problem;
- expected and actual result;
- whether it happens every time; and
- the reviewed session report or the smallest useful log.

Review every report before sharing it. Do not post Ableton installers, authorisation files, projects, samples, recordings, account details or plug-in credentials.

## Release check

A build is ready when:

- a fresh install passes on every system listed in the release notes;
- install, launch, save, audio, reporting, update and removal all pass;
- each reported failure is fixed and retested, or stated plainly in the release notes;
- the release notes list the exact tested systems and devices; and
- the published files are the same files that were tested.

---

Updates? Questions? Email: [cade@parare.al](mailto:cade@parare.al)
