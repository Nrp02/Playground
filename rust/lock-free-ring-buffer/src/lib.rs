use std::cell::UnsafeCell;
use std::mem::MaybeUninit;
use std::sync::atomic::{AtomicUsize, Ordering};

pub struct RingBuffer<T> {
    slots: Box<[UnsafeCell<MaybeUninit<T>>]>,
    capacity: usize,
    head: AtomicUsize,
    tail: AtomicUsize,
}

unsafe impl<T: Send> Sync for RingBuffer<T> {}

impl<T> RingBuffer<T> {
    pub fn new(capacity: usize) -> Self {
        assert!(capacity > 0, "capacity must be greater than zero");
        let slot_count = capacity + 1;
        let mut slots = Vec::with_capacity(slot_count);
        for _ in 0..slot_count {
            slots.push(UnsafeCell::new(MaybeUninit::uninit()));
        }
        RingBuffer {
            slots: slots.into_boxed_slice(),
            capacity,
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        }
    }

    pub fn capacity(&self) -> usize {
        self.capacity
    }

    fn slot_count(&self) -> usize {
        self.slots.len()
    }

    fn advance(&self, index: usize) -> usize {
        let next = index + 1;
        if next == self.slot_count() {
            0
        } else {
            next
        }
    }

    pub fn try_push(&self, value: T) -> Result<(), T> {
        let tail = self.tail.load(Ordering::Relaxed);
        let next_tail = self.advance(tail);
        if next_tail == self.head.load(Ordering::Acquire) {
            return Err(value);
        }
        unsafe {
            (*self.slots[tail].get()).write(value);
        }
        self.tail.store(next_tail, Ordering::Release);
        Ok(())
    }

    pub fn try_pop(&self) -> Option<T> {
        let head = self.head.load(Ordering::Relaxed);
        if head == self.tail.load(Ordering::Acquire) {
            return None;
        }
        let value = unsafe { (*self.slots[head].get()).assume_init_read() };
        let next_head = self.advance(head);
        self.head.store(next_head, Ordering::Release);
        Some(value)
    }

    pub fn is_empty(&self) -> bool {
        self.head.load(Ordering::Acquire) == self.tail.load(Ordering::Acquire)
    }

    pub fn is_full(&self) -> bool {
        let tail = self.tail.load(Ordering::Acquire);
        let head = self.head.load(Ordering::Acquire);
        self.advance(tail) == head
    }
}

impl<T> Drop for RingBuffer<T> {
    fn drop(&mut self) {
        let mut head = *self.head.get_mut();
        let tail = *self.tail.get_mut();
        while head != tail {
            unsafe {
                (*self.slots[head].get()).assume_init_drop();
            }
            head = self.advance(head);
        }
    }
}
