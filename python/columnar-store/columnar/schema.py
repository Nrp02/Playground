from typing import Dict, List

COLUMN_TYPES = ("int", "float", "str", "bool")


class Schema:
    def __init__(self, columns: Dict[str, str]) -> None:
        for name, ctype in columns.items():
            if ctype not in COLUMN_TYPES:
                raise ValueError(f"unsupported column type: {ctype}")
        self.columns: Dict[str, str] = dict(columns)

    def names(self) -> List[str]:
        return list(self.columns.keys())

    def type_of(self, name: str) -> str:
        return self.columns[name]

    def to_dict(self) -> Dict[str, str]:
        return dict(self.columns)

    @staticmethod
    def from_dict(data: Dict[str, str]) -> "Schema":
        return Schema(data)

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, Schema):
            return NotImplemented
        return self.columns == other.columns
