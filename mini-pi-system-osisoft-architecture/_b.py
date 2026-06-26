import os
base = r"F:
ano-everything\mini-control-engineering-practice	. mini-industrial-real-time-database\mini-pi-system-osisoft-architecture"
def w(rel, content):
    fp = os.path.join(base, rel)
    os.makedirs(os.path.dirname(fp), exist_ok=True)
    with open(fp, "w", encoding="utf-8") as f:
        f.write(content)
    return content.count("
")
TL = 0
