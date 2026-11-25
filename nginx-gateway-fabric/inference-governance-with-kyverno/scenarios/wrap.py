#!/usr/bin/env python3
"""Wraps the clips' text at a space, not mid-word, for a terminal <cols> wide (scenarios/lib.sh).

  wrap.py <cols>              output: a wrapped line keeps the original line's indent
  wrap.py <cols> --command    a typed command: the first line starts after the "$ " prompt, and
                              wrapped lines are indented two more than the line they continue

Colour codes take no width, so coloured output wraps at the same place as plain text. A word
longer than the line breaks where it must.
"""
import re
import sys

ESC = re.compile(r"(\x1b\[[0-9;?]*[A-Za-z])")


def wrap(line, width, used, indent):
    out, col, space = [], used, None   # space: index in out of the last space on this line
    for part in ESC.split(line):
        if ESC.fullmatch(part):
            out.append(part)
            continue
        for ch in part:
            if col >= width:
                if space is not None:
                    out[space] = "\n" + indent
                    col = len(indent) + sum(len(p) for p in out[space + 1:] if not ESC.fullmatch(p))
                else:
                    out.append("\n" + indent)
                    col = len(indent)
                space = None
            if ch == " " and col > len(indent):
                space = len(out)
            out.append(ch)
            col += 1
    return "".join(out)


def main():
    width = int(sys.argv[1])
    command = "--command" in sys.argv[2:]
    text = sys.stdin.read()
    end = "\n" if text.endswith("\n") else ""
    lines = text[:-1].split("\n") if end else text.split("\n")
    out = []
    for i, line in enumerate(lines):
        own = line[: len(line) - len(line.lstrip(" "))]
        indent = own + "  " if command else own
        if len(indent) > width // 2:
            indent = ""
        out.append(wrap(line, width, 2 if command and i == 0 else 0, indent))
    sys.stdout.write("\n".join(out) + end)


if __name__ == "__main__":
    main()
