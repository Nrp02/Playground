from typing import List, Sequence

from . import gf256

Matrix = List[List[int]]


def identity(n: int) -> Matrix:
    return [[1 if i == j else 0 for j in range(n)] for i in range(n)]


def submatrix_rows(m: Matrix, indices: Sequence[int]) -> Matrix:
    return [list(m[i]) for i in indices]


def augment(a: Matrix, b: Matrix) -> Matrix:
    if len(a) != len(b):
        raise ValueError("row count mismatch in augment")
    return [list(a[i]) + list(b[i]) for i in range(len(a))]


def split_cols(m: Matrix, left_width: int) -> "tuple[Matrix, Matrix]":
    left = [row[:left_width] for row in m]
    right = [row[left_width:] for row in m]
    return left, right


def multiply(a: Matrix, b: Matrix) -> Matrix:
    rows_a = len(a)
    cols_a = len(a[0]) if rows_a else 0
    rows_b = len(b)
    cols_b = len(b[0]) if rows_b else 0
    if cols_a != rows_b:
        raise ValueError("incompatible matrix dimensions for multiply")
    result = [[0] * cols_b for _ in range(rows_a)]
    for i in range(rows_a):
        row_a = a[i]
        result_row = result[i]
        for k in range(cols_a):
            coefficient = row_a[k]
            if coefficient == 0:
                continue
            row_b = b[k]
            for j in range(cols_b):
                result_row[j] ^= gf256.mul(coefficient, row_b[j])
    return result


def invert(m: Matrix) -> Matrix:
    n = len(m)
    for row in m:
        if len(row) != n:
            raise ValueError("invert requires a square matrix")
    work = augment(m, identity(n))
    for col in range(n):
        pivot_row = None
        for r in range(col, n):
            if work[r][col] != 0:
                pivot_row = r
                break
        if pivot_row is None:
            raise ValueError("matrix is singular")
        if pivot_row != col:
            work[col], work[pivot_row] = work[pivot_row], work[col]
        pivot_inv = gf256.inv(work[col][col])
        if pivot_inv != 1:
            work[col] = [gf256.mul(pivot_inv, value) for value in work[col]]
        for r in range(n):
            if r == col:
                continue
            factor = work[r][col]
            if factor == 0:
                continue
            pivot_row_values = work[col]
            work[r] = [
                work[r][c] ^ gf256.mul(factor, pivot_row_values[c])
                for c in range(2 * n)
            ]
    _, inverse = split_cols(work, n)
    return inverse
