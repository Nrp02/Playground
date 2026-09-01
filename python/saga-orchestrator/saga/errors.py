class TransientError(Exception):
    pass


class TerminalError(Exception):
    pass


class SagaCrashed(Exception):
    def __init__(self, saga_id: str) -> None:
        super().__init__(saga_id)
        self.saga_id = saga_id
