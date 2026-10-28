# Practice icon for AppLoad

These files add a real **Practice** entry to the community AppLoad launcher.
They do not add a notebook tile or claim native `.desktop` support. Pinned third-party launcher files are installed for the current boot on the test tablet; no boot startup was configured. Icon: editable monochrome SVG,
with a 256 × 256 PNG rendered using librsvg. The manifest follows upstream
AppLoad's external-application format; it starts a dispatcher rather than our
Qt app directly.

## Candidate compatibility and pinned upstream packages

Current tablet is ARMv7 reMarkable 2, firmware **3.28.0.172**; staged update is
**3.29.0.153**. AppLoad's 3.28 support was merged upstream September 7, 2026,
before release 0.6.0. That PR's direct reported test used Paper Pro Move
3.28.0.157, not this exact rm2. RemarkableAnimator's author reports AppLoad
operation on rm2 3.29.0.153. Neither establishes this device/app integration.
Do not update firmware simply to install this icon; our pen/refresh path also
requires firmware revalidation.

Use ARM32, not AArch64:

| Package | Pinned release | SHA-256 |
| --- | --- | --- |
| [XOVI/QRR bundle](https://github.com/asivery/rm-xovi-extensions/releases/download/v19-23052026/xovi-arm32.tar.gz) | v19-23052026 | `9aa00537ad41e9be0c3151992bfc25106465318cf5bb4c41cf59b3ddd4866377` |
| [AppLoad](https://github.com/asivery/rm-appload/releases/download/v0.6.0/appload-arm32.tar.gz) | v0.6.0 | `25aa54735742ea63c0a043cba5496987dd4ca742d8b3fc26a21b7368372bf44f` |

Both downloads were hashed locally and matched GitHub release API digests.
Inspected copies are in `/tmp/noah-launcher-research/` on the original host;
these temporary files are not a permanent dependency. The bundle contains
XOVI, qt-resource-rebuilder and setup scripts. AppLoad contains `appload.so`
and two framebuffer shims. Do not copy shims into `extensions.d/`: only
`appload.so` is a XOVI extension. Our dispatcher does not use QTFB shims.

Primary sources:
- [Upstream AppLoad external manifests](https://github.com/asivery/rm-appload#external-applications)
- [Merged 3.28 support](https://github.com/asivery/rm-appload/pull/59)
- [rm2 3.29.0.153 application evidence](https://github.com/dav01d/RemarkableAnimator)
- [XOVI extension setup and automatic-start warning](https://github.com/asivery/rm-xovi-extensions)

## Installation sequence for the operator

Do this initially over USB with backups and a working recovery connection. Do
not run unattended remote installation pipelines. Review the pinned archive
scripts first; inspect existing XOVI/AppLoad files and service drop-ins before
copying anything. Preserve any pre-existing installation rather than overwriting
it. Exact-version compatibility is still an installation gate.

1. Download/check the two pinned archives on the computer. Extract to staging,
   inspect file names and ARM architecture, and back up existing tablet settings
   and affected files. Extract the bundle's `xovi/` to `/home/root/xovi/` only
   after checking for conflicts. Put AppLoad's `appload.so` in
   `/home/root/xovi/extensions.d/`. No credential changes are required.
2. Build the exact tablet firmware's resource hashtable with upstream
   `/home/root/xovi/rebuild_hashtable`. This is interactive and starts/stops
   Xochitl. **Do not invoke it unguarded on this device:** stopping Xochitl
   previously segfaulted and invoked its reboot handler. Apply the project's
   recovery-aware temporary masking of `remarkable-fail.service`, record whether
   a mask existed first, and restore only changes made by this operation.
   Confirm successful hashtable generation and leave stock UI recoverable.
3. Transfer `tablet-launcher/noah-practice/` to
   `/home/root/xovi/exthome/appload/noah-practice/`. Keep its manifest, `launch.sh`
   and `icon.png` together. `icon.svg` is source and can remain alongside them.
   Existing `/home/root/run-rm2-practice.sh`, `/home/root/run-learning.sh`, app,
   private runtime and protected device credential must already exist.
4. Activate **for this boot only** using `/home/root/xovi/start`, again inside
   the same safe Xochitl/reboot-handler recovery procedure. Verify stock UI,
   AppLoad sidebar entry, and the Practice icon before launching practice.
   Inspect private logs for missing hash/QML symbols; on failure restore stock
   UI and do not continue with mismatched hooks.
5. Tap Practice. The dispatcher calls detached `systemd-run --no-block` with
   the fixed `noah-practice` unit. The application can then stop Xochitl without
   being killed with AppLoad. Repeated taps do not start a second active unit.
   Existing wrapper cleanup restores Xochitl when Exit is tapped.

The dispatcher intentionally does not load credentials, change core launchers,
patch stock binaries, configure boot startup, or restart XOVI itself.

## Why the icon should return after Exit

The inspected pinned `xovi/start` mounts a **tmpfs** on
`/etc/systemd/system/xochitl.service.d`, writes the XOVI service environment
there, reloads systemd and restarts Xochitl. That mount survives ordinary
stop/start/restart in the same boot. Therefore the base wrapper's cleanup
`systemctl start xochitl` should restore AppLoad too; this needs a physical
launch/Exit test. Do not call `xovi/start` from the wrapper's cleanup: that would
add another unguarded restart and overwrite runtime service state.

A **full reboot removes this tmpfs** and returns to stock Xochitl. Upstream
explicitly warns against automatically starting XOVI on boot. Accordingly this
candidate provides on-tablet icon launching after one manual activation for the
current boot, but **does not yet meet launch-from-icon after every reboot**.
That needs a separately reviewed guarded persistence design or another supported
launcher; do not silently enable it. Firmware updates require revalidation and
new hashtable generation before reactivation.

## Verification and reversal

Local checks: shell syntax, manifest JSON and PNG render pass. Guarded activation on the exact3.28.0.172 rm2 generated20250resource-map entries; xochitl logs confirm AppLoad main UI hooks loaded, and the temporary failure-handler mask was removed. Physical icon-tap/Exit confirmation remains pending.

Before calling this ready, physically verify: stock UI and AppLoad remain
usable; one Practice tap launches; a second tap cannot duplicate the app;
correct pen coordinates/refresh; authenticated HTTPS; Exit restores stock UI
and the icon; Stop/Exit preserve history; reboot restores stock UI; no reboot
loop. A controlled fallback over USB must work throughout.

For the icon alone, stop practice, then remove only the installed
`noah-practice` app directory after backing it up; it does not alter notebooks.
For the temporary XOVI session, upstream `xovi/stock` reverses its tmpfs service
mount and restarts services; inspect it first and use the same recovery-aware
mask procedure. A full reboot also removes the runtime hook. Preserve any
pre-existing XOVI installation, service overrides and masks. Never blindly
remove an existing `/etc/systemd/system/xochitl.service.d` directory or other
applications. Check that `xochitl` is active and recovery-mask state matches
its original state afterward.

## Reviewed reactivation helper

`activate-rm2-3.28.sh` contains the guarded activation used for this installation, with an added exact-firmware check before any mutation. Copy it to the tablet and run with Bash only after reviewing the pinned installation and confirming the active version. It builds a missing resource map, temporarily masks only the reboot-on-failure handler it owns, rolls back to stock on activation failure, and removes its temporary mask. It refuses activation after a firmware update; revalidate instead of bypassing the guard. Do not install it as a boot service.
