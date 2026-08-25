from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple, Union

TYPE_A = 1
TYPE_CNAME = 5
CLASS_IN = 1

RCODE_NOERROR = 0
RCODE_NXDOMAIN = 3

OPCODE_QUERY = 0


class DNSFormatError(Exception):
    pass


def encode_name(name: str, compression: Optional[Dict[str, int]], base_offset: int) -> bytes:
    name = name.rstrip(".")
    labels = name.split(".") if name else []
    out = bytearray()
    remaining = ".".join(labels)
    for i, label in enumerate(labels):
        suffix = ".".join(labels[i:])
        if compression is not None and suffix in compression:
            pointer = compression[suffix]
            out += struct.pack("!H", 0xC000 | pointer)
            return bytes(out)
        if compression is not None and base_offset + len(out) <= 0x3FFF:
            compression[suffix] = base_offset + len(out)
        encoded_label = label.encode("ascii")
        if len(encoded_label) > 63:
            raise DNSFormatError(f"label too long: {label}")
        out.append(len(encoded_label))
        out += encoded_label
    out.append(0)
    return bytes(out)


def decode_name(data: bytes, offset: int) -> Tuple[str, int]:
    labels: List[str] = []
    visited: set = set()
    original_offset = offset
    jumped = False
    cur = offset
    while True:
        if cur >= len(data):
            raise DNSFormatError("name extends past end of message")
        length = data[cur]
        if length == 0:
            cur += 1
            break
        if length & 0xC0 == 0xC0:
            if cur + 1 >= len(data):
                raise DNSFormatError("truncated pointer")
            pointer = ((length & 0x3F) << 8) | data[cur + 1]
            if pointer in visited:
                raise DNSFormatError("pointer loop detected")
            visited.add(pointer)
            if not jumped:
                original_offset = cur + 2
                jumped = True
            cur = pointer
            continue
        if length & 0xC0 != 0:
            raise DNSFormatError("invalid label length byte")
        cur += 1
        if cur + length > len(data):
            raise DNSFormatError("label extends past end of message")
        labels.append(data[cur:cur + length].decode("ascii"))
        cur += length
    final_offset = cur if not jumped else original_offset
    return ".".join(labels), final_offset


@dataclass
class Question:
    qname: str
    qtype: int
    qclass: int = CLASS_IN


@dataclass
class ResourceRecord:
    name: str
    rtype: int
    ttl: int
    rdata: Union[str, bytes]
    rclass: int = CLASS_IN


@dataclass
class DNSMessage:
    id: int
    qr: int
    opcode: int
    aa: int
    tc: int
    rd: int
    ra: int
    rcode: int
    questions: List[Question] = field(default_factory=list)
    answers: List[ResourceRecord] = field(default_factory=list)

    def flags(self) -> int:
        return (
            (self.qr & 0x1) << 15
            | (self.opcode & 0xF) << 11
            | (self.aa & 0x1) << 10
            | (self.tc & 0x1) << 9
            | (self.rd & 0x1) << 8
            | (self.ra & 0x1) << 7
            | (self.rcode & 0xF)
        )

    def encode(self) -> bytes:
        header = struct.pack(
            "!HHHHHH",
            self.id,
            self.flags(),
            len(self.questions),
            len(self.answers),
            0,
            0,
        )
        out = bytearray(header)
        compression: Dict[str, int] = {}
        for question in self.questions:
            out += encode_name(question.qname, compression, len(out))
            out += struct.pack("!HH", question.qtype, question.qclass)
        for rr in self.answers:
            out += encode_name(rr.name, compression, len(out))
            rdata = _encode_rdata(rr, compression, len(out) + 10)
            out += struct.pack("!HHIH", rr.rtype, rr.rclass, rr.ttl, len(rdata))
            out += rdata
        return bytes(out)

    @staticmethod
    def decode(data: bytes) -> "DNSMessage":
        if len(data) < 12:
            raise DNSFormatError("message shorter than header")
        msg_id, flags, qdcount, ancount, _nscount, _arcount = struct.unpack("!HHHHHH", data[:12])
        qr = (flags >> 15) & 0x1
        opcode = (flags >> 11) & 0xF
        aa = (flags >> 10) & 0x1
        tc = (flags >> 9) & 0x1
        rd = (flags >> 8) & 0x1
        ra = (flags >> 7) & 0x1
        rcode = flags & 0xF
        offset = 12
        questions: List[Question] = []
        for _ in range(qdcount):
            qname, offset = decode_name(data, offset)
            qtype, qclass = struct.unpack("!HH", data[offset:offset + 4])
            offset += 4
            questions.append(Question(qname, qtype, qclass))
        answers: List[ResourceRecord] = []
        for _ in range(ancount):
            name, offset = decode_name(data, offset)
            rtype, rclass, ttl, rdlength = struct.unpack("!HHIH", data[offset:offset + 10])
            offset += 10
            rdata_bytes = data[offset:offset + rdlength]
            offset += rdlength
            answers.append(ResourceRecord(name, rtype, ttl, _decode_rdata(rtype, rdata_bytes, data, offset - rdlength), rclass))
        return DNSMessage(msg_id, qr, opcode, aa, tc, rd, ra, rcode, questions, answers)


def _encode_rdata(rr: ResourceRecord, compression: Dict[str, int], rdata_offset: int) -> bytes:
    if rr.rtype == TYPE_A:
        assert isinstance(rr.rdata, str)
        return bytes(int(part) for part in rr.rdata.split("."))
    if rr.rtype == TYPE_CNAME:
        assert isinstance(rr.rdata, str)
        return encode_name(rr.rdata, compression, rdata_offset)
    assert isinstance(rr.rdata, bytes)
    return rr.rdata


def _decode_rdata(rtype: int, rdata: bytes, full_message: bytes, rdata_offset: int) -> Union[str, bytes]:
    if rtype == TYPE_A:
        if len(rdata) != 4:
            raise DNSFormatError("A record rdata must be 4 bytes")
        return ".".join(str(b) for b in rdata)
    if rtype == TYPE_CNAME:
        name, _ = decode_name(full_message, rdata_offset)
        return name
    return rdata
