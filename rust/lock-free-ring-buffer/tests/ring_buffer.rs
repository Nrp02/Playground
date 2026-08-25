use lock_free_ring_buffer::RingBuffer;
use std::hint::spin_loop;
use std::sync::Arc;
use std::thread;

#[test]
fn fifo_order_single_threaded() {
    let buffer = RingBuffer::<i32>::new(8);
    for value in 0..8 {
        assert!(buffer.try_push(value).is_ok());
    }
    for expected in 0..8 {
        assert_eq!(buffer.try_pop(), Some(expected));
    }
    assert_eq!(buffer.try_pop(), None);
}

#[test]
fn fifo_order_survives_interleaved_push_pop() {
    let buffer = RingBuffer::<i32>::new(4);
    assert!(buffer.try_push(1).is_ok());
    assert!(buffer.try_push(2).is_ok());
    assert_eq!(buffer.try_pop(), Some(1));
    assert!(buffer.try_push(3).is_ok());
    assert!(buffer.try_push(4).is_ok());
    assert_eq!(buffer.try_pop(), Some(2));
    assert_eq!(buffer.try_pop(), Some(3));
    assert_eq!(buffer.try_pop(), Some(4));
    assert_eq!(buffer.try_pop(), None);
}

#[test]
fn empty_buffer_pop_returns_none() {
    let buffer = RingBuffer::<i32>::new(4);
    assert!(buffer.is_empty());
    assert_eq!(buffer.try_pop(), None);
}

#[test]
fn buffer_reports_full_at_capacity_and_rejects_extra_push() {
    let buffer = RingBuffer::<i32>::new(3);
    assert!(!buffer.is_full());
    assert!(buffer.try_push(10).is_ok());
    assert!(buffer.try_push(20).is_ok());
    assert!(buffer.try_push(30).is_ok());
    assert!(buffer.is_full());

    let rejected = buffer.try_push(40);
    assert_eq!(rejected, Err(40));
    assert!(buffer.is_full());
}

#[test]
fn popping_from_full_buffer_frees_a_slot_for_next_push() {
    let buffer = RingBuffer::<i32>::new(2);
    assert!(buffer.try_push(1).is_ok());
    assert!(buffer.try_push(2).is_ok());
    assert!(buffer.is_full());
    assert!(buffer.try_push(3).is_err());

    assert_eq!(buffer.try_pop(), Some(1));
    assert!(!buffer.is_full());
    assert!(buffer.try_push(3).is_ok());
    assert!(buffer.is_full());

    assert_eq!(buffer.try_pop(), Some(2));
    assert_eq!(buffer.try_pop(), Some(3));
    assert_eq!(buffer.try_pop(), None);
}

#[test]
fn capacity_reflects_requested_usable_slots() {
    let buffer = RingBuffer::<i32>::new(5);
    assert_eq!(buffer.capacity(), 5);
    for value in 0..5 {
        assert!(buffer.try_push(value).is_ok());
    }
    assert!(buffer.try_push(99).is_err());
}

fn run_concurrent_round_trip(item_count: u64, capacity: usize) {
    let buffer = Arc::new(RingBuffer::<u64>::new(capacity));

    let producer_buffer = Arc::clone(&buffer);
    let producer = thread::spawn(move || {
        for value in 0..item_count {
            let mut item = value;
            loop {
                match producer_buffer.try_push(item) {
                    Ok(()) => break,
                    Err(rejected) => {
                        item = rejected;
                        spin_loop();
                    }
                }
            }
        }
    });

    let consumer_buffer = Arc::clone(&buffer);
    let consumer = thread::spawn(move || {
        let mut received = Vec::with_capacity(item_count as usize);
        while received.len() < item_count as usize {
            match consumer_buffer.try_pop() {
                Some(value) => received.push(value),
                None => spin_loop(),
            }
        }
        received
    });

    producer.join().expect("producer thread panicked");
    let received = consumer.join().expect("consumer thread panicked");

    assert_eq!(received.len(), item_count as usize);
    for (index, value) in received.iter().enumerate() {
        assert_eq!(*value, index as u64);
    }
    assert!(buffer.is_empty());
}

#[test]
fn concurrent_producer_consumer_preserves_order_and_completeness() {
    run_concurrent_round_trip(200_000, 16);
}

#[test]
fn concurrent_producer_consumer_small_capacity_high_contention() {
    run_concurrent_round_trip(100_000, 2);
}

#[test]
fn concurrent_producer_consumer_repeated_runs_are_stable() {
    for _ in 0..5 {
        run_concurrent_round_trip(20_000, 8);
    }
}
