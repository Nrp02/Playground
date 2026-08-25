from causal_broadcast import CausalNode
from vector_clock import compare_clocks


def print_clock(label: str, clock) -> None:
    ordered = ", ".join(f"{k}={v}" for k, v in sorted(clock.items()))
    print(f"{label}: {{{ordered}}}")


def main() -> None:
    node_ids = ["A", "B", "C", "D"]
    nodes = {node_id: CausalNode(node_id, node_ids) for node_id in node_ids}
    a, b, c, d = nodes["A"], nodes["B"], nodes["C"], nodes["D"]

    print("=== causal broadcast demo ===")

    msg1 = a.send("m1: A announces startup")
    print_clock("msg1 vector (A -> B, C delayed)", msg1.vector)
    b.receive(msg1)
    print(f"B delivered so far: {b.delivered_payloads}")

    d.local_event()
    d_snapshot = d.clock.snapshot()
    print_clock("D local event vector (unrelated to msg1)", d_snapshot)

    msg2 = b.send("m2: B forwards after seeing A's startup")
    print_clock("msg2 vector (B -> C, depends on msg1)", msg2.vector)

    print("C receives msg2 first (transport reordering)")
    c.receive(msg2)
    print(f"C pending buffer size: {c.pending_count()}, delivered so far: {c.delivered_payloads}")

    print("delayed msg1 finally arrives at C")
    c.receive(msg1)
    print(f"C pending buffer size: {c.pending_count()}, delivered so far: {c.delivered_payloads}")

    print()
    print("=== event classification ===")

    classification = compare_clocks(msg1.vector, msg2.vector)
    print(f"msg1 vs msg2: {classification}")

    classification = compare_clocks(msg2.vector, d_snapshot)
    print(f"msg2 vs D's local event: {classification}")

    classification = compare_clocks(msg1.vector, d_snapshot)
    print(f"msg1 vs D's local event: {classification}")

    same_vector = dict(msg1.vector)
    classification = compare_clocks(msg1.vector, same_vector)
    print(f"msg1 vs itself: {classification}")


if __name__ == "__main__":
    main()
