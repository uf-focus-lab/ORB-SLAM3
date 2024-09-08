#!/usr/bin/env python3

import os, sys, struct

line = sys.stdin.readline()

N = None
with os.fdopen(sys.stdout.fileno(), "wb", closefd=False) as stdout:
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
        try:
            stdout.write(
                struct.pack(
                    f"<I{len(integers)-1}B{len(floats)}d",
                    *map(int, integers),
                    *map(float, floats),
                )
            )
        except struct.error:
            print(f"Invalid input {line}", file=sys.stderr)
            sys.exit(1)
        stdout.flush()

print("OK", f"N = {N}", file=sys.stderr)
