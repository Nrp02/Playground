from typing import List

PRIMITIVE_POLY = 0x11D

EXP: List[int] = [0] * 512
LOG: List[int] = [0] * 256


def _build_tables() -> None:
    x = 1
    for i in range(255):
        EXP[i] = x
        LOG[x] = i
        x <<= 1
        if x & 0x100:
            x ^= PRIMITIVE_POLY
    for i in range(255, 512):
        EXP[i] = EXP[i - 255]


_build_tables()


def add(a: int, b: int) -> int:
    return a ^ b


sub = add


def mul(a: int, b: int) -> int:
    if a == 0 or b == 0:
        return 0
    return EXP[LOG[a] + LOG[b]]


def div(a: int, b: int) -> int:
    if b == 0:
        raise ZeroDivisionError("division by zero in GF(2^8)")
    if a == 0:
        return 0
    return EXP[(LOG[a] - LOG[b]) % 255]


def inv(a: int) -> int:
    if a == 0:
        raise ZeroDivisionError("zero has no inverse in GF(2^8)")
    return EXP[(255 - LOG[a]) % 255]


def pow_(a: int, n: int) -> int:
    if n == 0:
        return 1
    if a == 0:
        return 0
    return EXP[(LOG[a] * n) % 255]


def mul_table(coefficient: int) -> bytes:
    if coefficient == 0:
        return bytes(256)
    if coefficient == 1:
        return bytes(range(256))
    log_c = LOG[coefficient]
    table = bytearray(256)
    for value in range(1, 256):
        table[value] = EXP[log_c + LOG[value]]
    return bytes(table)
