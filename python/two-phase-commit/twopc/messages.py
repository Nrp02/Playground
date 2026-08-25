from enum import Enum, auto


class Vote(Enum):
    COMMIT = auto()
    ABORT = auto()


class Decision(Enum):
    COMMIT = auto()
    ABORT = auto()
