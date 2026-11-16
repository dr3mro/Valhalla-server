#!/usr/bin/env python3
"""Small, standalone fuzzer for the four auth-adjacent endpoints: providers/users create and login.
Separate from sqlmap_test.sh's full 89-step sweep - this is a quick, focused check you can run on its
own without touching any other endpoint or needing sqlmap installed.

Two passes, both pure stdlib (no sqlmap, no extra packages):

1. SQLi/auth-bypass pass (fuzz_login / fuzz_create): mutates one field at a time with classic SQLi
   payloads and flags an unexpected 200 on login (possible auth bypass), a 500, a response that echoes
   a SQL/driver error string, or a response that took much longer than the baseline (possible
   time-based blind injection via pg_sleep()).

2. JSON-structure pass (fuzz_structure / fuzz_malformed_body): the same idea PyJFuzz (`pjf`) is built
   for - mutated data types, boundary values, and malformed JSON structures - reimplemented here
   directly because PyJFuzz itself is Python-2-only and unmaintained since 2017 (its setup.py does
   `import commands`, removed in Python 3, so `pip install pyjfuzz` fails to build on Python 3). This
   pass swaps each field to null/int/float/bool/empty-list/nested-dict/huge-string/format-specifiers/
   null-bytes/unicode overflow/negative numbers one at a time, plus three whole-body shapes (a JSON
   array instead of an object, an empty object, and the object with a required field removed), and
   flags only 500s and reflected SQL/driver error text (a 400 for a malformed type is expected and
   NOT flagged - only a crash or a leaked error is).

Usage: fuzz_auth.py [base_url]   (default http://127.0.0.1:8080/api/v2)
Needs: python3 stdlib only.
"""
import json
import re
import sys
import time
import urllib.error
import urllib.request

PAYLOADS = [
    "' OR '1'='1",
    "' OR '1'='1' --",
    "' OR '1'='1' -- -",
    "admin'--",
    "' UNION SELECT NULL--",
    "'; DROP TABLE users; --",
    "\" OR \"1\"=\"1",
    "' OR 1=1#",
    "'||pg_sleep(5)||'",
    "'; SELECT pg_sleep(5); --",
]

SQL_ERROR_HINTS = re.compile(
    r"(syntax error|pg_|postgres|sqlstate|unterminated quoted|duplicate key|relation .* does not exist)",
    re.I,
)
SLOW_THRESHOLD_S = 3.0

# Type/boundary/structure mutations - the same intent as PyJFuzz's "mutated data types, boundary
# values, and malformed structures" (level/techniques CHPTRSX), reimplemented in plain stdlib.
STRUCT_MUTATIONS = [
    ("null", lambda v: None),
    ("int", lambda v: 999999999),
    ("float_max", lambda v: 1.7976931348623157e+308),
    ("bool", lambda v: True),
    ("empty_list", lambda v: []),
    ("nested_dict", lambda v: {"a": {"b": {"c": "d"}}}),
    ("huge_string", lambda v: "A" * 20000),
    ("format_spec", lambda v: "%n%s%d%x"),
    ("null_byte", lambda v: "abc\x00def"),
    ("unicode_overflow", lambda v: "\U0001F4A5" * 200),
    ("big_negative", lambda v: -999999999999999999999),
    ("empty_string", lambda v: ""),
]


def request(base_url, method, path, body):
    url = base_url.rstrip("/") + path
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(
        url, data=data, headers={"Content-Type": "application/json"}, method=method
    )
    t0 = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            text = resp.read().decode("utf-8", "replace")
            return resp.status, text, time.monotonic() - t0
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace"), time.monotonic() - t0
    except urllib.error.URLError as e:
        return 0, str(e), time.monotonic() - t0


def flag(status, text, elapsed, expect_fail):
    reasons = []
    if expect_fail and status == 200:
        reasons.append("unexpected 200 (possible auth bypass / injection accepted)")
    if status == 500:
        reasons.append("500 (likely unhandled SQL error)")
    if SQL_ERROR_HINTS.search(text):
        reasons.append("response contains a SQL/driver error string")
    if elapsed > SLOW_THRESHOLD_S:
        reasons.append("response took %.1fs (possible time-based blind injection)" % elapsed)
    return reasons


def fuzz_login(base_url, grp, run):
    print("\n== %s login ==" % grp)
    baseline = {"username": "nonexistent_%s_%s" % (grp, run), "password": "wrong-pw"}
    status, text, elapsed = request(base_url, "POST", "/clients/%s/login" % grp, baseline)
    print("  baseline -> %s (%.2fs)" % (status, elapsed))

    hits = 0
    for field in ("username", "password"):
        for payload in PAYLOADS:
            body = dict(baseline)
            body[field] = payload
            status, text, elapsed = request(base_url, "POST", "/clients/%s/login" % grp, body)
            reasons = flag(status, text, elapsed, expect_fail=True)
            mark = "FLAG" if reasons else "ok"
            print("  %-4s %-9s %-28r -> %s (%.2fs)%s"
                  % (mark, field, payload, status, elapsed, (" | " + "; ".join(reasons)) if reasons else ""))
            if reasons:
                hits += 1
    return hits


def fuzz_create(base_url, grp, run, template):
    print("\n== %s create ==" % grp)
    hits = 0
    for field in ("username", "email", "fullname", "phone"):
        for i, payload in enumerate(PAYLOADS):
            body = dict(template)
            body["username"] = "%s_%s_%s_%d" % (grp, run, field, i)
            body["email"] = "%s%s%d@mail.com" % (grp, run, i)
            body[field] = payload
            status, text, elapsed = request(base_url, "POST", "/clients/%s/create" % grp, body)
            reasons = flag(status, text, elapsed, expect_fail=False)
            mark = "FLAG" if reasons else "ok"
            print("  %-4s %-9s %-28r -> %s (%.2fs)%s"
                  % (mark, field, payload, status, elapsed, (" | " + "; ".join(reasons)) if reasons else ""))
            if reasons:
                hits += 1
    return hits


def flag_structural(status, text):
    # Unlike the SQLi pass, a 400/422 here is the CORRECT response to a malformed type - only a
    # server crash or a leaked error string is an anomaly worth flagging.
    reasons = []
    if status == 500:
        reasons.append("500 (likely unhandled type/structure error)")
    if SQL_ERROR_HINTS.search(text):
        reasons.append("response contains a SQL/driver error string")
    return reasons


def fuzz_structure(base_url, grp, kind, path, template, run):
    print("\n== %s %s: JSON-structure fuzz (%d fields x %d mutations) =="
          % (grp, kind, len(template), len(STRUCT_MUTATIONS)))
    hits = 0
    tested = 0
    for field, original in template.items():
        for label, mutate in STRUCT_MUTATIONS:
            body = dict(template)
            body[field] = mutate(original)
            # keep identifying fields unique-ish so creates don't just collide on duplicates
            if "username" in body and field != "username":
                body["username"] = "%s_%s_struct_%d" % (grp, run, tested)
            if "email" in body and field != "email":
                body["email"] = "%s%s%d@mail.com" % (grp, run, tested)
            tested += 1
            status, text, _elapsed = request(base_url, "POST", path, body)
            reasons = flag_structural(status, text)
            if reasons:
                hits += 1
                print("  FLAG %-14s %-14s -> %s | %s" % (field, label, status, "; ".join(reasons)))
    print("  %d/%d flagged" % (hits, tested))
    return hits


def fuzz_malformed_body(base_url, grp, kind, path, template):
    print("\n== %s %s: malformed whole-body shapes ==" % (grp, kind))
    cases = [
        ("array_instead_of_object", list(template.values())),
        ("empty_object", {}),
        ("missing_required_field", {k: v for k, v in template.items() if k != next(iter(template))}),
    ]
    hits = 0
    for label, body in cases:
        status, text, _elapsed = request(base_url, "POST", path, body)
        reasons = flag_structural(status, text)
        mark = "FLAG" if reasons else "ok"
        print("  %-4s %-26s -> %s%s" % (mark, label, status, (" | " + "; ".join(reasons)) if reasons else ""))
        if reasons:
            hits += 1
    return hits


def main():
    base_url = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8080/api/v2"
    run = str(abs(hash(time.time())))[-6:]
    print("Target: %s  (run tag: %s)" % (base_url, run))

    provider_template = {
        "avatar": "http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200",
        "active": True, "language": "en", "password": "1234@Abc!",
        "fullname": "Fuzz Test", "phone": "+201299999999", "country": "Egypt",
        "city": "Damietta", "address": "portsaid street", "dob": "17-02-1985",
        "gender": "Male", "position": "Doctor", "speciality": "Cardiology",
    }
    user_template = {
        "avatar": "http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200",
        "language": "en", "active": True, "parent": "0", "password": "123Mm@p0",
        "fullname": "Fuzz Test", "phone": "+201299999999", "country": "Egypt",
        "city": "Damietta", "address": "12 valhalla Street", "dob": "10-10-1990",
        "gender": "Male", "marital": "single", "job": "Doctor",
    }

    login_template = {"username": "nonexistent_%s" % run, "password": "wrong-pw"}

    total_hits = 0
    total_hits += fuzz_login(base_url, "providers", run)
    total_hits += fuzz_login(base_url, "users", run)
    total_hits += fuzz_create(base_url, "providers", run, provider_template)
    total_hits += fuzz_create(base_url, "users", run, user_template)

    total_hits += fuzz_structure(base_url, "providers", "login", "/clients/providers/login", login_template, run)
    total_hits += fuzz_structure(base_url, "users", "login", "/clients/users/login", login_template, run)
    total_hits += fuzz_structure(base_url, "providers", "create", "/clients/providers/create", provider_template, run)
    total_hits += fuzz_structure(base_url, "users", "create", "/clients/users/create", user_template, run)

    total_hits += fuzz_malformed_body(base_url, "providers", "login", "/clients/providers/login", login_template)
    total_hits += fuzz_malformed_body(base_url, "users", "login", "/clients/users/login", login_template)
    total_hits += fuzz_malformed_body(base_url, "providers", "create", "/clients/providers/create", provider_template)
    total_hits += fuzz_malformed_body(base_url, "users", "create", "/clients/users/create", user_template)

    print()
    if total_hits:
        print("RESULT: %d flagged response(s) - review the FLAG lines above." % total_hits)
        return 1
    print("RESULT: no anomalies flagged.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
