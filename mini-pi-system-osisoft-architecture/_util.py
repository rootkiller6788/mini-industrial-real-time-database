import os
base = os.path.dirname(os.path.abspath(__file__))
def w(rel, s):
    p = os.path.join(base, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w", encoding="utf-8") as fh:
        fh.write(s)
    return s.count(chr(10))
