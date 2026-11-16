#!/usr/bin/env python3
"""Runs the full, all-89-step API test flow against a live Valhalla-server (same steps/order as
test_api.sh and the docs page's test console/benchmark panel: provider/user -> services -> patient ->
health/drugs/reports -> visit -> requests/prescriptions/paidservices -> appointment -> suspend/activate
-> clean-up), and writes one raw-HTTP request file per step, with every placeholder already filled in
with real ids and tokens from earlier responses. Every step gets its own request file (named after its
step id), even when several steps share the same (method, path) - e.g. "providers create" and "users
create" are both captured, and so are per-role reads/updates/searches - so sqlmap gets a dedicated,
successful baseline for each of the 89 individual test cases, not just one per unique route.

This gives sqlmap a valid, successful baseline request for (almost) every step, which is what lets it
actually test query/body parameters for injection instead of aborting on an unauthorized/malformed
baseline. No '*' markers are added: for a plain JSON body or query string, sqlmap auto-detects every
top-level field as a testable parameter once --batch answers its "process this JSON?" prompt. NOTE:
sqlmap only auto-detects *top-level* JSON keys - nested objects/arrays (e.g. "examination", "staff",
"drugs") are not tested this way. To fuzz a nested field, open its .req file and put a '*' right after
the value you want sqlmap to mutate, then run sqlmap -r manually.

The suspend/activate steps ARE now included and actually executed against the live server (they're part
of the 89), but only against the throwaway provider/user accounts this run itself creates - never a real
account - so permanently suspending them is harmless.

Usage: sqlmap_gen.py <payload_dir> <base_url> <out_dir>
Writes: <out_dir>/requests/<step-id>.req  and  <out_dir>/manifest.tsv (id<TAB>method<TAB>path<TAB>status)
Exit code: 0 if every non-skipped step got its expected status, 1 otherwise (still writes everything it can).
"""
import json
import os
import re
import sys
import urllib.error
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "docs"))
from steps import build_steps  # noqa: E402

VAR_RE = re.compile(r"\{\{([A-Z_]+)\}\}")


class Missing(Exception):
    def __init__(self, name):
        super().__init__("missing " + name)
        self.name = name


def sub(text, local, state):
    if text is None:
        return None

    def repl(m):
        name = m.group(1)
        if local is not None and name in local:
            return str(local[name])
        if name in state:
            return str(state[name])
        raise Missing(name)

    return VAR_RE.sub(repl, text)


def request(base_url, method, path, query, headers, body):
    url = base_url + path + (("?" + query) if query else "")
    data = body.encode("utf-8") if body is not None else None
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            text = resp.read().decode("utf-8", "replace")
            return resp.status, text, url
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace"), url
    except urllib.error.URLError as e:
        return 0, str(e), url


def write_req_file(path, method, url_path_and_query, host, headers, body):
    with open(path, "w", newline="") as f:
        f.write("%s %s HTTP/1.1\r\n" % (method, url_path_and_query))
        f.write("Host: %s\r\n" % host)
        for k, v in headers.items():
            f.write("%s: %s\r\n" % (k, v))
        f.write("\r\n")
        if body is not None:
            f.write(body)


def main():
    if len(sys.argv) != 4:
        print("usage: sqlmap_gen.py <payload_dir> <base_url> <out_dir>", file=sys.stderr)
        return 2
    payload_dir, base_url, out_dir = sys.argv[1:4]
    base_url = base_url.rstrip("/")
    host = re.sub(r"^https?://", "", base_url).split("/", 1)[0]
    api_prefix = base_url[len("http://" + host):] if base_url.startswith("http://" + host) else \
        base_url[len("https://" + host):]

    req_dir = os.path.join(out_dir, "requests")
    os.makedirs(req_dir, exist_ok=True)
    manifest_path = os.path.join(out_dir, "manifest.tsv")

    steps = build_steps(payload_dir)
    run = str(abs(hash(os.urandom(8))))[-6:]
    state = {
        "RUN": run, "PROVIDER_USERNAME": "provider" + run, "PROVIDER_PASSWORD": "1234@Abc!",
        "USER_USERNAME": "user" + run, "USER_PASSWORD": "123Mm@p0",
        "DISP_PROVIDER": "providerx" + run, "DISP_USER": "userx" + run,
    }

    manifest = []
    ok_count = 0
    fail_count = 0
    skip_count = 0

    for step in steps:
        local = {}
        try:
            for k, t in (step["set"] or {}).items():
                local[k] = sub(t, local, state)
            path = sub(step["path"], local, state)
            query = sub(step["query"], local, state) if step["query"] else None
            headers = {}
            body = None
            if step["auth"]:
                headers["Authorization"] = "Bearer " + state[step["auth"]]
            if step["body"] == "@empty":
                headers["Content-Type"] = "application/json"
            elif step["body"]:
                headers["Content-Type"] = "application/json"
                raw = sub(step["body"], local, state)
                body = json.dumps(json.loads(raw), separators=(",", ":"))
        except Missing as m:
            print("  skip  %-7s %-45s (missing %s)" % (step["method"], step["path"], m.name))
            skip_count += 1
            continue
        except (ValueError, KeyError) as e:
            print("  skip  %-7s %-45s (%s)" % (step["method"], step["path"], e))
            skip_count += 1
            continue

        full_path = api_prefix + path + (("?" + query) if query else "")
        fname = os.path.join(req_dir, step["id"] + ".req")
        write_req_file(fname, step["method"], full_path, host, headers, body)
        manifest.append((step["id"], step["method"], step["path"]))

        status, text, url = request(base_url, step["method"], path, query, headers, body)
        try:
            resp_json = json.loads(text)
        except ValueError:
            resp_json = None

        mark = "ok"
        if status == step["expect"]:
            ok_count += 1
            if status // 100 == 2 or step["expect"] != 200:
                for var, jkey in (step["saves"] or {}).items():
                    if isinstance(resp_json, dict) and jkey in resp_json and resp_json[jkey] is not None:
                        state[var] = str(resp_json[jkey])
                for var in step["clears"] or []:
                    state.pop(var, None)
        else:
            mark = "FAIL(%s!=%s)" % (status, step["expect"])
            fail_count += 1
        print("  %-14s %-7s %-45s -> %s" % (mark, step["method"], step["path"], status))

    with open(manifest_path, "w") as f:
        for sid, method, path in manifest:
            f.write("%s\t%s\t%s\n" % (sid, method, path))

    print()
    print("Executed: %d ok, %d failed, %d skipped. %d of 89 steps captured for sqlmap."
          % (ok_count, fail_count, skip_count, len(manifest)))
    return 0 if fail_count == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
