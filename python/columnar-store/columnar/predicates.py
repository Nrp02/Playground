from typing import Any, Optional


class Predicate:
    def __init__(
        self,
        column: str,
        op: str,
        value: Optional[Any] = None,
        low: Optional[Any] = None,
        high: Optional[Any] = None,
    ) -> None:
        valid_ops = ("==", "!=", ">", ">=", "<", "<=", "between")
        if op not in valid_ops:
            raise ValueError(f"unsupported op: {op}")
        self.column = column
        self.op = op
        self.value = value
        self.low = low
        self.high = high

    def can_skip_zone(self, min_value: Any, max_value: Any, null_count: int, num_rows: int) -> bool:
        if null_count >= num_rows:
            return True
        if min_value is None or max_value is None:
            return True
        op = self.op
        if op == "==":
            return self.value < min_value or self.value > max_value
        if op == "!=":
            return False
        if op == ">":
            return max_value <= self.value
        if op == ">=":
            return max_value < self.value
        if op == "<":
            return min_value >= self.value
        if op == "<=":
            return min_value > self.value
        if op == "between":
            return max_value < self.low or min_value > self.high
        raise ValueError(f"unsupported op: {op}")

    def matches(self, value: Any) -> bool:
        if value is None:
            return False
        op = self.op
        if op == "==":
            return value == self.value
        if op == "!=":
            return value != self.value
        if op == ">":
            return value > self.value
        if op == ">=":
            return value >= self.value
        if op == "<":
            return value < self.value
        if op == "<=":
            return value <= self.value
        if op == "between":
            return self.low <= value <= self.high
        raise ValueError(f"unsupported op: {op}")
