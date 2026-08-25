use lock_free_ring_buffer::RingBuffer;
use std::hint::spin_loop;
use std::sync::Arc;
use std::thread;
use std::time::Instant;

const ITEM_COUNT: u64 = 2_000_000;
const CAPACITY: usize = 1024;

fn main() {
    let buffer = Arc::new(RingBuffer::<u64>::new(CAPACITY));

    let producer_buffer = Arc::clone(&buffer);
    let start = Instant::now();

    let producer = thread::spawn(move || {
        for value in 0..ITEM_COUNT {
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
        let mut received = Vec::with_capacity(ITEM_COUNT as usize);
        while received.len() < ITEM_COUNT as usize {
            match consumer_buffer.try_pop() {
                Some(value) => received.push(value),
                None => spin_loop(),
            }
        }
        received
    });

    producer.join().expect("producer thread panicked");
    let received = consumer.join().expect("consumer thread panicked");
    let elapsed = start.elapsed();

    assert_eq!(received.len(), ITEM_COUNT as usize);
    for (index, value) in received.iter().enumerate() {
        assert_eq!(*value, index as u64);
    }

    let seconds = elapsed.as_secs_f64();
    let throughput = ITEM_COUNT as f64 / seconds;
    println!("transferred {ITEM_COUNT} values in {seconds:.3}s");
    println!("throughput: {throughput:.0} values/sec");
    println!("all values received exactly once and in order");
}
