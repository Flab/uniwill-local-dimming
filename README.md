# uniwill-local-dimming

Minimal Linux kernel module to switch the **local dimming** of mini-LED panels
on Uniwill notebooks, for models the mainline `uniwill-laptop` driver does not
know (and which have no local dimming support there yet).

## Sources

Everything in this project that is not original comes from the sources below.
Each was read on 2026-10-26 and is cited only for what it actually says.

1. **RFC patch 3/3, "platform/x86: uniwill-laptop: Offer support to activate
   local dimming"**, Werner Sembach (TUXEDO), 2026-04-21. Source of the EC
   support register, the WMI GUID, method, function, values and buffer
   layout.
   <https://lists.openwall.net/linux-kernel/2026/04/21/1627>
2. **The same series, thread view**: the cover letter says the third patch
   (local dimming) was not yet tested, and Armin Wolf's reply of 2026-04-21
   rejects `wmi_evaluate_method()` ("this API is deprecated and should not
   be used"). The series is RFC and was not merged as of that page.
   <https://ratatoskr.run/lkml/2026/04/3523267/t>
3. **Mainline `uniwill-laptop` driver, Linux v7.0**, Armin Wolf. Reference
   for the `ECRR` EC read, the DMI-gated registration and the reason for not
   autoloading on the WMI GUID.
   <https://github.com/torvalds/linux/blob/v7.0/drivers/platform/x86/uniwill/uniwill-acpi.c>
   <https://github.com/torvalds/linux/blob/v7.0/drivers/platform/x86/uniwill/uniwill-wmi.c>
4. **Kernel documentation for `uniwill-laptop`**: the driver "relies on a DMI
   table to automatically load on supported devices", has a `force`
   parameter, and documents no local dimming attribute.
   <https://www.kernel.org/doc/html/next/admin-guide/laptops/uniwill-laptop.html>
5. **Uniwill Control Utility**: its README lists a "MiniLED Local Dimming"
   toggle and names the XMG Neo 16 25 and PCSpecialist Recoil 3 16 among the
   machines of this chassis. Evidence that the toggle exists on this
   hardware family.
   <https://github.com/Uniwill-Control-Utility/Uniwill-Control-Utility>

Facts about the test machine come from the machine itself, not from the web:
`/sys/class/dmi/id` (board `X6AR559Y`, vendor `AiStone`, BIOS
`N.1.17PCS12`), the panel EDID (`BOE NE160QDM-NM9`, HDR block with about
1261 nits peak), the ACPI tables decompiled with `iasl` (the `ABBC0F6F`
WMI block maps to `WMBC`, whose method 4 is the one called), and the EC
register read by the module itself.

## Status

**Experimental.** Tested working on one machine only: AiStone `X6AR559Y`
(Uniwill X6 chassis, BIOS `N.1.17PCS12`, BOE `NE160QDM-NM9` mini-LED panel,
Ubuntu, kernel 7.0.0-38). The EC reported local dimming support
(`0x0D4F = 0x01`) and writing `0` removed the halo around a bright cursor on
a dark gray background. The setting also survived a reboot (through the
systemd unit) and a suspend/resume cycle (with the module's restore on
resume). Read this whole file before loading the module.

This module is not affiliated with TUXEDO, PCSpecialist, XMG, SKIKK or the
Uniwill vendor.

## Origin and credits

This is a derivative work of Linux kernel code, so it is GPL-2.0-or-later.

**Taken from other people's work**

- The EC support register (`0x0D4F`, bit 0), the WMI GUID
  `ABBC0F6F-8EA1-11D1-00A0-C90629100000`, method `0x04`, function `0x500`,
  the argument values (`0x0e` on, `0x0d` off) and the input buffer layout
  come from the RFC patch by Werner Sembach (sources 1 and 2). It targets
  the TUXEDO Stellaris 16 Gen7 and was not merged.
- The EC read through the ACPI `ECRR` method, the DMI gating, and the
  decision not to autoload on the WMI GUID follow the mainline
  `uniwill-laptop` driver by Armin Wolf (sources 3 and 4). His review of the
  RFC (the old `wmi_evaluate_method()` is deprecated, source 2) is why this
  module uses `wmidev_invoke_method()`.
- That the toggle exists on this hardware family was known from the Uniwill
  Control Utility (source 5).

**Done here**

- Confirming that the same register and call exist and work on an
  `AiStone X6AR559Y` (the WMI method is `WMBC` in the machine's DSDT, the
  support bit reads `1`), and adding that board to the DMI table.
- A standalone minimal module instead of a patch to the full driver, so it
  can be used on a kernel that does not know the board and does not enable
  any other Uniwill feature.
- Restoring the chosen state after suspend, and the DKMS and systemd
  packaging.

If you are one of the authors above and want a credit changed or the project
removed, please open an issue.

## What it does

- Reads EC register `0x0D4F` (bit 0) once at probe to see whether the
  firmware reports local dimming support. This is a read only.
- Exposes two files under the WMI device
  `/sys/bus/wmi/devices/ABBC0F6F-8EA1-11D1-00A0-C90629100000/`:
  - `local_dimming_supported` (read only): `1` if the EC reports support.
  - `local_dimming` (read/write): write `0` to turn local dimming off, `1`
    to turn it on. Reading returns the last value you wrote, or `unknown`
    (the EC state cannot be read back).
- Re-applies your last choice after resume, as a precaution because the EC
  state is not guaranteed to survive suspend. It was tested only with this
  restore in place: whether the firmware would reset the state by itself on
  this machine was not checked.
- **Writes nothing at load time.** Nothing changes until you write the
  attribute yourself.

It does not load any other Uniwill feature (hotkeys, fans, battery limit,
lightbar, keyboard backlight).

## Safety

- It is a runtime WMI call to the embedded controller, not a BIOS flash.
  The state looks volatile (the firmware resets it after sleep), so a reboot
  should restore the default. This has not been verified on every model.
- The call is made through `wmidev_invoke_method()` with the buffer layout
  of the RFC patch. On a firmware that interprets function `0x500`
  differently, the effect is undefined. Worst case seen in theory: a hang
  that needs a long press of the power button.
- By default the module refuses to load unless DMI matches a whitelisted
  board and refuses writes if the EC does not report support. The `force`
  and `allow_unsupported` parameters override this and are marked unsafe.
- It never autoloads (no `MODULE_DEVICE_TABLE`) because the WMI GUID is
  copied from the Windows driver example by many vendors.

Save your work before the first write.

## Supported boards

Whitelisted in `ld_dmi_table`: `AiStone` / `X6AR559Y`. To add yours, append
an entry with your `sys_vendor` and `board_name`
(`cat /sys/class/dmi/id/{sys_vendor,board_name}`) and send a patch.

The WMI GUID `ABBC0F6F-...` and the ACPI device `\_SB.INOU` must exist.

## Build and use

Needs kernel headers and a compiler (`linux-headers-$(uname -r)`,
`build-essential`). Tested on Ubuntu with kernel 7.0.0-38.

    make
    sudo insmod ./uniwill_local_dimming.ko
    dmesg | tail                                   # shows the EC support bit
    cat /sys/bus/wmi/devices/ABBC0F6F-8EA1-11D1-00A0-C90629100000/local_dimming_supported

If the support bit is 1:

    echo 0 | sudo tee /sys/bus/wmi/devices/ABBC0F6F-8EA1-11D1-00A0-C90629100000/local_dimming   # off
    echo 1 | sudo tee /sys/bus/wmi/devices/ABBC0F6F-8EA1-11D1-00A0-C90629100000/local_dimming   # on

Unload with `sudo rmmod uniwill_local_dimming`.

## Install at boot (DKMS + systemd)

    sudo ./install.sh

This installs the module through DKMS (rebuilt for each new kernel and
signed with the DKMS MOK key when Secure Boot is on, so the key must be
enrolled already) and enables `uniwill-local-dimming.service`, which loads
the module and writes the value in `/etc/default/uniwill-local-dimming`
(`LOCAL_DIMMING=0` is off, `1` is on) at every boot. Remove everything with
`sudo ./uninstall.sh`.

With Secure Boot and no enrolled key, `insmod` of an unsigned module fails
without a clear message: sign the `.ko` first with `sign-file` and your MOK.

## Reporting

Please open an issue with `dmidecode -t 1 -t 2`, the `dmesg` line from the
module, the panel (EDID model) and whether the halo around a bright cursor
on a dark gray background changed.

## License

GPL-2.0-or-later, as it is derived from kernel code. See `LICENSE`.
