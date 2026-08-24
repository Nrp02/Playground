from __future__ import annotations

from typing import Any, Dict, Optional, Tuple

from .consumer import Consumer
from .consumer_group import ConsumerGroup
from .topic import Topic


class Broker:
    def __init__(self):
        self.topics: Dict[str, Topic] = {}
        self._consumer_groups: Dict[Tuple[str, str], ConsumerGroup] = {}

    def create_topic(self, name: str, num_partitions: int = 1) -> Topic:
        if name in self.topics:
            raise ValueError(f"topic already exists: {name}")
        topic = Topic(name, num_partitions)
        self.topics[name] = topic
        return topic

    def get_topic(self, name: str) -> Topic:
        return self.topics[name]

    def produce(self, topic_name: str, value: Any, key: Optional[str] = None) -> Tuple[int, int]:
        return self.topics[topic_name].append(value, key=key)

    def group_for(self, topic_name: str, group_id: str) -> ConsumerGroup:
        key = (topic_name, group_id)
        if key not in self._consumer_groups:
            num_partitions = self.topics[topic_name].num_partitions
            self._consumer_groups[key] = ConsumerGroup(group_id, num_partitions)
        return self._consumer_groups[key]

    def create_consumer(self, topic_name: str, group_id: str) -> Consumer:
        return Consumer(self, topic_name, group_id)
