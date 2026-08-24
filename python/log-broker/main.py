from broker.broker import Broker


def format_batch(batch):
    return {partition: [message.value for message in messages] for partition, messages in batch.items()}


def main() -> int:
    broker = Broker()
    topic = broker.create_topic("orders", num_partitions=3)

    print("=== producing messages across partitions ===")
    keys = ["alice", "bob", "carol", None, "alice", "dave", None, "bob", "alice"]
    for i, key in enumerate(keys):
        partition_index, offset = broker.produce("orders", f"order-{i}", key=key)
        print(f"produced order-{i} key={key!r} -> partition {partition_index} offset {offset}")

    print()
    for partition_index, partition in enumerate(topic.partitions):
        values = [message.value for message in partition.read_from(0)]
        print(f"partition {partition_index} contents: {values}")

    print("\n=== two independent consumer groups reading the same topic ===")
    billing = broker.create_consumer("orders", "billing")
    analytics = broker.create_consumer("orders", "analytics")

    billing_batch = billing.poll_all_partitions(max_messages=2)
    print("billing polled:", format_batch(billing_batch))
    billing.commit_all()

    analytics_batch = analytics.poll_all_partitions(max_messages=5)
    print("analytics polled:", format_batch(analytics_batch))
    analytics.commit_all()

    billing_batch_2 = billing.poll_all_partitions(max_messages=10)
    print("billing polled next batch:", format_batch(billing_batch_2))
    billing.commit_all()

    analytics_batch_2 = analytics.poll_all_partitions(max_messages=10)
    print("analytics polled next batch:", format_batch(analytics_batch_2))
    analytics.commit_all()

    print("\n=== simulating a consumer restart before commit ===")
    restart_demo = broker.create_consumer("orders", "restart-demo")
    processed = restart_demo.poll_all_partitions(max_messages=10)
    print("processed but not yet committed:", format_batch(processed))
    print("crash! consumer instance is discarded without committing")

    restarted = broker.create_consumer("orders", "restart-demo")
    redelivered = restarted.poll_all_partitions(max_messages=10)
    print("redelivered to the restarted consumer:", format_batch(redelivered))
    restarted.commit_all()

    after_commit = broker.create_consumer("orders", "restart-demo")
    nothing_new = after_commit.poll_all_partitions(max_messages=10)
    print("after commit, a fresh consumer sees:", format_batch(nothing_new))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
