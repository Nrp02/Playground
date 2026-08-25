from __future__ import annotations

from dns.client import resolve
from dns.server import DNSServer
from dns.wire import TYPE_A


def build_zone() -> dict:
    return {
        "example.com": [("A", "93.184.216.34")],
        "www.example.com": [("CNAME", "example.com")],
        "mail.example.com": [("A", "10.0.0.5"), ("A", "10.0.0.6")],
        "blog.example.com": [("CNAME", "www.example.com")],
    }


def main() -> None:
    server = DNSServer(zone=build_zone())
    server.start()
    address = (server.host, server.port)
    print(f"authoritative server listening on {address}")

    result = resolve(address, "example.com", TYPE_A)
    print(f"example.com -> rcode={result.rcode} ips={result.ips}")

    result = resolve(address, "mail.example.com", TYPE_A)
    print(f"mail.example.com -> rcode={result.rcode} ips={result.ips}")

    result = resolve(address, "blog.example.com", TYPE_A)
    print(
        f"blog.example.com -> rcode={result.rcode} "
        f"cname_chain={result.cname_chain} ips={result.ips}"
    )

    result = resolve(address, "nowhere.example.com", TYPE_A)
    print(f"nowhere.example.com -> rcode={result.rcode} is_nxdomain={result.is_nxdomain}")

    server.stop()
    print("server stopped")


if __name__ == "__main__":
    main()
