#!/usr/bin/env bash
# abi_layout.sh <our-src> <recomp-ui-src>: every public struct must have the
# same size and field offsets in both headers (clang record-layout dump).
set -euo pipefail
ours="$1"; ref="$2"; cc="${CC:-clang}"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
types=$(grep -oE '^typedef struct RecompLauncherC[A-Za-z]+|^struct RecompLauncherCSettings' "$ours/recomp_launcher.h" \
        | awk '{print $NF}' | sort -u)
dump() {
  { echo '#include "recomp_launcher.h"'
    for t in $types; do
      if [ "$t" = RecompLauncherCSettings ]; then echo "struct $t v_$t;"; else echo "$t v_$t;"; fi
    done; } > "$tmp/$2.c"
  "$cc" -c -o /dev/null -Xclang -fdump-record-layouts -I"$1" "$tmp/$2.c" | grep -E "^ *[0-9:-]+ \| |sizeof=" > "$tmp/$2.txt"
}
dump "$ours" ours
dump "$ref" ref
# recomp-launcher may append fields to a struct (append-only ABI): every
# reference field must keep its offset, and our struct may only grow at the end.
if python3 - "$tmp/ref.txt" "$tmp/ours.txt" <<'PY'
import re, sys
def parse(path):
    out, cur = {}, None
    for line in open(path):
        m = re.match(r"\s*0 \| (?:struct )?(RecompLauncherC\w+)\s*$", line)
        if m and cur is None:
            cur = m.group(1); out[cur] = {"fields": [], "size": 0}; continue
        m = re.search(r"sizeof=(\d+)", line)
        if m and cur:
            out[cur]["size"] = int(m.group(1)); cur = None; continue
        if cur: out[cur]["fields"].append(line.rstrip())
    return out
ref, ours = parse(sys.argv[1]), parse(sys.argv[2])
bad = []
for name, r in ref.items():
    o = ours.get(name)
    if o is None: bad.append(f"{name}: missing"); continue
    if o["fields"][:len(r["fields"])] != r["fields"]: bad.append(f"{name}: reference fields moved")
    if o["size"] < r["size"]: bad.append(f"{name}: shrank")
for b in bad: print("abi_layout:", b, file=sys.stderr)
sys.exit(1 if bad else 0)
PY
then
  echo "abi_layout: $(echo $types | wc -w | tr -d ' ') structs prefix-compatible with $ref"
else
  echo "abi_layout: layout differs from $ref" >&2; exit 1
fi
