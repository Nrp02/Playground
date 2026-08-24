from __future__ import annotations

from typing import Dict, List, TYPE_CHECKING

from .message import Message

if TYPE_CHECKING:
    from .broker import Broker


class Consumer:
    def __init__(self, broker: "Broker", topic_name: str, group_id: str):
        self.broker = broker
        self.topic_name = topic_name
        self.group_id = group_id
        self._group = broker.group_for(topic_name, group_id)
        self._read_positions: Dict[int, int] = dict(self._group.committed_offsets)

    def poll(self, partition_index: int, max_messages: int = 100) -> List[Message]:
        topic = self.broker.get_topic(self.topic_name)
        start = self._read_positions.get(partition_index, self._group.committed(partition_index))
        messages = topic.partitions[partition_index].read_from(start, max_messages)
        if messages:
            self._read_positions[partition_index] = messages[-1].offset + 1
        return messages

    def poll_all_partitions(self, max_messages: int = 100) -> Dict[int, List[Message]]:
        topic = self.broker.get_topic(self.topic_name)
        result: Dict[int, List[Message]] = {}
        for partition_index in range(topic.num_partitions):
            messages = self.poll(partition_index, max_messages)
            if messages:
                result[partition_index] = messages
        return result

    def commit(self, partition_index: int) -> None:
        offset = self._read_positions.get(partition_index, self._group.committed(partition_index))
        self._group.commit(partition_index, offset)

    def commit_all(self) -> None:
        for partition_index, position in self._read_positions.items():
            self._group.commit(partition_index, position)
