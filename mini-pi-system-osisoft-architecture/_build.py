import os, sys
base = os.path.dirname(os.path.abspath(__file__))
def w(p, L):
    fp = os.path.join(base, p)
    os.makedirs(os.path.dirname(fp), exist_ok=True)
    with open(fp, "w", encoding="utf-8") as fh:
        fh.write(chr(10).join(L) + chr(10))
    return len(L)
TL = 0

L = ["""/** pi_da_types.c - Core PI Data Archive Type Implementations */""",
"""#include <stdio.h>""",
"""#include <string.h>""",
"""#include <math.h>""",
"""#include <time.h>""",
"""#include <stdlib.h>""",
"""#include "../include/pi_da_types.h"""",
""""""",
"""const char* pi_timestamp_to_iso(const pi_timestamp_t *ts) {""",
"""    static char buf[32];""",
"""    if (!ts || ts->seconds == INT64_MAX) {""",
"""        strncpy(buf, "*NOW*", 31); buf[31] = 0; return buf;""",
"""    }""",
"""    if (ts->seconds == 0 && ts->subsec == 0) {""",
"""        strncpy(buf, "*EMPTY*", 31); buf[31] = 0; return buf;""",
"""    }""",
"""    time_t sec = (time_t)ts->seconds;""",
"""    struct tm tbuf;""",
"""#ifdef _WIN32""",
"""    gmtime_s(&tbuf, &sec);""",
"""#else""",
"""    gmtime_r(&sec, &tbuf);""",
"""#endif""",
"""    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%07u",""",
"""             tbuf.tm_year + 1900, tbuf.tm_mon + 1, tbuf.tm_mday,""",
"""             tbuf.tm_hour, tbuf.tm_min, tbuf.tm_sec, ts->subsec);""",
"""    return buf;""",
"""}"""]
TL += w("src/pi_da_types.c", L)
print(f"pi_da_types.c: {TL} lines cumulative")
