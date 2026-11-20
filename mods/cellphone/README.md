# Cellphone

Use the phone from anywhere. Urb Info gets a fifth button, Cellphone, in the middle of the screen.

## What it does
- Touch Cellphone in Urb Info. At home your Sim lifts the phone to their ear and the phone's own choices
  come up: Call Services, Call Friend and Invite a Friend Over. Away from home Call Friend is the only
  choice, so it opens straight away (no question box). No phone object needed, and it costs nothing.
- Call Services works as at home: Fire, Repair Man or Maid, the same answer and the same visit.
- Call Friend and Invite open the Phone mod's screen with the faces when the Phone mod is in the game and
  switched on. Pick someone (touch their face twice, or the +Control Pad and A). Back in the city, your Sim
  lifts the phone to their ear on the spot, the person answers as usual, and your Sim puts it down.
- Without the Phone mod (or with it switched off) you get the game's own list instead: "More" and 4 names
  at a time, as on a phone in the original game.
- The call is the game's own: the same answer, the same +1 relationship.
- If your Sim is busy (using something, in the middle of an action), or knows nobody to call away from home,
  the button just buzzes and Urb Info stays open.

Works on its own. With the Phone mod (`mods/phone`) it uses that mod's screen with the faces.
The button's picture is `icon.png` (32 x 32, named in `mod.json` as `"urb_info_icon"`); the kit adds it to
Urb Info's own picture sheet when it builds the ROM.
Switch it off in the game: Options > Mods > Cellphone.

## Proven
`python tests/proofs.py cellphone` (DeSmuME, from the city save on a street with no phone): Urb Info shows
the Cellphone button with icon.png (every coloured pixel checked in the screenshot); on a street the Call Friend screen
opens within 18 frames of the touch, with no question box on the way (checked every 6 frames), with 11 faces; two touches make the call (the answer box), with the Sim holding the phone (animation 0x51) on the same spot; afterwards the Sim stands (0x04)
and the city is back. The answer is up within 100 frames of the second touch, and the +Control Pad held all that
time does not move the Sim; after the call they walk again. With the Phone mod switched off, the button opens
the game's own list instead and Urb Info stays open (its icon still on screen). Pictures:
`build/proofs/cellphone-*.png`.

`python tests/proofs.py cellphone-alone` (a ROM with no Phone mod, on a street): the Sim picks up, the game's
list of names comes up ("More" and 4 names), the first name answers ("I'll be hanging out at the bayou
brothers' shack for a little while."), and the Sim puts the phone down. Pictures: `build/proofs/cellphone-alone-*.png`.

Seen once, not a proof (DeSmuME, the apartment save, 2026-11-20): at home the box shows all three choices;
Call Services > Fire said "The fireman is coming!" and the fireman came.

Not checked: melonDS; Invite and Repair Man / Maid from the cellphone and real hardware; the pick-up is the home phone's animation, so how it looks with no
phone in reach has only been seen in one screenshot.
