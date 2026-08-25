from __future__ import annotations

import random
import socket
from dataclasses import dataclass, field
from typing import List, Tuple

from dns.wire import (
    CLASS_IN,
    DNSMessage,
    OPCODE_QUERY,
    Question,
    RCODE_NOERROR,
    RCODE_NXDOMAIN,
    TYPE_A,
    TYPE_CNAME,
)


class DNSQueryTimeout(Exception):
    pass


@dataclass
class ResolveResult:
    rcode: int
    ips: List[str] = field(default_factory=list)
    cname_chain: List[str] = field(default_factory=list)

    @property
    def is_nxdomain(self) -> bool:
        return self.rcode == RCODE_NXDOMAIN


def build_query(qname: str, qtype: int = TYPE_A, msg_id: int | None = None) -> bytes:
    if msg_id is None:
        msg_id = random.randint(0, 0xFFFF)
    message = DNSMessage(
        id=msg_id,
        qr=0,
        opcode=OPCODE_QUERY,
        aa=0,
        tc=0,
        rd=1,
        ra=0,
        rcode=RCODE_NOERROR,
        questions=[Question(qname, qtype, CLASS_IN)],
    )
    return message.encode()


def resolve(server: Tuple[str, int], qname: str, qtype: int = TYPE_A, timeout: float = 2.0) -> ResolveResult:
    msg_id = random.randint(0, 0xFFFF)
    query_bytes = build_query(qname, qtype, msg_id)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    try:
        sock.sendto(query_bytes, server)
        try:
            data, _addr = sock.recvfrom(4096)
        except socket.timeout as exc:
            raise DNSQueryTimeout(f"no response from {server} for {qname}") from exc
    finally:
        sock.close()
    response = DNSMessage.decode(data)
    if response.id != msg_id:
        raise DNSQueryTimeout("mismatched response id")
    ips: List[str] = []
    cname_chain: List[str] = []
    for rr in response.answers:
        if rr.rtype == TYPE_A:
            ips.append(str(rr.rdata))
        elif rr.rtype == TYPE_CNAME:
            cname_chain.append(str(rr.rdata))
    return ResolveResult(response.rcode, ips, cname_chain)
