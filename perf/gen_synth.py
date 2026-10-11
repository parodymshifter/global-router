#!/usr/bin/env python3
"""Write a deterministic synthetic ISPD-2008-format .gr design to stdout.

The design is the same on every machine and every Python version (it uses
its own linear congruential generator, not the random module), so a timing
difference between two runs is a difference in the router, not the input.

usage: gen_synth.py [nx ny layers nets seed]
"""
import sys


def main():
    nx = int(sys.argv[1]) if len(sys.argv) > 1 else 90
    ny = int(sys.argv[2]) if len(sys.argv) > 2 else 90
    layers = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    nets = int(sys.argv[4]) if len(sys.argv) > 4 else 3500
    seed = int(sys.argv[5]) if len(sys.argv) > 5 else 20261011
    tile = 10
    cap = 8  # capacity per tile edge; a wire needs width 1 + spacing 1 = 2

    state = [seed & 0xFFFFFFFF]

    def rnd(n):
        state[0] = (state[0] * 1103515245 + 12345) & 0xFFFFFFFF
        return (state[0] >> 8) % n

    out = []
    out.append("grid %d %d %d" % (nx, ny, layers))
    # Odd layers (1, 3, ...) run horizontally, even layers vertically.
    out.append("vertical capacity " + " ".join(str(0 if i % 2 == 0 else cap) for i in range(layers)))
    out.append("horizontal capacity " + " ".join(str(cap if i % 2 == 0 else 0) for i in range(layers)))
    out.append("minimum width " + " ".join("1" for _ in range(layers)))
    out.append("minimum spacing " + " ".join("1" for _ in range(layers)))
    out.append("via spacing " + " ".join("0" for _ in range(layers)))
    out.append("0 0 %d %d" % (tile, tile))
    out.append("num net %d" % nets)
    for i in range(nets):
        # Mostly short nets with a tail of long ones, like a real placement.
        spread = 4 if rnd(10) < 7 else (12 if rnd(10) < 8 else 40)
        npins = 2 + rnd(4)
        cx, cy = rnd(nx), rnd(ny)
        out.append("n%d %d %d 1" % (i, i, npins))
        seen = set()
        for _ in range(npins):
            for _try in range(8):
                x = min(nx - 1, max(0, cx + rnd(2 * spread + 1) - spread))
                y = min(ny - 1, max(0, cy + rnd(2 * spread + 1) - spread))
                if (x, y) not in seen:
                    break
            seen.add((x, y))
            out.append("%d %d 1" % (x * tile + tile // 2, y * tile + tile // 2))
    out.append("0")
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
