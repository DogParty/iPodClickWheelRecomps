import sys,glob
rows=[l.split('\t') for l in open(sys.argv[1])][1:]
F={int(r[0],16):int(r[5]) for r in rows}
game={a for a in F if a>=0x18002c28}
def targets(p):
    s=set()
    for l in open(p): s.add(int(l.split()[1],16))
    return s
new=targets(sys.argv[2])&game
prev=set()
for p in sys.argv[3:]: prev|=targets(p)&game
print(f"this run: {len(new)}/{len(game)} game fns, {sum(F[a] for a in new)} insns")
print(f"new vs all earlier runs+probes: {len(new-prev)} functions, {sum(F[a] for a in new-prev)} insns")
print("newly reached (biggest first):", [(hex(a),F[a]) for a in sorted(new-prev,key=lambda a:-F[a])[:25]])
allc=new|prev
print(f"cumulative: {len(allc)}/{len(game)} fns, {sum(F[a] for a in allc)}/{sum(F[a] for a in game)} insns")
print("still unreached biggest:", [(hex(a),F[a]) for a in sorted(game-allc,key=lambda a:-F[a])[:15]])
