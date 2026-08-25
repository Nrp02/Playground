from __future__ import annotations

import socket
import threading
from dataclasses import dataclass
from typing import Dict, List, Tuple

from dns.wire import (
    DNSFormatError,
    DNSMessage,
    OPCODE_QUERY,
    RCODE_NOERROR,
    RCODE_NXDOMAIN,
    ResourceRecord,
    TYPE_A,
    TYPE_CNAME,
)

ZoneRecord = Tuple[str, str]
Zone = Dict[str, List[ZoneRecord]]

MAX_CHAIN_HOPS = 8


def resolve_in_zone(zone: Zone, qname: str, qtype: int) -> Tuple[List[ResourceRecord], bool]:
    qname = qname.rstrip(".")
    if qname not in zone:
        return [], False
    answers: List[ResourceRecord] = []
    current = qname
    visited = set()
    hops = 0
    while hops < MAX_CHAIN_HOPS:
        hops += 1
        if current in visited:
            break
        visited.add(current)
        records = zone.get(current)
        if records is None:
            break
        a_records = [value for kind, value in records if kind == "A"]
        cname_records = [value for kind, value in records if kind == "CNAME"]
        if a_records and qtype == TYPE_A:
            for ip in a_records:
                answers.append(ResourceRecord(current, TYPE_A, 300, ip))
            return answers, True
        if cname_records:
            target = cname_records[0]
            answers.append(ResourceRecord(current, TYPE_CNAME, 300, target))
            current = target
            continue
        break
    return answers, True


@dataclass
class DNSServer:
    zone: Zone
    host: str = "127.0.0.1"
    port: int = 0

    def __post_init__(self) -> None:
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.bind((self.host, self.port))
        self.port = self._socket.getsockname()[1]
        self._thread: threading.Thread | None = None
        self._running = False

    def start(self) -> None:
        self._running = True
        self._thread = threading.Thread(target=self._serve_loop, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        try:
            self._socket.close()
        except OSError:
            pass
        if self._thread is not None:
            self._thread.join(timeout=2.0)

    def _serve_loop(self) -> None:
        while self._running:
            try:
                data, addr = self._socket.recvfrom(4096)
            except OSError:
                break
            try:
                response = self._handle_query(data)
            except DNSFormatError:
                continue
            try:
                self._socket.sendto(response, addr)
            except OSError:
                break

    def _handle_query(self, data: bytes) -> bytes:
        query = DNSMessage.decode(data)
        question = query.questions[0]
        answers, found = resolve_in_zone(self.zone, question.qname, question.qtype)
        rcode = RCODE_NOERROR if found else RCODE_NXDOMAIN
        response = DNSMessage(
            id=query.id,
            qr=1,
            opcode=OPCODE_QUERY,
            aa=1,
            tc=0,
            rd=query.rd,
            ra=0,
            rcode=rcode,
            questions=[question],
            answers=answers,
        )
        return response.encode()
