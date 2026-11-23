/* Auto Interact: press A at an object that has exactly one choice and the Sim does it at once, with no
   question box.

   How the game asks (docs/systems.md "The player using an object"): A at the pointed-at object calls
   object_menu_build (0x02067760). It writes the choices as u8 activity numbers at object+0x132 (the object's
   own, then Put in Pocket 0x2C and Move 0x25, those two only in the home area), copies their texts into
   dialog_text and opens the question box with BL set_state_here(2) at 0x02067A10. The player waits in state
   0xF; one tick after the box closes object_menu_result (0x020675F4) reads dialog_result, a 0-based index into
   that list, and starts that choice (or does nothing when it is 0x7FFFFFFF, cancelled).

   We take that one call over. With exactly one choice we do not open the box: we write the answer the box
   would have given (0, the first and only choice) into dialog_result and return; the game goes on exactly as
   after a touch on the first choice. With two or more choices (most objects at home have Put in Pocket and
   Move too) we open the box as the game does.

   The count is the run of non-zero bytes at object+0x132, at most 7, as object_menu_build counts them. */
#include "game.h"
#include "mod.h"

#define LIST       0x132               /* object entity: u8 choice[7] */
#define MAX_LIST   7
#define BOX_STATE  2                   /* the top screen's question box */
#define set_state_here GAME_FN(ADDR_set_state_here, void (*)(int state, int mode, int param))

struct { u32 magic, skipped, asked, last_n; } auto_interact = { 0x49545541, 0, 0, 0 };   /* 'AUTI' (tests) */

#define POINTED_AT 0x021473FC          /* what A would use: the object's node or entity (docs/systems.md) */
#define in_ram(p)  ((u32)(p) >= 0x02100000u && (u32)(p) < 0x02300000u)

/* The pointed-at object's entity, the one that holds the list (entity+0x11c = its node, node+0x24 = it). */
static u8 *object_entity(void)
{
    u8 *p = *(u8 **)POINTED_AT;
    if (!in_ram(p))
        return 0;
    u8 *cand[2] = { p, *(u8 **)(p + 0x24) };
    for (int i = 0; i < 2; i++) {
        u8 *e = cand[i], *node;
        if (in_ram(e) && in_ram(node = *(u8 **)(e + 0x11C)) && *(u8 **)(node + 0x24) == e)
            return e;
    }
    return 0;
}

/* The game's call set_state_here(2) (the wrapper at 0x0204E038 passes mode 0, param 0). */
void auto_interact_open(int state)
{
    u8 *pl = object_entity();   /* (the list is on the object's entity) */
    int n = 0;
    if (pl && state == BOX_STATE)
        while (n < MAX_LIST && pl[LIST + n])
            n++;
    auto_interact.last_n = n;
    if (n == 1) {
        u32 screen = GAME_VAR(ADDR_cur_screen, u32);
        *(volatile u32 *)(ADDR_screen_block + 0xD4 + screen * 0x8C) = 0;   /* dialog_result: the first choice */
        auto_interact.skipped++;
        return;
    }
    auto_interact.asked++;
    set_state_here(state, 0, 0);
}
