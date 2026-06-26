import os, sys
base = os.path.dirname(os.path.abspath(__file__))
def w(rel, content):
    p = os.path.join(base, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w", encoding="utf-8") as fh:
        fh.write(content)
    return content.count(chr(10))
total = 0

# === pi_da_types.h ===
total += w("include/pi_da_types.h", open("pi_da_types_h.txt", "r").read())
