"""Open a chat with person 48 at Slice O' Life Pizza and read the four choices (friendly-topics probe, step 2).
python research/chat/open_chat.py [facing...]"""
import json, os, re, struct, sys, tempfile
KIT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [os.path.join(KIT, 'tests'), os.path.join(KIT, 'verify'), os.path.join(KIT, 'kit')]
import proofs as P, urbz_verify as V
rom, log = P.build('friendly-topics', [os.path.join(KIT, 'mods', 'friendly-topics')])[:2]
base = int(re.search(r'code mod friendly-topics: .* at ([0-9a-f]{8})', log).group(1), 16)
sym = json.load(open(os.path.join(KIT, 'mods', 'friendly-topics', 'code', 'build', 'patch.json')))['symbols']
FT = base + sym['friendly_topics']
state = V.city_state(os.path.abspath(rom))
AREA, WHO = 51, 48
job = lambda script, snap: {'rom': os.path.abspath(rom), 'out': tempfile.mkdtemp(prefix='urbz-chat-'), 'tag': 'snap',
                            'poke': [], 'state': state, 'goto': (AREA, 0), 'script': [['wait', 120]] + script, 'snap': snap}
w = P._world_full(open(V.run_child(job([['shot', 'x']], [P.MEM_HI]))['snaps'][0][1][0], 'rb').read())
pl = [e for e in w if e[1] == 0][0]
pe = [e for e in w if e[1] == 7 and e[2] == WHO][0]
px, py = pe[3], pe[4]
FULL = ['poke', '0x02141204=' + '00000064' * 8]        # player_motives: all needs full (low needs refuse chats)
def hold(x, y, f):
    return [['poke', '0x%08X=%s' % (pl[0] + 0x18, struct.pack('<iiii', x << 16, y << 16, x << 16, y << 16).hex())],
            ['poke', '0x%08X=%02x' % (pl[0] + 0x12, f)],
            ['poke', '0x%08X=%s' % (pe[0] + 0x18, struct.pack('<iiii', px << 16, py << 16, px << 16, py << 16).hex())],
            FULL, ['wait', 4]]
ROUNDS = int(os.environ.get('ROUNDS', '8'))
f = 0
wait = lambda n: sum((hold(px, py + 14, f) for _ in range(n // 4)), [])
steps = wait(20) + [['press', 'A', 4]] + wait(60) + [['press', 'A', 4]] + wait(60) + [['shot', 'deal0']]
for r in range(1, ROUNDS):     # FRIENDLY STUFF (4th choice, bottom screen) deals three new ones
    steps += [['touch', 140, 169, 4]] + wait(50) + [['shot', 'deal%d' % r]]
res = V.run_child(job(steps, ['0x021474F8:0x18', '0x%08X:0x1C' % FT]), timeout=1200)
for s in res['snaps']:
    talk = open(s[1][0], 'rb').read(); ft = open(s[1][1], 'rb').read()
    print(s[0], 'partner', struct.unpack_from('<I', talk)[0], 'choices', talk[0x10:0x14].hex(),
          'mod picks/already/fixed/best', struct.unpack_from('<6I', ft)[1:5])
print(os.path.dirname(res['snaps'][-1][1][0]))
