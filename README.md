# Modified OEFWCOM Transceiver Firmware for Radtel RT-890 | Ruyage UV58Plus | iradio UV5118Plus

![20240311_214152](https://github.com/M7OCM/890/assets/128899149/c0b0f547-ba98-401b-bdcd-dc0b1fd48704)

## Latest
Please read, since v2.0.8 the auto functionality has been removed this is now developing into the advanced user category. Subsequently, this radio now requires experimentation and time to learn - if thats not for you, there are many more AM Fix firmwares in the archive to play with, I recommend v2.0.0

Please don't expect v2.1.3 to work like before, AGC Modes need to be finely tuned for best AM reception. For example don't use A3 (Auto), unless for experimental research, use a Fixed mode, either F2 or F1 and adjust values (read on for additional info further down the page). FM will work as before and Auto AGC does work (see pic below). Fine tuning AGC FM will also have benefits. SATCOM RX is vastly improved, especially when coupled with a cheap wideband 20dB pre amplifier and a resonant antenna.

![20240314_163417](https://github.com/M7OCM/890/assets/128899149/ca0daa74-d504-4aab-a2ab-f0317dcef602)

A clear illustration of AGC Mode A3 (auto) working. Drops one level to A2 and reduces LNA 7 to 3 and PGA 6 to 3 as can be seen. Not effective on AM though **F2** and all 3 values for each reg is actually a decent setting for airband I find.

The default keys I changed are specific to using this firmware. Priority given to keys most used. See default list.

For airband AGC Mode testing I use a Yaesu SRA-20A FTA series BNC helical antenna and a Abbree "AR" whip (SMA). The "AR-771" is identical (!).

For other bands I use a mix of dedicated antennae and telecopics tuned for specific frequency ranges. All tested with a VNA for performance guarantees.

**Essential update v2.1.3 re released - fixed search function not detecting signals, speed reduced. Please download latest.**

~~v2.1.3 released 13032024~~ Xawen's amazing Tunable Squelch System. Fine tune the Squelch Level via RSSI, Noise, Glitch values to suit your radio setup - a truly remarkable addition. Back up SPI before use. If in doubt leave defaults: Squelch RSSI 94, Squelch Noise 68 and Squelch Glitch 17. Adjust values with caution - squelch may get stuck open or not open at all if used aggressively! Its unlikely that your "normal" squelch level will be the same. If you factor in the reg edits, tunable squelch, frequency band, bandwidth and antenna there are a lot of things at play and a typical "Squelch 1 user" may well end up on 4 or 5, but drop lower on other bands.

There are no instructions on how to use it, as there is no BK4819 modded squelch manual! Its trial, error and patience. Personaly I'd suggest starting at Squelch Level 1, as that's the base setting with offsets advancing original level values thereafter. Gradually increase or decrease RSSI and quantify, before progressing to Noise and Glitch. I've added some new Key Action Dialogs for FM Radio and Bandwidth. Credit: OEFWCOM

![20240304_093404](https://github.com/M7OCM/890/assets/128899149/2bc95e04-97d0-4fbc-9ea4-45385575615f)
As previously mentioned I'm hopeful that very soon if not now (!) this firmware will be called final and complete. I never wanted an ongoing beta version as that makes it less useable from my point of view. In addition, the fw requires more rigorous testing as, inevitably, bugs tend to creep in when something 'major' gets changed 😂

I've jumped the gun recently calling it finished. Lets just say there could be a few minor revisions coming. AGC Mode/Reg Editor is a revelation - its handed full control to the user and eliminates the previous gain issues that plagued AM. Likewise Xawen's Tunable Squelch provides the best solution to squelch not opening with weaker signals.

I'm more than satisfied with how the radio works - its never going to be perfect, but for the price its a solid performer.

The source files contain all the changes I've made. With that in mind, and as this is 100% Open Source Firmware, if any developers/fettlers want to continue where I left (leave) off, add new features or use anything in their own revision, please do. Just change the Version, Author info (keep OEFWCOM) to reflect that and the Community behind this project. Thanks.

![20240302_152730](https://github.com/M7OCM/890/assets/128899149/c89a7577-7597-4911-934a-d8ebfddd10e9)

![20240303_143115](https://github.com/M7OCM/890/assets/128899149/c366bd9f-45ab-4b57-bbd9-9109193a0063)

Personal thanks to Reppad and Xawen for the coding tips and all the fixes 👍

# Instructions

**Default Keys v2.1.3**

- Side Key [1SP] Monitor
- Side Key [1LP] Freq Detect
- Side Key [2SP] Scan/Advance Scan List/Scan Stop (LP)
- Side Key [2LP] Spectrum

SP = Short Press; LP = Long Press

The following keypad keys are all LP

- [1] Squelch
- [2] Modulation
- [3] Bandwidth
- [4] TX Power
- [5] AGC Mode
- [6] Dual Standby
- [7] Repeater Mode
- [8] Reg Editor
- [9] Scan List Add/Remove
- [0] FMB
- [*] Edit TX Freq
- [#] Lock
- [MENU] TX CTCSS/DCS
- [EXIT] Single/Dual Display

**Spectrum**

Start by mapping a key (side key or keypad) to the Spectrum action using the main menu.

- [Up] Increase frequency range
- [Down] Decrease frequency range
- [1] Change scan step (16, 32, 64 or 128)
- [3] Change modulation FM, AM or SB (SSB)
- [4] Change step size (0.25k - 50k)
- [5] Switch spectrum modes (toggle 1-4)
- [6] Increase squelch level
- [7] Hold/Search (in hold, use up/down to adjust main frequency - useful to avoid RFI)
- [9] Decrease squelch level
- [0] Toggle filter (X = unfiltered, F = filtered)
- [*] Change scan delay (0 - 10ms)
- [#] Toggle bandwidth (25.0K = wide, 12.5K = narrow)
- [MENU] Jump to VFO mode with current frequency and settings (to allow TX)
- [EXIT] Return to main

**Register Editor**

Start by mapping a key (side key or keypad) to the Reg Editor action using the main menu. Register Editor will launch, showing the current register values. The register currently being edited will display in large font. Please read further down for additional information about this feature.

Note: as the squelch has been calibrated for additional sensitivity it is recommended to fine tune BW and WEAK (WK) values to match squelch settings to ensure the weakest signals break squelch. Likewise reduce BW and WK if the squelch opens too frequently (or continuously) and raise the squelch threshold. Its a fine balancing act, but with hands on experience it soon becomes second nature.

- [Up] Move editor to next register
- [Down] Move to previous register
- [1] Change AGC Mode (A3, F3, F2, F1, F0, F7, F6, F5, F4); F2, F1 are good choices for airband
- [2] Decrease value of current register's setting by 1
- [3] Increase value of current register's setting by 1
- [EXIT] Return to main

**Scan lists and Scanning**

There are 8 scanlists, plus scan all.

The current channel can be added to any scanlist using the Ch In List # menus.

The scanlist to be used can be selected in the List To Scan menu.

To ignore scanlists and scan all channels, select * in the List To Scan menu.

To add/remove current channel to/from current scanlist, use the Toggle SList shortcut which is default Key 9 (v2.1.1k+).

To start scanning, press a key mapped to the Freq scanner shortcut (default: short press on side key 2).

When scanning is in progress, use side key 2 short press to change the scan list. This action will move to the next non-empty scanlist, or switch to scan all mode if all subsequent lists are empty.

To change the direction of current scan, use the up/down keys.

To force the scan to resume when the scanner stops on a signal, use the up/down keys.

Press any key other than Freq scanner to stop scanning.

Alternatively use Chirp to store scan list memory (load my modified Python module). There needs to be at least 2 frequencies per scan list but no max limit. Duplicate frequencies can be added to more than one list.

The Chirp driver is a WIP. 8.333kHz channels can be entered and saved as can any frequency in the range 10MHz to 1.3GHz. All modulation modes. Scan Lists can be modified. Border colour can be changed and basic radio functions adjusted. Not all key actions save however. A key reset after upload fixes that. I reccomend making copies of data files on a regular basis just in case a file becomes corrupted which may happen when switching between different firmware versions (latest always works).

**FM Broadcast (FMB)**

Default [0/FM], toggle FMB On/FMB Idle. The 4 digit FM frequency now appears in the upper left part of the status bar. It works the same as stock just without the garbage graphics. Turn on FM Standby via menu, and you can listen to FMB while scanning/searching. When a signal is present the FM radio will mute, then continue until another signal appears - pretty cool!

To clear the idle frequency, press [0/FM] or [PTT] then [MENU].

**Status Bar**

(l-r) Padlock = Lock, FMB, V = VOX, Scan List #, D = Dual Standby/S = Single RX, Repeater Mode "-" = No RX Subtone, "=" = Monitor Input, TX Tone A = Roger Beep A, B = Roger Beep B, I = Send FSK ID, Battery Icon = Cell charge status.

![chirp-border](https://github.com/M7OCM/890/assets/128899149/2bd92a75-c296-45d2-ba77-d346ef139c0f)

The default border color used is grey (33808). The code change can be made in Chirp (see above) or Radtel's CPS software. Please note if using the latter to change logo or border colour, CPS will throw an error after reading from radio. Nothing else gets changed during upload, but best save a Chirp back up file before proceeding. Always download to radio first, don't change border colour and upload as the data will/could get corrupted and/or overwritten (backup data regularly just in case).

**Speech Inversion Frequencies**

- 1 1350Hz
- 2 1400Hz
- 3 1450Hz
- 4 1500Hz
- 5 1550Hz
- 6 1600Hz
- 7 1650Hz
- 8 1700Hz

v2.1.3 bugs: Steef noted the search function was inop (scan/search speed too high, reduced speed FIXED)

## Features in v2.1.3
- All stock features: [check user manual](https://cdn.shopifycdn.net/s/files/1/0564/8855/8800/files/RT-890_user_manual.pdf?v=1670288968)
- RX is unlocked 10 MHz to 1.3 GHz CAUTION experimental use only. BK4819 (useable) RX is approx 50-600 MHz; Reception outside of this range is possible but not guaranteed; radio may also exhibit erratic behaviour.
- TX 2m/70cm officially, plus various VHF/UHF bands 136-470 MHz. Output varies by band: 2-6W generally on High; max 3.6W on Low power - all figures approx - your mileage may vary 💀 CAUTION do not TX outside of chip specification, it could destroy your radio and/or breach your country of residence radio communications laws/license agreement 💀
- 0.01K to 5 MHz steps
- 999 channel memory
- (N/W)FM, (N/W)AM and SSB (SB) (LSB/USB) modulation
- Light and dark theme, user selectable
- Restyled ui, fixed alignment issues, renamed menu items, new use for status bar (Scan List #), colour changes, font changes, improved clarity etc
- Squelch sensitivty and S-meter revisions
- DCS RX revised/Freq Detect DCS fixed
- PTT BCLO TX during monitor revised, PTT now TX when monitor is open
- RSSI timer speed reduction to reduce internal RFI caused by SPI (screen) updates
- Full colour spectrum with control options, views
- ~~AM Fix ported from 1 of 11's UV-K5 firmware~~
- 1 of 11's "AM Fix" was removed in v2.0.8-naf (and subsequent versions), replaced entirely by dynamic AGC Mode and Reg Editor
- ~~Flashlight Mode~~
- ~~NOAA Monitor~~
- Custom side key and configurable "quick access" keypad actions; Note: Flashlight and NOAA have been removed in v2.1.2 and Modulation, Bandwidth and TX CTCSS/DCS action keys added
- New Dialogs for FM Radio and Bandwidth (key actions)
- Clock speed 120 MHz (OEFWCOM 72 MHz)
- Display BK4819 AGC Modes/ battery voltage registers in single VFO mode (firmware v2.0.3+ reg values can be adjusted) 
- Display dBM when receiving (calculation accuracy revised)
- Reworked scan functionality
  - 8 Scan lists plus scan all
  - Faster scanning (16 ch/s)
  - Resume mode: Time, Carrier, No resume
  - Change scan direction while scanning (up/down keys)
  - Force scan resume (up/down keys)
- Reworked main-sub menu system, renamed items. *I currently don't intend changing the main menu order, as I've been using this radio for 2 years and got used to it as is, despite its short comings and additions! It can be amended should you wish, its just a two file change (app/menu.c and app/menu.h). Rearrange to your preference, but remember, both file lists have to be in the same order otherwise the menu won't work correctly
- Ability to disable green LED flashing when scanning/searching
- and many more ui improvements...

Note, the 10 character name tag font is UPPER CASE English alphanumeric only and limited in special characters/punctuation: . : - = < >

Lower case or any other characters will not display ie tag will be blank.

## Background and previous versions
Radtel, Ruyage, iradio v1.34 (initially v1.33) transceiver firmware was originally reversed and rewritten in C by Dual Tacyhon to become Open Edition FirmWare (OEFW). Developed further by OEFW COMmunity (OEFWCOM) during late 2023-present.

My firmware is a spinoff of OEFWCOM, and that is the code my changes are based on, it is not identical though. I was keen to improve the overall look and my revisions are unashamedly airband related and so may not reflect your own listening habits. AM on ranges 108-136.975, 137-143.975 and 225-399.975 MHz is truly exceptional using all manner of antennae including preamp usage. Amateur 2m/70cm and VHF marine 156-162.975 MHz is also greatly improved.

![20240309_185145](https://github.com/M7OCM/890/assets/128899149/777e58c8-906f-46b7-8184-b24473094a61)

[The base source](https://github.com/OEFW-community/RT-890-custom-firmware/blob/main/README.md)

[v1.0 series bin files](https://github.com/M7OCM/890/tree/oefwc-m7ocm-archive)

[v2.0.0-v2.1.0 source](https://github.com/M7OCM/890/tree/source)

Experimental ui. I've been working on bringing back the awful display divider line 😁 That required a major overhaul of individual display coordinates and really shows up a wonky screen of which I have 2! Looks better than stock I think but pretty crammed in. Back burner projects those. Some bits might get used in later releases.

Please note display images are of previous versions - or newer ones in development - and therefore subject to change or modification and may not be representative of final release.

![20240204_152010](https://github.com/M7OCM/890/assets/128899149/33220955-c419-41e2-8304-e6dd5704d353)

![20240206_075833](https://github.com/M7OCM/890/assets/128899149/86dea561-664c-4a53-8526-8f1dc07290f7)

![20240204_231208](https://github.com/M7OCM/890/assets/128899149/a0bfbfc2-b8df-4158-975c-06fb8a464de7)

v2.1.2 Updated 11032024 essential DCS fix by Dual Tachyon, plus clean up...

~~v2.1.2 originally released 05032024~~ (2 bin files standard plus aero) NOAA Monitor removed, didn't work effectively; added TX CTCSS/DCS to Key Action; works in harmony with key */RP "reverse frequency", (eg for setting tone on repeater input frequency). Reset keys is mandatory. Menu #66 (note #66 as NOAA menu item removed, was #67 in previous versions).

v2.1.1k released 04032024 added another Key Action: Bandwidth, adjusted default keys to my own, reset Menu #67 to use or remap own.

v2.1.1 released 03032024 After installing this firmware please "Reset Keys" (Menu #67), this is required as changes have been made to the Key Action code. After that, just map buttons as per your preference 👍 Flashlight Mode code removed completely.

v2.1.0 released 27022024, mainly cosmetic. Finalising code prior to completion of project.

v2.0.9-naf REVISED 19022024 minor fixes. Please download again if using the 17022024 version. Not worth a version change.

I am no longer supporting AM Fix in my firmware revisions as AGC Mode/Reg Editor surpasses the formers performance. Includes Xawens (+Marcos input) DCS revision! Cosmetics, plus new Scan List info in status bar, 8 character descriptor editable in helper.c (end of code) then recompile binary.

v2.0.8-naf 09022024 No AM Fix. If you prefer AM Fix use standard v2.0.7.

v2.0.7-naf 07022024 for HARDCORE AIRBAND users 😂 NO AM FIX, yes you read that correctly. I have removed all of 1of11s ported code, so AM reception is in your hands via the AGC Mode/Reg Editor. Use PGA wisely - overload at your peril.

Key 2 preset (was AM Fix) is defaulted to Flashlight mode for time being (is already changed in upcoming v2.0.8-naf, to original TX Priority). The 2.0.7-naf menu AM Fix option is replaced by *AM Fix Void - if selected, this again defaults to Flashlight, just as a reminder this is a no AM Fix firmware edition 😉

AGC Mode and Reg Editor; same setup and keys, however for airband it is advised to use AGC Mode F2 (or F1 if using an external antenna) or change values to suit your antenna, location etc. Unless using a nail as an antenna 😁 AGC Modes A3 and F3 provide too much gain without AM Fix. These values are OEM default for FM RX. Hence the overloading issue with AM on stock. Try it though maybe be useable to some.

VHF airband tips. Using a commonly available 771 type antenna, good location for air traffic, set F2. Test for local overloading and try reducing LNAS3 to 2. If reception is borderline try LNA 4 and PGA 3 or 4. I have mine on default F2 (all the 3's barring BW4 WK0). Sometimes for military UHF airband, I'll bump up LNA and PGA to 4. LNAS can be adjusted accordingly. Best to experiment not only with registers but antennae. Stock works well on airband despite the bad rep it receives online.

FM can be greatly enhanced. Crank up the gain it works wonders. Try it!

As before, registers reset when unit is switched off, but remain active while powered on. AGC Mode works in vfo, channel and spectrum modes.

v2.0.7 07022024 a minor revision, includes CR7BLE's excellent RSSI timer update (slows down screen updates to reduce interference).

v2.0.6 02022024 single binary, mic gain implementation by Xawen, few cosmetics, also I've cured (as opposed fixed) a pixel shift "issue" which has been bugging me for months!

v2.0.5 27012024 (AGC Mode/Reg edit issue resolved thanks Xawen) single binary, squelch threshold revision; based on data gleaned from various UV-K5 firmware sources.

Note: looks identical to 2.0.4 check version Menu 74.

v2.0.4 24012024 single binary, colour not mono, cosmetic, AGC Mode id in single ch/vfo. A=Auto (AM Fix) F=Fixed - manual value changes to pre existing AGC Modes.

v2.0.3 21012024 two binaries mono and colour, you decide ;-) AM enhancement; full manual control or auto AM Fix, AGC modes and AM/FM register editor.

Instructions: Assign three quick access keys via menu, I recommend Key 2, 5 and 8. All long press.

Key 2 AM Fix on/off (manual mode should be off), Key 5 AGC Mode, toggle selection and Key 8 Reg Editor.

To edit registers, use up/down keys to scroll and 2/3 to decrease/increase value. Exit to save. If AM Fix is on, AUTO3 plus FIX 3, (from v2.0.4+ A3 and F3) values will be reduced automatically (when necessary) to prevent overloading and then reset to default after squelch closes.

Manual value interaction is better in my opinion as weak signal capture is vastly improved and stronger signals heard with more clarity and greater amplification. AM Fix tends to impose a blanket dBM reduction leading to inconsistent audio levels.

After many weeks of testing, fixed AGC mode FIX 2 (F2) is a good starting point for excellent airband listening on a airband resonant handheld whip. Higher gain antennas (plus pre amp, external roof mounted etc) will likely require FIX 1 (F1). If the point of saturation is exceeded and squelch opens, the receiving frequency will reset to 000.00000. Reduce value(s) accordingly.

Users are encouraged to experiment based on location and antenna used. As a rule of thumb, high PGA combined with high LNA values will result in distortion of strong local AM signals. Consider reducing LNAS to value 2, and/or reducing PGA by 1 or more units. Weaker signals can be captured by adjusting BW (Bandwidth) and WK (Weak) values. Squelch will be your friend, dont forget its super sensitive after modding.

Please note registers reset to default on shutdown. All BK4819 AGC modes are available though F0, F7, F6 and F5 are undocumented and most have default zero values, some of those values reset or maybe unchangeable which is normal behaviour. Unlike AM fix, register edits also work on FM.

Barring this feature and some cosmetics (status bar Dual Standby DSB changed to RX2), there is no other changes from v2.0.2 so no need to update if AM - particularly airband - isn't of interest.

"Color" version best viewed in Dark Theme as the Light Theme lacks the clarity of black on white, white on black. Thats why I never used coloured text on a white background! 😉 See mono binary for that.

2.0.2It colour update 31122023: OEM style, blue rx etc. This is a special firmware and differs cosmetically from standard. I revised this exclusively for an Italian amateur radio group on Telegram.

2.0.2 refactoring 21122023 one bin: black/white (default).

2.0.1 OEFW bug fix 18122023 two bins: black/green; black/orange.

![20231213_164050](https://github.com/M7OCM/890/assets/128899149/74cf0357-34b5-433f-a620-4f2a853c2a49)

2.0.0 official release 13122023 two bins: black/green; black/orange. Credits: OEFW - OEFWCOM

![20231213_164032](https://github.com/M7OCM/890/assets/128899149/72efdbd4-1906-4ca0-bf35-fc0a35ed616d)

Please note the bin files herein are two colour themes only, not multiple colour versions that can be switched within the radio menu. Colours can be readily changed in ui/gfx.c (info in file).

**Disclaimer, experimental firmware on a radio known for spurious signals and harmonics... what could possibly go wrong?!** 🧯

**Use at own risk, no guarantee anything will work correctly or as intended. Back up your SPI with OEFW firmware flasher tool before using (see files) and radio data with Chirp.**

## OEFWCOM
This is a personal project for my use primarily. Its uploaded here for archiving and for others to try as is. This is my vision, it might not be yours and that is fine. Please try OEFW or the other OEFWCOM releases. The latter being fully supported by an active group on [Telegram RT890 OEFW](https://t.me/RT890_OEFW)

All versions have been updated with functional changes wherever possible making the latest release the most current and/or 'fixed'. That said, not all OEFWCOM features are implemented and some of mine are unique. My revisions have been focussed on ui and operability for my use.

OEFW 890 has come a long way in a very short space of time and is very useable now compared to stock.

OEFWCOM is an ongoing development project by enthusiasts, in their own time and for no financial gain. Testers are encouraged to provide feedback to the developers, without it, progess and implementation is unlikely to happen. So please do get involved.

[Telegram RT890 OEFW](https://t.me/RT890_OEFW)

[OEFWCOM](https://github.com/OEFW-community/RT-890-custom-firmware)

## OEFW/OEFWCOM

[DualTachyon](https://github.com/dualtachyon)

[CR7BLE](https://github.com/jcalado)

LCiccio

Marcos

[Psy97x](https://github.com/Psy97x)

[Reppad](https://github.com/Reppad)

[Superogira](https://github.com/superogira)

[Tunas1337](https://github.com/tunas1337)

[Xawen](https://github.com/xawen)

Many thanks to them all!

73

M7OCM

[Supporting Open Edition Firmware](https://ko-fi.com/dualtachyon)🔚
