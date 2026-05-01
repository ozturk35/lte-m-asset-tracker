#!/usr/bin/env python3
"""
Provision ISRG Root X1 CA certificate to nRF9151 modem security tag 1.
Sends AT%CMNG=0,1,0,"<cert>" via the AT_HOST library over RFC2217 proxy.

Usage:
  python3 provision_cert.py [--port rfc2217://localhost:4002]
"""

import argparse
import serial
import time
import sys

RFC2217_URL      = "rfc2217://localhost:4002"
BAUD_RATE        = 115200
SEC_TAG          = 1
CERT_TYPE        = 0      # CA certificate
RESPONSE_TIMEOUT = 30.0   # seconds

ISRG_ROOT_X1_PEM = """\
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----"""


def _collect_response(s: serial.Serial, timeout: float) -> list[str]:
    """Read response lines until OK/ERROR or timeout."""
    lines    = []
    buf      = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        chunk = s.read(s.in_waiting or 1)
        if chunk:
            buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", errors="replace").strip()
            if text:
                lines.append(text)
                if text in ("OK", "ERROR") or text.startswith("+CME ERROR"):
                    return lines
        time.sleep(0.02)
    return lines


def send_at(s: serial.Serial, cmd: str, timeout: float = 5.0) -> list[str]:
    """Send an AT command; collect and return response lines."""
    s.reset_input_buffer()
    s.write((cmd + "\r\n").encode())
    s.flush()

    s.reset_input_buffer()
    s.write((cmd + "\r\n").encode())
    s.flush()
    resp = _collect_response(s, timeout)
    # Strip echoed command if present
    return [l for l in resp if l != cmd]


def ok(resp: list[str]) -> bool:
    return "OK" in resp


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=RFC2217_URL)
    args = parser.parse_args()

    print(f"Connecting to {args.port} …")
    s = serial.serial_for_url(args.port, baudrate=BAUD_RATE, timeout=0.5)
    time.sleep(1.5)
    s.reset_input_buffer()

    # 1. Sanity check
    print("\n[1] AT echo test …")
    resp = send_at(s, "AT")
    print(f"    → {resp}")
    if not ok(resp):
        print("FAIL: no OK — is AT_HOST running?")
        sys.exit(1)

    # 2. Enable extended error reporting
    send_at(s, "AT+CMEE=1")

    # 3. Offline mode (required for credential operations on some modem FW)
    print("\n[2] Setting modem offline (AT+CFUN=4) …")
    resp = send_at(s, "AT+CFUN=4", timeout=15.0)
    print(f"    → {resp}")
    time.sleep(2.0)  # modem needs a moment to settle

    # 4. List existing credentials at tag 1
    print(f"\n[3] Listing credentials at security tag {SEC_TAG} …")
    resp = send_at(s, f"AT%CMNG=1,{SEC_TAG}", timeout=10.0)
    print(f"    → {resp}")

    # 5. Delete existing CA cert at tag 1 (ignore error if not present)
    print(f"\n[4] Deleting existing CA cert at tag {SEC_TAG} (if any) …")
    resp = send_at(s, f"AT%CMNG=3,{SEC_TAG},{CERT_TYPE}", timeout=10.0)
    print(f"    → {resp}")
    time.sleep(0.5)

    # 6. Write cert — PEM lines joined with actual CRLF bytes inside the quoted string.
    #    AT_HOST tracks inside_quotes so embedded CR+LF does not terminate the command.
    cert_inline = "\r\n".join(ISRG_ROOT_X1_PEM.splitlines()) + "\r\n"
    cmng_cmd = f'AT%CMNG=0,{SEC_TAG},{CERT_TYPE},"{cert_inline}"'
    print(f"\n[5] Writing CA cert ({len(cmng_cmd)} byte command) …")
    # Send manually: cert body + closing quote + CRLF terminator
    s.reset_input_buffer()
    s.write((cmng_cmd + "\r\n").encode())
    s.flush()
    resp = _collect_response(s, RESPONSE_TIMEOUT)
    print(f"    → {resp}")

    if not ok(resp):
        # Extended error
        err_resp = send_at(s, "AT+CEER", timeout=5.0)
        print(f"    CEER: {err_resp}")
        print("\nFAIL: AT%CMNG write returned error")
        send_at(s, "AT+CFUN=1", timeout=15.0)
        s.close()
        sys.exit(1)

    # 7. Verify cert is stored
    print(f"\n[6] Verifying cert at tag {SEC_TAG} …")
    resp = send_at(s, f"AT%CMNG=2,{SEC_TAG},{CERT_TYPE}", timeout=10.0)
    # Response is the cert content — just print first/last lines
    print(f"    Lines returned: {len(resp)}")
    if resp:
        print(f"    First: {resp[0][:80]}")
        print(f"    Last:  {resp[-1][:80]}")

    # 8. Restore normal mode
    print("\n[7] Restoring modem to normal mode (AT+CFUN=1) …")
    resp = send_at(s, "AT+CFUN=1", timeout=15.0)
    print(f"    → {resp}")

    s.close()
    print("\nDone — ISRG Root X1 provisioned to security tag 1.")


if __name__ == "__main__":
    main()
