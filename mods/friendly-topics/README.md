# Friendly Topics

Every chat shows at least one topic the person likes. No more tapping FRIENDLY STUFF over and over to
find one.

## What it does
- When a chat opens, and each time FRIENDLY STUFF deals new choices, the game draws three topics at random
  as usual.
- If none of them would raise your relationship, the mod swaps the last one for a topic this person likes
  (picked at random among the ones they like).
- If the game already drew a liked topic, nothing changes.
- It never adds Flirt, Hug or Kiss. Those keep their own rules (see Any Romance).
- "Likes" means the same thing the game uses: the topic's number in the person's reply table, plus your
  Rep bonus with their group (-2 to +2). Above zero, the relationship goes up.

Switch it off in the game: Options > Mods > Friendly Topics.

## Proven
`python tests/proofs.py friendly-topics`: 12 deals in a chat with Lottie Cash at Slice O' Life Pizza.
Every deal showed a topic she likes. The game's own draw had none 2 times; the mod put one in both times
(build/proofs/friendly-topics.png: Kiss, Talk about Sleeping, and the mod's Talk about Science).

Not checked in the emulator: actually choosing the added topic (it uses the game's own reply, so it should
go up like any liked topic). Only tried in DeSmuME, not melonDS.
