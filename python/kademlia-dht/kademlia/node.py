from __future__ import annotations

import random
from typing import Dict, List, Optional, Set, Tuple

from .ids import DEFAULT_BITS, bucket_index, distance, id_in_bucket, key_id
from .network import FIND_NODE, FIND_VALUE, PING, STORE, Network, Timeout
from .routing import Contact, RoutingTable

MAX_EVICTION_DEPTH = 2


class LookupResult:
    __slots__ = ("target", "value", "contacts", "rpcs", "rounds", "timeouts")

    def __init__(
        self,
        target: int,
        value: Optional[str],
        contacts: List[Contact],
        rpcs: int,
        rounds: int,
        timeouts: int,
    ) -> None:
        self.target = target
        self.value = value
        self.contacts = contacts
        self.rpcs = rpcs
        self.rounds = rounds
        self.timeouts = timeouts

    @property
    def found(self) -> bool:
        return self.value is not None

    def node_ids(self) -> List[int]:
        return [c.node_id for c in self.contacts]

    def __repr__(self) -> str:
        return "LookupResult(found={}, contacts={}, rpcs={}, rounds={})".format(
            self.found, len(self.contacts), self.rpcs, self.rounds
        )


class KademliaNode:
    def __init__(
        self,
        node_id: int,
        network: Network,
        k: int = 20,
        alpha: int = 3,
        bits: int = DEFAULT_BITS,
        address: Optional[str] = None,
    ) -> None:
        self.id = node_id
        self.bits = bits
        self.k = k
        self.alpha = alpha
        self.network = network
        self.address = address if address is not None else "node-{:x}".format(node_id)[:16]
        self.contact = Contact(node_id, self.address)
        self.storage: Dict[int, str] = {}
        self.rng = random.Random(node_id & 0xFFFFFFFF)
        self.routing = RoutingTable(node_id, k=k, bits=bits, ping=self._probe_stale)

    def _touch(self, sender: Contact) -> None:
        self.routing.update(sender, self.network.now())

    def rpc_ping(self, sender: Contact) -> bool:
        self._touch(sender)
        return True

    def rpc_store(self, sender: Contact, key: int, value: str) -> bool:
        self._touch(sender)
        self.storage[key] = value
        return True

    def rpc_find_node(self, sender: Contact, target: int) -> List[Contact]:
        self._touch(sender)
        return self.routing.closest(target, self.k, exclude=[sender.node_id])

    def rpc_find_value(self, sender: Contact, key: int) -> Tuple[Optional[str], List[Contact]]:
        self._touch(sender)
        if key in self.storage:
            return self.storage[key], []
        return None, self.routing.closest(key, self.k, exclude=[sender.node_id])

    def ping(self, contact: Contact) -> bool:
        try:
            self.network.call(self.contact, contact.node_id, PING)
        except Timeout:
            self.routing.remove(contact.node_id)
            return False
        return True

    def _probe_stale(self, contact: Contact) -> bool:
        if self.network.eviction_depth >= MAX_EVICTION_DEPTH:
            return True
        self.network.eviction_depth += 1
        try:
            return self.ping(contact)
        finally:
            self.network.eviction_depth -= 1

    def node_lookup(self, target: int) -> LookupResult:
        return self._iterative_lookup(target, want_value=False)

    def value_lookup(self, key: int) -> LookupResult:
        if key in self.storage:
            return LookupResult(key, self.storage[key], [self.contact.copy()], 0, 0, 0)
        return self._iterative_lookup(key, want_value=True)

    def _iterative_lookup(self, target: int, want_value: bool) -> LookupResult:
        candidates: Dict[int, Contact] = {}
        for contact in self.routing.closest(target, self.k):
            candidates[contact.node_id] = contact
        queried: Set[int] = set()
        failed: Set[int] = set()
        rpcs = 0
        timeouts = 0
        rounds = 0
        improving = True

        while candidates:
            ordered = sorted(candidates.values(), key=lambda c: distance(c.node_id, target))
            shortlist = ordered[: self.k]
            pending = [c for c in shortlist if c.node_id not in queried]
            if not pending:
                break
            batch = pending[: self.alpha] if improving else pending
            snapshot = {c.node_id for c in shortlist}
            rounds += 1
            for contact in batch:
                queried.add(contact.node_id)
                rpcs += 1
                try:
                    if want_value:
                        value, peers = self.network.call(
                            self.contact, contact.node_id, FIND_VALUE, target
                        )
                    else:
                        value = None
                        peers = self.network.call(self.contact, contact.node_id, FIND_NODE, target)
                except Timeout:
                    timeouts += 1
                    failed.add(contact.node_id)
                    candidates.pop(contact.node_id, None)
                    self.routing.remove(contact.node_id)
                    continue
                self.routing.update(contact, self.network.now())
                if value is not None:
                    return LookupResult(target, value, [contact.copy()], rpcs, rounds, timeouts)
                for peer in peers:
                    if peer.node_id == self.id or peer.node_id in failed:
                        continue
                    if peer.node_id not in candidates:
                        candidates[peer.node_id] = Contact(peer.node_id, peer.address)
            alive = list(candidates.values())
            if not alive:
                break
            survivors = [distance(c.node_id, target) for c in alive if c.node_id in snapshot]
            best_after = min(distance(c.node_id, target) for c in alive)
            was_improving = improving
            improving = not survivors or best_after < min(survivors)
            if not was_improving and not improving:
                break

        final = [c.copy() for c in candidates.values() if c.node_id not in failed]
        final.sort(key=lambda c: distance(c.node_id, target))
        return LookupResult(target, None, final[: self.k], rpcs, rounds, timeouts)

    def join(self, bootstrap: Contact) -> bool:
        if bootstrap.node_id == self.id:
            return False
        self.routing.update(bootstrap, self.network.now())
        if not self.ping(bootstrap):
            return False
        self.node_lookup(self.id)
        neighbours = self.routing.closest(self.id, 1)
        floor = 0
        if neighbours:
            floor = bucket_index(self.id, neighbours[0].node_id, self.bits)
        for index in sorted(self.routing.buckets):
            if index <= floor:
                continue
            self.node_lookup(id_in_bucket(self.id, index, self.rng))
        return True

    def store(self, key: str, value: str) -> int:
        target = key_id(key, self.bits)
        result = self.node_lookup(target)
        pool = list(result.contacts)
        pool.append(self.contact.copy())
        pool.sort(key=lambda c: distance(c.node_id, target))
        replicas = pool[: self.k]
        stored = 0
        for contact in replicas:
            if contact.node_id == self.id:
                self.storage[target] = value
                stored += 1
                continue
            try:
                self.network.call(self.contact, contact.node_id, STORE, target, value)
                stored += 1
            except Timeout:
                self.routing.remove(contact.node_id)
        return stored

    def get(self, key: str) -> Optional[str]:
        return self.get_with_stats(key).value

    def get_with_stats(self, key: str) -> LookupResult:
        return self.value_lookup(key_id(key, self.bits))

    def holds(self, key: str) -> bool:
        return key_id(key, self.bits) in self.storage

    def __repr__(self) -> str:
        return "KademliaNode({})".format(self.address)
