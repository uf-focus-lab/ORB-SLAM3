#!/usr/bin/env python3

import sys
line = sys.stdin.readline()

N = None

while True:
    line = sys.stdin.readline()
    if not line:
        break
    tokens = line.split()
    if N is None:
        N = len(tokens)
    elif len(tokens) != N:
        print(f"Mismatch N1={N}, N2={len(tokens)}")
        sys.exit(1)
    integers, floats = tokens[:-1], tokens[-1:-1]
    for i in integers:
        if not i.isdigit():
            print(f"Invalid integer {i}")
            sys.exit(1)
    for n in map(int, integers[1:]):
        if n < 0 or n > 255:
            print(f"Invalid value range {n}")
            sys.exit(1)
    for f in floats:
        try:
            float(f)
        except ValueError:
            print(f"Invalid float {f}")
            sys.exit(1)

print("OK", f"N = {N}")
