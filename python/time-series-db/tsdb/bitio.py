from typing import List


class BitWriter:
    def __init__(self) -> None:
        self._bytes = bytearray()
        self._bitpos = 0

    def write_bit(self, bit: int) -> None:
        if self._bitpos == 0:
            self._bytes.append(0)
        if bit:
            self._bytes[-1] |= 1 << (7 - self._bitpos)
        self._bitpos = (self._bitpos + 1) % 8

    def write_bits(self, value: int, nbits: int) -> None:
        for i in range(nbits - 1, -1, -1):
            self.write_bit((value >> i) & 1)

    def write_int(self, value: int, nbits: int) -> None:
        masked = value & ((1 << nbits) - 1)
        self.write_bits(masked, nbits)

    def getvalue(self) -> bytes:
        return bytes(self._bytes)


class BitReader:
    def __init__(self, data: bytes) -> None:
        self._data = data
        self._pos = 0

    def read_bit(self) -> int:
        byte_idx = self._pos // 8
        bit_idx = self._pos % 8
        bit = (self._data[byte_idx] >> (7 - bit_idx)) & 1
        self._pos += 1
        return bit

    def read_bits(self, n: int) -> int:
        value = 0
        for _ in range(n):
            value = (value << 1) | self.read_bit()
        return value

    def read_int(self, nbits: int) -> int:
        value = self.read_bits(nbits)
        if value >= (1 << (nbits - 1)):
            value -= 1 << nbits
        return value
