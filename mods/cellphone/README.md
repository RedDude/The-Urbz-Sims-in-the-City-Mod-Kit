# Cellphone

Use the phone from anywhere. Urb Info gets a fifth button, Cellphone, in the middle of the screen.

## What it does
- Touch Cellphone in Urb Info. Your Sim lifts the phone to their ear and the phone's own choices come up,
  as if they had used a phone: at home Call Services, Call Friend and Invite a Friend Over; away from home
  only Call Friend (the same as the game's phones). No phone object needed, and it costs nothing.
- Call Services works as at home: Fire, Repair Man or Maid, the same answer and the same visit.
- Call Friend and Invite open the Phone mod's screen with the faces. Pick someone (touch their face twice, or the +Control Pad and A). Back in the city, your Sim lifts the
  phone to their ear on the spot, the person answers as usual, and your Sim puts it down.
- The call is the game's own: the same answer, the same +1 relationship.
- If your Sim is busy (using something, in the middle of an action), the button just buzzes and Urb Info stays
  open. The same happens when the Phone mod is switched off.

**Needs the Phone mod** (`mods/phone`); this mod uses its touch screen.
The button's picture is `icon.png` (32 x 32, named in `mod.json` as `"urb_info_icon"`); the kit adds it to
Urb Info's own picture sheet when it builds the ROM.
Switch it off in the game: Options > Mods > Cellphone.

## Proven
`python tests/proofs.py cellphone` (DeSmuME, from the city save on a street with no phone): Urb Info shows
the Cellphone button with icon.png (every coloured pixel checked in the screenshot); the Sim picks up
(animation 0x51) and the phone's question box opens (Call Friend only, on a street); A opens the Call
Friend screen with 8 faces; two touches make the call (the answer box), with the Sim holding the phone (animation 0x51) on the same spot; afterwards the Sim stands (0x04)
and the city is back. The answer is up within 100 frames of the second touch, and the +Control Pad held all that
time does not move the Sim; after the call they walk again. With the Phone mod switched off, the button opens nothing and Urb Info stays open (its icon still on
screen). Pictures:
`build/proofs/cellphone-*.png`.

Seen once, not a proof (DeSmuME, the apartment save, 2026-11-20): at home the box shows all three choices;
Call Services > Fire said "The fireman is coming!" and the fireman came.

Not checked: melonDS; Invite and Repair Man / Maid from the cellphone and real hardware; the pick-up is the home phone's animation, so how it looks with no
phone in reach has only been seen in one screenshot.
