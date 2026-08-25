import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from dns.client import build_query, resolve
from dns.server import DNSServer, resolve_in_zone
from dns.wire import (
    CLASS_IN,
    DNSMessage,
    OPCODE_QUERY,
    Question,
    RCODE_NOERROR,
    RCODE_NXDOMAIN,
    ResourceRecord,
    TYPE_A,
    TYPE_CNAME,
    decode_name,
    encode_name,
)


class TestNameEncoding(unittest.TestCase):
    def test_round_trip_simple_name(self):
        encoded = encode_name("example.com", None, 0)
        name, offset = decode_name(encoded, 0)
        self.assertEqual(name, "example.com")
        self.assertEqual(offset, len(encoded))

    def test_root_name_encodes_to_single_zero_byte(self):
        encoded = encode_name("", None, 0)
        self.assertEqual(encoded, b"\x00")

    def test_compression_pointer_reused_for_repeated_name(self):
        compression = {}
        first = encode_name("example.com", compression, 12)
        second = encode_name("example.com", compression, 12 + len(first))
        self.assertEqual(len(second), 2)
        self.assertEqual(second[0] & 0xC0, 0xC0)

    def test_decode_follows_compression_pointer(self):
        compression = {}
        first = encode_name("example.com", compression, 12)
        second = encode_name("www.example.com", compression, 12 + len(first))
        buffer = b"\x00" * 12 + first + second
        name, offset = decode_name(buffer, 12 + len(first))
        self.assertEqual(name, "www.example.com")
        self.assertEqual(offset, len(buffer))


class TestMessageEncoding(unittest.TestCase):
    def test_query_round_trip(self):
        query = build_query("example.com", TYPE_A, msg_id=1234)
        decoded = DNSMessage.decode(query)
        self.assertEqual(decoded.id, 1234)
        self.assertEqual(decoded.qr, 0)
        self.assertEqual(decoded.rd, 1)
        self.assertEqual(len(decoded.questions), 1)
        self.assertEqual(decoded.questions[0].qname, "example.com")
        self.assertEqual(decoded.questions[0].qtype, TYPE_A)
        self.assertEqual(decoded.questions[0].qclass, CLASS_IN)

    def test_response_round_trip_with_a_and_cname_records(self):
        message = DNSMessage(
            id=42,
            qr=1,
            opcode=OPCODE_QUERY,
            aa=1,
            tc=0,
            rd=1,
            ra=0,
            rcode=RCODE_NOERROR,
            questions=[Question("www.example.com", TYPE_A)],
            answers=[
                ResourceRecord("www.example.com", TYPE_CNAME, 300, "example.com"),
                ResourceRecord("example.com", TYPE_A, 300, "93.184.216.34"),
            ],
        )
        encoded = message.encode()
        decoded = DNSMessage.decode(encoded)
        self.assertEqual(decoded.id, 42)
        self.assertEqual(decoded.rcode, RCODE_NOERROR)
        self.assertEqual(len(decoded.answers), 2)
        self.assertEqual(decoded.answers[0].rtype, TYPE_CNAME)
        self.assertEqual(decoded.answers[0].rdata, "example.com")
        self.assertEqual(decoded.answers[1].rtype, TYPE_A)
        self.assertEqual(decoded.answers[1].rdata, "93.184.216.34")

    def test_nxdomain_response_round_trip(self):
        message = DNSMessage(
            id=7,
            qr=1,
            opcode=OPCODE_QUERY,
            aa=1,
            tc=0,
            rd=1,
            ra=0,
            rcode=RCODE_NXDOMAIN,
            questions=[Question("nowhere.example.com", TYPE_A)],
            answers=[],
        )
        decoded = DNSMessage.decode(message.encode())
        self.assertEqual(decoded.rcode, RCODE_NXDOMAIN)
        self.assertEqual(len(decoded.answers), 0)


class TestResolveInZone(unittest.TestCase):
    def setUp(self):
        self.zone = {
            "example.com": [("A", "93.184.216.34")],
            "www.example.com": [("CNAME", "example.com")],
            "mail.example.com": [("A", "10.0.0.5"), ("A", "10.0.0.6")],
        }

    def test_direct_a_lookup(self):
        answers, found = resolve_in_zone(self.zone, "mail.example.com", TYPE_A)
        self.assertTrue(found)
        self.assertEqual({a.rdata for a in answers}, {"10.0.0.5", "10.0.0.6"})

    def test_cname_chain_resolves_to_a_record(self):
        answers, found = resolve_in_zone(self.zone, "www.example.com", TYPE_A)
        self.assertTrue(found)
        self.assertEqual(answers[0].rtype, TYPE_CNAME)
        self.assertEqual(answers[0].rdata, "example.com")
        self.assertEqual(answers[1].rtype, TYPE_A)
        self.assertEqual(answers[1].rdata, "93.184.216.34")

    def test_unknown_name_not_found(self):
        answers, found = resolve_in_zone(self.zone, "nowhere.example.com", TYPE_A)
        self.assertFalse(found)
        self.assertEqual(answers, [])


class TestLiveUDPServer(unittest.TestCase):
    def setUp(self):
        zone = {
            "example.com": [("A", "93.184.216.34")],
            "www.example.com": [("CNAME", "example.com")],
        }
        self.server = DNSServer(zone=zone)
        self.server.start()
        self.address = (self.server.host, self.server.port)

    def tearDown(self):
        self.server.stop()

    def test_resolve_a_record_over_udp(self):
        result = resolve(self.address, "example.com", TYPE_A)
        self.assertEqual(result.rcode, RCODE_NOERROR)
        self.assertEqual(result.ips, ["93.184.216.34"])

    def test_resolve_cname_chain_over_udp(self):
        result = resolve(self.address, "www.example.com", TYPE_A)
        self.assertEqual(result.rcode, RCODE_NOERROR)
        self.assertEqual(result.cname_chain, ["example.com"])
        self.assertEqual(result.ips, ["93.184.216.34"])

    def test_nxdomain_over_udp(self):
        result = resolve(self.address, "nowhere.example.com", TYPE_A)
        self.assertTrue(result.is_nxdomain)
        self.assertEqual(result.ips, [])


if __name__ == "__main__":
    unittest.main()
