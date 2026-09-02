from __future__ import annotations

from typing import Callable, Dict, Iterable, List, Optional, Set

from .ids import DEFAULT_BITS, bucket_index, distance


class Contact:
    __slots__ = ("node_id", "address", "last_seen")

    def __init__(self, node_id: int, address: str, last_seen: int = 0) -> None:
        self.node_id = node_id
        self.address = address
        self.last_seen = last_seen

    def copy(self) -> "Contact":
        return Contact(self.node_id, self.address, self.last_seen)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Contact) and other.node_id == self.node_id

    def __hash__(self) -> int:
        return hash(self.node_id)

    def __repr__(self) -> str:
        return "Contact({})".format(self.address)


class KBucket:
    def __init__(self, k: int) -> None:
        self.k = k
        self.contacts: List[Contact] = []

    def __len__(self) -> int:
        return len(self.contacts)

    def __iter__(self):
        return iter(self.contacts)

    def is_full(self) -> bool:
        return len(self.contacts) >= self.k

    def find(self, node_id: int) -> Optional[Contact]:
        for contact in self.contacts:
            if contact.node_id == node_id:
                return contact
        return None

    def least_recently_seen(self) -> Optional[Contact]:
        return self.contacts[0] if self.contacts else None

    def most_recently_seen(self) -> Optional[Contact]:
        return self.contacts[-1] if self.contacts else None

    def append(self, contact: Contact) -> None:
        self.contacts.append(contact)

    def promote(self, contact: Contact, last_seen: int) -> None:
        self.contacts.remove(contact)
        contact.last_seen = max(contact.last_seen, last_seen)
        self.contacts.append(contact)

    def remove(self, node_id: int) -> bool:
        existing = self.find(node_id)
        if existing is None:
            return False
        self.contacts.remove(existing)
        return True


PingFn = Callable[[Contact], bool]


class RoutingTable:
    def __init__(
        self,
        owner_id: int,
        k: int = 20,
        bits: int = DEFAULT_BITS,
        ping: Optional[PingFn] = None,
    ) -> None:
        self.owner_id = owner_id
        self.k = k
        self.bits = bits
        self.ping = ping
        self.buckets: Dict[int, KBucket] = {}

    def bucket_for(self, node_id: int) -> KBucket:
        index = bucket_index(self.owner_id, node_id, self.bits)
        bucket = self.buckets.get(index)
        if bucket is None:
            bucket = KBucket(self.k)
            self.buckets[index] = bucket
        return bucket

    def update(self, contact: Contact, now: int = 0) -> bool:
        if contact.node_id == self.owner_id:
            return False
        bucket = self.bucket_for(contact.node_id)
        existing = bucket.find(contact.node_id)
        if existing is not None:
            bucket.promote(existing, now)
            return True
        fresh = Contact(contact.node_id, contact.address, now)
        if not bucket.is_full():
            bucket.append(fresh)
            return True
        stale = bucket.least_recently_seen()
        if stale is None:
            bucket.append(fresh)
            return True
        if self.ping is None or self.ping(stale):
            if bucket.find(stale.node_id) is not None:
                bucket.promote(stale, now)
                return False
        bucket.remove(stale.node_id)
        if bucket.is_full():
            return False
        bucket.append(fresh)
        return True

    def remove(self, node_id: int) -> bool:
        try:
            index = bucket_index(self.owner_id, node_id, self.bits)
        except ValueError:
            return False
        bucket = self.buckets.get(index)
        if bucket is None:
            return False
        return bucket.remove(node_id)

    def contains(self, node_id: int) -> bool:
        try:
            index = bucket_index(self.owner_id, node_id, self.bits)
        except ValueError:
            return False
        bucket = self.buckets.get(index)
        return bucket is not None and bucket.find(node_id) is not None

    def all_contacts(self) -> List[Contact]:
        out: List[Contact] = []
        for index in sorted(self.buckets):
            out.extend(self.buckets[index].contacts)
        return out

    def closest(
        self,
        target: int,
        count: Optional[int] = None,
        exclude: Optional[Iterable[int]] = None,
    ) -> List[Contact]:
        skip: Set[int] = set(exclude) if exclude else set()
        pool = [c for c in self.all_contacts() if c.node_id not in skip]
        pool.sort(key=lambda c: distance(c.node_id, target))
        limit = self.k if count is None else count
        return [c.copy() for c in pool[:limit]]

    def occupancy(self) -> List[tuple]:
        return [(index, len(self.buckets[index])) for index in sorted(self.buckets) if self.buckets[index]]

    def size(self) -> int:
        return sum(len(bucket) for bucket in self.buckets.values())
