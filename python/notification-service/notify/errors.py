from __future__ import annotations


class ChannelError(Exception):
    pass


class TransientChannelError(ChannelError):
    pass


class PermanentChannelError(ChannelError):
    pass
