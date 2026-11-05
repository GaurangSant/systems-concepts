#!/usr/bin/env python3
"""check_rules.py — flag predictor code that keeps state outside storage.h.

A fast, conservative first pass. Anything it flags must be fixed (or explained);
a clean result is then confirmed by a human or LLM review (see
instructor/llm_review_prompt.md), because no simple checker catches everything.

Rules checked:
  R1  Only these includes: "predictor.h", "storage.h", <cstdint>, <cmath>,
      <algorithm>, <cstdlib>, <climits>.
  R2  No containers, strings, smart pointers, or other library storage types.
  R3  No heap allocation (new, malloc, ...), no file/env/system access.
  R4  No static (except static constexpr constants and static functions),
      mutable, thread_local, extern, or volatile.
  R5  Class members and globals are only: Register / Table / DirectMappedCache /
      SetAssocCache objects, constexpr constants, functions, using-aliases.
  R6  No casts that defeat const (const_cast, reinterpret_cast), no asm,
      no #define (macros can hide declarations), no unions, no raw memory
      functions, no access to the storage ledger (storage_detail).

usage: check_rules.py predictors/*.cpp
"""
import re
import sys

STORAGE_TYPES = ("Register", "Table", "DirectMappedCache", "SetAssocCache")
OK_INCLUDES = {'"predictor.h"', '"storage.h"', "<cstdint>", "<cmath>", "<algorithm>",
               "<cstdlib>", "<climits>"}

TOKEN_RULES = [
    ("R2", r"\bstd\s*::\s*(vector|array|map|multimap|unordered_\w+|set|multiset|deque|list|"
           r"forward_list|bitset|string|queue|priority_queue|stack|pair|tuple|"
           r"unique_ptr|shared_ptr|optional|variant|any|function|span)\b",
     "standard-library storage type"),
    ("R2", r"\b(vector|bitset|unordered_map|deque)\s*<", "container type"),
    ("R3", r"\bnew\b|\b(malloc|calloc|realloc|alloca|free|mmap)\s*\(", "heap allocation"),
    ("R3", r"(?<![.\w>])(fopen|fread|fwrite|ifstream|ofstream|fstream|getenv|system|popen|open|read|"
           r"write|time|clock|rand|srand)\s*\(", "file / environment / time / random access"),
    ("R4", r"\b(mutable|thread_local|extern|volatile)\b", "forbidden storage specifier"),
    ("R6", r"\b(const_cast|reinterpret_cast)\b", "const-defeating cast"),
    ("R6", r"\b(asm|__asm__)\b", "inline assembly"),
    ("R6", r"\bunion\b", "union"),
    ("R6", r"\b(storage_detail|ledger)\b", "touching the storage ledger"),
    ("R6", r"\b(memcpy|memset|memmove)\s*\(", "raw memory access"),
]


def strip(src):
    """Remove comments and string/char literals, keeping line structure."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * src.count("\n", i, j))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def scopes(code):
    """Yield (statement_text, line, scope_kind) for top-level statements of the
    file and of every class/struct body; also yield ('<body>', line, 'func')
    texts for function bodies so static locals can be found."""
    results = []

    def walk(i, kind):
        # kind: 'global' | 'class'. Returns index after the closing brace.
        buf, start_line = [], None
        while i < len(code):
            c = code[i]
            if start_line is None and not c.isspace():
                start_line = code.count("\n", 0, i) + 1
            if c == "}" and kind != "global":
                if "".join(buf).strip():
                    results.append(("".join(buf).strip(), start_line, kind))
                return i + 1
            if c == ";":
                text = "".join(buf).strip()
                if text:
                    results.append((text, start_line, kind))
                buf, start_line = [], None
                i += 1
                continue
            if c == ":" and kind == "class" and re.fullmatch(r"\s*(public|private|protected)\s*",
                                                              "".join(buf)):
                buf, start_line = [], None
                i += 1
                continue
            if c == "{":
                head = "".join(buf)
                if re.search(r"\b(class|struct)\b", head) and "(" not in head:
                    results.append((head.strip(), start_line, kind + "-classhead"))
                    i = walk(i + 1, "class")
                    buf, start_line = [], None  # trailing ';' handled next loop
                    continue
                if re.search(r"\bnamespace\b", head):
                    i = walk(i + 1, "global")
                    buf, start_line = [], None
                    continue
                if "(" in head and not re.search(r"=\s*$", head.strip()) and not \
                        re.match(r"\s*(" + "|".join(STORAGE_TYPES) + r")\b", head):
                    # function body: capture it whole
                    depth, j = 1, i + 1
                    while j < len(code) and depth:
                        depth += {"{": 1, "}": -1}.get(code[j], 0)
                        j += 1
                    results.append((head.strip(), start_line, kind + "-func"))
                    results.append((code[i:j], code.count("\n", 0, i) + 1, "body"))
                    buf, start_line = [], None
                    i = j
                    continue
                # brace initializer: copy through matching brace into buf
                depth, j = 1, i + 1
                while j < len(code) and depth:
                    depth += {"{": 1, "}": -1}.get(code[j], 0)
                    j += 1
                buf.append(code[i:j])
                i = j
                continue
            buf.append(c)
            i += 1
        if "".join(buf).strip():
            results.append(("".join(buf).strip(), start_line, kind))
        return i

    walk(0, "global")
    return results


def check_decl(text, kind):
    """Return a problem string for a class-member or global statement, or None."""
    t = re.sub(r"\s+", " ", text).strip()
    if not t or t.startswith("#"):
        return None
    if kind.endswith("-classhead") or kind.endswith("-func"):
        if kind.endswith("-func") and re.match(r"(static )", t) and "(" not in t:
            return "static data"
        return None
    if re.match(r"(using|typedef|friend|template)\b", t) or t.startswith("REGISTER_PREDICTOR"):
        return None
    if re.match(r"(static )?(inline )?constexpr\b", t) or re.match(r"(static )?const\s", t) and \
            "constexpr" in t:
        return None
    if re.match(r"(" + "|".join(STORAGE_TYPES) + r")\b", t):
        return None
    if re.match(r"(class|struct|enum)\b", t) and "{" not in t:  # forward decl / closing
        return None
    # function declarations / defaulted/deleted members
    if re.search(r"\)\s*(const)?\s*(override|final|noexcept)?\s*(=\s*(0|default|delete))?$", t) \
            and not re.search(r"=\s*[^=0dD]", t.split("(")[0]):
        return None
    if re.search(r"^~?\w+\s*\(", t):  # constructor / destructor declaration
        return None
    return "non-storage variable"


def check(path):
    raw = open(path).read()
    code = strip(raw)
    lines = code.split("\n")
    problems = []

    raw_lines = raw.split("\n")
    for ln, line in enumerate(lines, 1):
        m = re.match(r"\s*#\s*include\s*(\S+)", raw_lines[ln - 1]) if \
            re.match(r"\s*#\s*include", line) else None
        if m and m.group(1) not in OK_INCLUDES:
            problems.append((ln, "R1", f"include {m.group(1)} not allowed"))
        if re.match(r"\s*#\s*define\b", line):
            problems.append((ln, "R6", "#define not allowed"))
        for rule, pat, msg in TOKEN_RULES:
            if re.search(pat, line):
                problems.append((ln, rule, msg))

    code_nopp = "\n".join("" if re.match(r"\s*#", l) else l for l in lines)
    for text, ln, kind in scopes(code_nopp):
        if kind == "body":
            for m in re.finditer(r"\bstatic\b", text):
                problems.append((ln + text.count("\n", 0, m.start()), "R4", "static local variable"))
            for m in re.finditer(r"\b(" + "|".join(STORAGE_TYPES) + r")\s+\w+\s*[{(;]", text):
                problems.append((ln + text.count("\n", 0, m.start()), "R5",
                                 "storage object created inside a function"))
            continue
        why = check_decl(text, kind)
        if why:
            first = re.sub(r"\s+", " ", text)[:70]
            problems.append((ln, "R5", f"{why}: `{first}`"))

    return sorted(set(problems))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    bad = 0
    for path in sys.argv[1:]:
        probs = check(path)
        if probs:
            bad += 1
            for ln, rule, msg in probs:
                print(f"{path}:{ln}: [{rule}] {msg}")
        else:
            print(f"{path}: OK")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
