use mpsc_channel::{channel, sync_channel, RecvTimeoutError};
use std::collections::HashSet;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;

fn unbounded_demo() {
    println!("== unbounded multi-producer demo ==");
    let (tx, rx) = channel();
    let n_producers = 6;
    let n_per_producer = 2000;
    let mut handles = Vec::new();
    for p in 0..n_producers {
        let tx = tx.clone();
        handles.push(thread::spawn(move || {
            for i in 0..n_per_producer {
                tx.send(p * n_per_producer + i).unwrap();
            }
        }));
    }
    drop(tx);
    let mut seen = HashSet::new();
    for v in rx.iter() {
        seen.insert(v);
    }
    for h in handles {
        h.join().unwrap();
    }
    println!(
        "received {} unique values out of {} expected",
        seen.len(),
        n_producers * n_per_producer
    );
}

fn bounded_backpressure_demo() {
    println!("== bounded backpressure demo ==");
    let (tx, rx) = sync_channel(8);
    let blocked_sends = Arc::new(AtomicUsize::new(0));
    let blocked_sends_clone = blocked_sends.clone();
    let handle = thread::spawn(move || {
        for i in 0..40 {
            if tx.try_send(i).is_err() {
                blocked_sends_clone.fetch_add(1, Ordering::Relaxed);
                tx.send(i).unwrap();
            }
        }
    });
    for _ in 0..40 {
        thread::sleep(Duration::from_millis(2));
        let _ = rx.recv().unwrap();
    }
    handle.join().unwrap();
    println!(
        "producer blocked (fell back to blocking send) {} times",
        blocked_sends.load(Ordering::Relaxed)
    );
}

fn recv_timeout_demo() {
    println!("== recv_timeout demo ==");
    let (tx, rx) = channel::<i32>();
    match rx.recv_timeout(Duration::from_millis(50)) {
        Err(RecvTimeoutError::Timeout) => println!("recv_timeout timed out as expected"),
        other => println!("unexpected result: {:?}", other.is_ok()),
    }
    tx.send(99).unwrap();
    match rx.recv_timeout(Duration::from_millis(50)) {
        Ok(v) => println!("recv_timeout got value: {}", v),
        Err(_) => println!("unexpected timeout"),
    }
}

fn disconnect_demo() {
    println!("== disconnect demo ==");
    let (tx, rx) = channel::<i32>();
    let handle = thread::spawn(move || {
        thread::sleep(Duration::from_millis(30));
        drop(tx);
    });
    match rx.recv() {
        Err(_) => println!("recv returned Err promptly after last sender dropped"),
        Ok(_) => println!("unexpected value"),
    }
    handle.join().unwrap();
}

fn iterator_drain_demo() {
    println!("== iterator drain demo ==");
    let (tx, rx) = channel();
    for i in 0..10 {
        tx.send(i).unwrap();
    }
    drop(tx);
    let collected: Vec<i32> = rx.into_iter().collect();
    println!("drained via iterator: {:?}", collected);
}

fn main() {
    unbounded_demo();
    bounded_backpressure_demo();
    recv_timeout_demo();
    disconnect_demo();
    iterator_drain_demo();
}
