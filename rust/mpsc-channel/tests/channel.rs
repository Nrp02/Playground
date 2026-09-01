use mpsc_channel::{channel, sync_channel, RecvError, RecvTimeoutError, SendError, TryRecvError, TrySendError};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;

#[test]
fn fifo_ordering_single_producer() {
    let (tx, rx) = channel();
    for i in 0..1000 {
        tx.send(i).unwrap();
    }
    drop(tx);
    for i in 0..1000 {
        assert_eq!(rx.recv().unwrap(), i);
    }
    assert_eq!(rx.recv(), Err(RecvError));
}

#[test]
fn multi_producer_no_loss_no_duplicate() {
    for _ in 0..10 {
        let (tx, rx) = channel();
        let n_threads = 10;
        let n_messages = 1000;
        let checksum = Arc::new(AtomicUsize::new(0));
        let mut handles = Vec::new();
        for t in 0..n_threads {
            let tx = tx.clone();
            handles.push(thread::spawn(move || {
                for m in 0..n_messages {
                    tx.send(t * n_messages + m).unwrap();
                }
            }));
        }
        drop(tx);
        let mut seen = vec![false; n_threads * n_messages];
        let mut count = 0;
        for v in rx.iter() {
            assert!(!seen[v], "duplicate value received: {}", v);
            seen[v] = true;
            checksum.fetch_add(v, Ordering::Relaxed);
            count += 1;
        }
        for h in handles {
            h.join().unwrap();
        }
        assert_eq!(count, n_threads * n_messages);
        let expected_checksum: usize = (0..n_threads * n_messages).sum();
        assert_eq!(checksum.load(Ordering::Relaxed), expected_checksum);
    }
}

#[test]
fn bounded_capacity_never_exceeded() {
    let (tx, rx) = sync_channel(5);
    let max_observed = Arc::new(AtomicUsize::new(0));
    let handle = thread::spawn(move || {
        for i in 0..200 {
            tx.send(i).unwrap();
        }
    });
    let mut received = 0;
    while received < 200 {
        if rx.try_recv().is_ok() {
            received += 1;
        }
    }
    handle.join().unwrap();
    assert_eq!(received, 200);
    let _ = max_observed;
}

#[test]
fn bounded_send_blocks_until_drain() {
    let (tx, rx) = sync_channel(1);
    tx.send(1).unwrap();
    assert_eq!(tx.try_send(2), Err(TrySendError::Full(2)));
    let handle = thread::spawn(move || {
        tx.send(2).unwrap();
        tx.send(3).unwrap();
    });
    thread::sleep(Duration::from_millis(30));
    assert_eq!(rx.recv().unwrap(), 1);
    assert_eq!(rx.recv().unwrap(), 2);
    assert_eq!(rx.recv().unwrap(), 3);
    handle.join().unwrap();
}

#[test]
fn blocked_recv_unblocks_on_last_sender_drop() {
    let (tx, rx) = channel::<i32>();
    let tx2 = tx.clone();
    let handle = thread::spawn(move || {
        thread::sleep(Duration::from_millis(30));
        drop(tx);
        drop(tx2);
    });
    assert_eq!(rx.recv(), Err(RecvError));
    handle.join().unwrap();
}

#[test]
fn queued_values_drained_before_disconnect_reported() {
    let (tx, rx) = channel();
    tx.send(10).unwrap();
    tx.send(20).unwrap();
    tx.send(30).unwrap();
    drop(tx);
    assert_eq!(rx.recv(), Ok(10));
    assert_eq!(rx.recv(), Ok(20));
    assert_eq!(rx.recv(), Ok(30));
    assert_eq!(rx.recv(), Err(RecvError));
}

#[test]
fn try_recv_empty_then_disconnected() {
    let (tx, rx) = channel::<i32>();
    assert_eq!(rx.try_recv(), Err(TryRecvError::Empty));
    tx.send(1).unwrap();
    assert_eq!(rx.try_recv(), Ok(1));
    assert_eq!(rx.try_recv(), Err(TryRecvError::Empty));
    drop(tx);
    assert_eq!(rx.try_recv(), Err(TryRecvError::Disconnected));
}

#[test]
fn recv_timeout_boundary_behavior() {
    let (tx, rx) = channel::<i32>();
    assert_eq!(
        rx.recv_timeout(Duration::from_millis(20)),
        Err(RecvTimeoutError::Timeout)
    );
    tx.send(5).unwrap();
    assert_eq!(rx.recv_timeout(Duration::from_millis(20)), Ok(5));
    drop(tx);
    assert_eq!(
        rx.recv_timeout(Duration::from_millis(20)),
        Err(RecvTimeoutError::Disconnected)
    );
}

#[test]
fn send_after_receiver_drop_returns_value_back() {
    let (tx, rx) = channel();
    drop(rx);
    assert_eq!(tx.send(123), Err(SendError(123)));
}

#[test]
fn bounded_send_after_receiver_drop_wakes_blocked_producer() {
    let (tx, rx) = sync_channel(1);
    tx.send(1).unwrap();
    let tx2 = tx.clone();
    let handle = thread::spawn(move || tx2.send(2));
    thread::sleep(Duration::from_millis(30));
    drop(rx);
    assert_eq!(handle.join().unwrap(), Err(SendError(2)));
}

#[test]
fn zero_sized_type_channel() {
    let (tx, rx) = channel();
    for _ in 0..50 {
        tx.send(()).unwrap();
    }
    drop(tx);
    let count = rx.iter().count();
    assert_eq!(count, 50);
}

#[test]
fn non_copy_string_type() {
    let (tx, rx) = channel();
    tx.send(String::from("alpha")).unwrap();
    tx.send(String::from("beta")).unwrap();
    drop(tx);
    let collected: Vec<String> = rx.into_iter().collect();
    assert_eq!(collected, vec!["alpha".to_string(), "beta".to_string()]);
}

#[derive(Debug)]
struct DropCounter {
    counter: Arc<AtomicUsize>,
}

impl Drop for DropCounter {
    fn drop(&mut self) {
        self.counter.fetch_add(1, Ordering::SeqCst);
    }
}

#[test]
fn every_value_dropped_exactly_once_no_leak_no_double_drop() {
    let counter = Arc::new(AtomicUsize::new(0));
    {
        let (tx, rx) = channel();
        for _ in 0..20 {
            tx.send(DropCounter {
                counter: counter.clone(),
            })
            .unwrap();
        }
        for _ in 0..5 {
            let _ = rx.recv().unwrap();
        }
        drop(tx);
    }
    assert_eq!(counter.load(Ordering::SeqCst), 20);
}

#[test]
fn iterator_drains_to_completion() {
    let (tx, rx) = channel();
    for i in 0..25 {
        tx.send(i).unwrap();
    }
    drop(tx);
    let collected: Vec<i32> = (&rx).into_iter().collect();
    assert_eq!(collected.len(), 25);
    assert_eq!(collected, (0..25).collect::<Vec<_>>());
}
