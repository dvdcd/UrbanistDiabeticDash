#!/usr/bin/env python3
"""
Dexcom Share API debug script — mirrors exactly what dexcom_source.cpp does.

Usage:
    python3 debug_dexcom.py <username> <password> [--ous]

    --ous  Use the outside-US endpoint (shareous1.dexcom.com)
           Default is US (share2.dexcom.com)
"""

import sys
import json
import urllib.request
import urllib.error

APP_ID = "d89443d2-327c-4a6f-89e5-496bbb0317db"


def post(url, body):
    data = json.dumps(body).encode()
    req = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json", "Accept": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        sys.exit(1)

    username, password = args[0], args[1]
    ous = "--ous" in args
    base = "https://shareous1.dexcom.com" if ous else "https://share2.dexcom.com"
    print(f"Endpoint: {base}\n")

    # ── Step 1: AuthenticatePublisherAccount ─────────────────────────────────
    print("── Step 1: AuthenticatePublisherAccount ──")
    url1 = base + "/ShareWebServices/Services/General/AuthenticatePublisherAccount"
    body1 = {"accountName": username, "password": password, "applicationId": APP_ID}
    print(f"POST {url1}")
    print(f"Body: {json.dumps(body1, indent=2)}")
    code1, resp1 = post(url1, body1)
    print(f"HTTP {code1}")
    print(f"Response: {resp1}\n")

    if code1 != 200:
        print("FAILED at step 1 — check username, password, and region (try --ous)")
        sys.exit(1)

    account_id = resp1.strip().strip('"')
    print(f"account_id: {account_id}\n")

    # ── Step 2: LoginPublisherAccountById ────────────────────────────────────
    print("── Step 2: LoginPublisherAccountById ──")
    url2 = base + "/ShareWebServices/Services/General/LoginPublisherAccountById"
    body2 = {"accountId": account_id, "password": password, "applicationId": APP_ID}
    print(f"POST {url2}")
    print(f"Body: {json.dumps(body2, indent=2)}")
    code2, resp2 = post(url2, body2)
    print(f"HTTP {code2}")
    print(f"Response: {resp2}\n")

    if code2 != 200:
        print("FAILED at step 2")
        sys.exit(1)

    session_id = resp2.strip().strip('"')
    print(f"session_id: {session_id}\n")

    # ── Step 3: ReadPublisherLatestGlucoseValues ──────────────────────────────
    print("── Step 3: ReadPublisherLatestGlucoseValues ──")
    url3 = (
        base
        + "/ShareWebServices/Services/Publisher/ReadPublisherLatestGlucoseValues"
        + f"?sessionId={session_id}&minutes=60&maxCount=12"
    )
    print(f"POST {url3}")
    code3, resp3 = post(url3, {})
    print(f"HTTP {code3}")
    try:
        readings = json.loads(resp3)
        print(f"Response ({len(readings)} readings):")
        for r in readings:
            print(f"  sgv={r.get('Value')}  trend={r.get('Trend')}  wt={r.get('WT')}")
    except Exception:
        print(f"Response: {resp3}")

    if code3 != 200:
        print("FAILED at step 3")
        sys.exit(1)

    print("\nAll 3 steps succeeded — Dexcom auth is working.")


if __name__ == "__main__":
    main()
