#!/usr/bin/env python3
"""Build an llm-d-inference-sim SQLite dataset from a text file (one answer per line).

No tokenizer is involved: request prompts never hash-match a record, so the simulator returns a
random record that fits the response length. Every record is on-theme, so every answer is too.
Schema matches llm-d-inference-sim v1.0.0 (table llmd: id, prompt_hash, gen_tokens, n_gen_tokens).

Usage: build.py <input.txt> <output.sqlite3>
"""
import hashlib
import json
import os
import sqlite3
import sys


def main() -> None:
    src, out = sys.argv[1], sys.argv[2]
    with open(src, encoding="utf-8") as f:
        lines = [line.strip() for line in f if line.strip() and not line.startswith("#")]
    if os.path.exists(out):
        os.remove(out)
    db = sqlite3.connect(out)
    db.execute(
        "CREATE TABLE llmd (id INTEGER PRIMARY KEY, prompt_hash BLOB NOT NULL, "
        "gen_tokens JSON NOT NULL, n_gen_tokens INTEGER NOT NULL)"
    )
    for i, text in enumerate(lines):
        words = text.split(" ")
        strings = [w if j == 0 else " " + w for j, w in enumerate(words)]
        tokens = [int(hashlib.sha1(s.encode()).hexdigest()[:6], 16) for s in strings]
        db.execute(
            "INSERT INTO llmd (prompt_hash, gen_tokens, n_gen_tokens) VALUES (?, ?, ?)",
            (
                hashlib.sha256(f"{src}:{i}".encode()).digest(),
                json.dumps({"Tokens": tokens, "Strings": strings}),
                len(strings),
            ),
        )
    db.commit()
    db.close()
    print(f"{out}: {len(lines)} records")


if __name__ == "__main__":
    main()
