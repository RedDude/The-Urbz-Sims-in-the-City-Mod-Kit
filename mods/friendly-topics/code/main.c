/* Friendly Topics: every chat shows at least one topic the person likes.

   social_pick_options (0x0208F230) fills social_options[0..3]: slot 0 can be What's Up? / The 411, then
   random topics (codes 0..0x45, no repeats), and FRIENDLY STUFF (0x4A) last. What a topic does is in the
   person's reply table (social_replies, docs/systems.md "Chats"): the relationship changes by the entry's
   number plus a Rep bonus (-2..+2, the player's standing with the person's group). Above 0 = they like it.

   After the game's pick: if no random choice is liked, the last random one becomes a liked topic chosen at
   random (plain topics 0..0x42 only: never Flirt/Hug/Kiss, which have their own rules). If the person likes
   no topic at all (a very low Rep bonus), it becomes the best-received one, when that beats what's shown. */
#include "game.h"
#include "mod.h"

#define social_pick_options GAME_FN(ADDR_social_pick_options, void (*)(void))
#define game_rand           GAME_FN(ADDR_game_rand, int (*)(int max))
#define rep_group_of        GAME_FN(ADDR_rep_group_of, int (*)(int person))
#define rep_bonus           GAME_FN(ADDR_rep_bonus, int (*)(void *rep, int group))

#define FIRST_PERSON 31
#define LAST_PERSON  66            /* the townspeople with reply tables (the table runs into other data after) */
#define PERSON_SIZE  0x274
#define LAST_PLAIN   0x42          /* topics we may put in: 0..0x42 (0x43 Flirt, 0x44 Hug, 0x45 Kiss, 0x46 extra) */
#define LAST_RANDOM  0x46          /* the most the game's draw can give; slots above are fixed (0x4A, 0x4E, 0x53...) */
#define NO_ENTRY     (-128)

struct { u32 magic, picks, already, fixed, best; u8 before, after, person, pad; } friendly_topics =
    { 0x444E5246 };                /* 'FRND' (tests) */

/* The reply entry's relationship change for a topic, as social_reply_entry (0x0208ABF4) finds it. */
static int topic_change(const u8 *table, int code)
{
    int off;
    if (code == 0x15)
        off = 0xB0;                /* the game's own table has these two the other way round */
    else if (code == 0x16)
        off = 0xA8;
    else if (code <= LAST_PLAIN)
        off = code * 8;
    else if (code == 0x43)
        off = 0x230;
    else if (code == 0x46)
        off = 0x238;
    else
        return NO_ENTRY;           /* Hug, Kiss: handled elsewhere by the game */
    return (s8)table[off + 4];
}

static int shown(const u8 *opt, int code)
{
    for (int i = 0; i < 4; i++)
        if (opt[i] == code)
            return 1;
    return 0;
}

void friendly_pick(void)
{
    social_pick_options();
    friendly_topics.picks++;

    u8 *opt = (u8 *)ADDR_social_options;
    int person = *(int *)ADDR_talk_partner;
    if (person < FIRST_PERSON || person > LAST_PERSON)
        return;
    const u8 *table = (person == FIRST_PERSON && ((u8 *)ADDR_game_state)[0x959])
                    ? (const u8 *)ADDR_social_replies_alt
                    : (const u8 *)ADDR_social_replies + (person - FIRST_PERSON) * PERSON_SIZE;
    int bonus = rep_bonus((void *)ADDR_rep_groups, rep_group_of(person));

    int last = -1, best_shown = NO_ENTRY;
    for (int i = 0; i < 4; i++) {
        if (opt[i] > LAST_RANDOM)
            continue;
        last = i;
        int c = topic_change(table, opt[i]);
        if (c != NO_ENTRY && c + bonus > best_shown)
            best_shown = c + bonus;
    }
    friendly_topics.person = person;
    if (last < 0)
        return;
    friendly_topics.before = opt[last];
    if (best_shown > 0) {
        friendly_topics.already++;
        friendly_topics.after = opt[last];
        return;
    }

    /* Count the liked topics not on screen (or, if none, the best-received ones), then pick one at random. */
    int top = -1000, n = 0;
    for (int code = 0; code <= LAST_PLAIN; code++) {
        if (shown(opt, code))
            continue;
        int v = topic_change(table, code) + bonus;
        if (v > 0)
            v = 1;                 /* every liked topic counts the same */
        if (v > top)
            top = v, n = 0;
        if (v == top)
            n++;
    }
    if (n == 0 || top <= best_shown)
        return;                    /* nothing better than what's shown */
    int k = game_rand(n - 1);
    for (int code = 0; code <= LAST_PLAIN; code++) {
        if (shown(opt, code))
            continue;
        int v = topic_change(table, code) + bonus;
        if (v > 0)
            v = 1;
        if (v == top && k-- == 0) {
            opt[last] = code;
            break;
        }
    }
    if (top > 0)
        friendly_topics.fixed++;
    else
        friendly_topics.best++;
    friendly_topics.after = opt[last];
}
