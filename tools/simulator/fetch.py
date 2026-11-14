#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""The simulator's network: requests in, answers out, one JSON object a line.

The simulator runs this once and keeps it; Python brings HTTPS on every
system, so the simulator itself needs no HTTP library.

Request:  {"method", "url", "headers": {name: value}, "body": base64,
           "follow": bool, "max": bytes, "timeout": seconds}
Answer:   {"status": HTTP status, or a HOST_FAILED_* code, "headers": {name: value},
           "body": base64}
"""
import base64
import json
import socket
import ssl
import sys
import urllib.error
import urllib.request

# sdk/host_api.h
NO_ANSWER = -2
TIMEOUT = -5
NOT_TRUSTED = -6
TOO_LARGE = -7
REDIRECTS_MAX = 3
KEPT_HEADERS = ("content-type", "content-length", "etag", "last-modified", "location", "link",
                "retry-after")


class Redirects(urllib.request.HTTPRedirectHandler):
    """A GET is asked again where a redirect says, three times at most; any other
    method takes the redirect as its answer."""
    max_redirections = REDIRECTS_MAX

    def __init__(self, follow):
        self.follow = follow

    def redirect_request(self, request, file, code, message, headers, new_url):
        if not self.follow:
            return None
        return super().redirect_request(request, file, code, message, headers, new_url)


def kept(headers):
    return {name.lower(): value for name, value in headers.items() if name.lower() in KEPT_HEADERS}


def read_limited(response, most):
    body = response.read(most + 1)
    return None if len(body) > most else body


def fetch(asked):
    body = base64.b64decode(asked.get("body") or "")
    request = urllib.request.Request(asked["url"], data=body or None, method=asked["method"],
                                     headers=asked.get("headers") or {})
    opener = urllib.request.build_opener(Redirects(asked.get("follow", True)))
    most = asked.get("max", 256 * 1024)
    try:
        with opener.open(request, timeout=asked.get("timeout", 15)) as response:
            content = read_limited(response, most)
            if content is None:
                return {"status": TOO_LARGE}
            return {"status": response.status, "headers": kept(response.headers),
                    "body": base64.b64encode(content).decode()}
    except urllib.error.HTTPError as answer:
        content = read_limited(answer, most) or b""
        return {"status": answer.code, "headers": kept(answer.headers),
                "body": base64.b64encode(content).decode()}
    except urllib.error.URLError as failure:
        reason = failure.reason
        if isinstance(reason, ssl.SSLCertVerificationError):
            return {"status": NOT_TRUSTED}
        if isinstance(reason, (socket.timeout, TimeoutError)):
            return {"status": TIMEOUT}
        return {"status": NO_ANSWER}
    except (socket.timeout, TimeoutError):
        return {"status": TIMEOUT}
    except (OSError, ValueError):
        return {"status": NO_ANSWER}


def main():
    for line in sys.stdin:
        try:
            answer = fetch(json.loads(line))
        except Exception:  # an answer for every request, whatever went wrong
            answer = {"status": NO_ANSWER}
        sys.stdout.write(json.dumps(answer) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
