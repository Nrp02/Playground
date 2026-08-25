use arena_allocator::Arena;

#[repr(align(16))]
struct Align16 {
    value: u64,
}

struct Node<'a> {
    value: i64,
    next: Option<&'a Node<'a>>,
}

fn push<'a>(arena: &'a Arena, value: i64, next: Option<&'a Node<'a>>) -> &'a Node<'a> {
    &*arena.alloc(Node { value, next })
}

#[test]
fn allocated_values_are_distinct_and_independently_mutable() {
    let arena = Arena::new();
    let a = arena.alloc(1i32);
    let b = arena.alloc(2i32);
    assert_ne!(a as *mut i32 as usize, b as *mut i32 as usize);
    *a += 10;
    *b += 100;
    assert_eq!(*a, 11);
    assert_eq!(*b, 102);
}

#[test]
fn allocations_are_correctly_aligned() {
    let arena = Arena::new();

    let a = arena.alloc(1u8);
    assert_eq!((a as *mut u8 as usize) % std::mem::align_of::<u8>(), 0);

    let b = arena.alloc(2u16);
    assert_eq!((b as *mut u16 as usize) % std::mem::align_of::<u16>(), 0);

    let c = arena.alloc(3u64);
    assert_eq!((c as *mut u64 as usize) % std::mem::align_of::<u64>(), 0);

    let d = arena.alloc(Align16 { value: 4 });
    assert_eq!(
        (d as *mut Align16 as usize) % std::mem::align_of::<Align16>(),
        0
    );
    assert_eq!(d.value, 4);

    for i in 0..50u64 {
        let x = arena.alloc(i as u8);
        let y = arena.alloc(i);
        let z = arena.alloc(Align16 { value: i });
        assert_eq!((x as *mut u8 as usize) % std::mem::align_of::<u8>(), 0);
        assert_eq!((y as *mut u64 as usize) % std::mem::align_of::<u64>(), 0);
        assert_eq!(
            (z as *mut Align16 as usize) % std::mem::align_of::<Align16>(),
            0
        );
        assert_eq!(z.value, i);
    }
}

#[test]
fn grows_into_new_chunk_when_current_is_exhausted() {
    let arena = Arena::with_chunk_size(64);
    assert_eq!(arena.chunk_count(), 1);
    for i in 0..1000i64 {
        arena.alloc(i);
    }
    assert!(arena.chunk_count() > 1);
}

#[test]
fn reset_allows_memory_to_be_reused_without_growing() {
    let mut arena = Arena::with_chunk_size(4096);
    for i in 0..100i64 {
        arena.alloc(i);
    }
    let chunk_count_before = arena.chunk_count();
    let reserved_before = arena.bytes_reserved();
    let used_before = arena.bytes_allocated();
    assert!(used_before > 0);

    arena.reset();
    assert_eq!(arena.bytes_allocated(), 0);
    assert_eq!(arena.chunk_count(), chunk_count_before);
    assert_eq!(arena.bytes_reserved(), reserved_before);

    for i in 0..100i64 {
        let v = arena.alloc(i);
        assert_eq!(*v, i);
    }
    assert_eq!(arena.chunk_count(), chunk_count_before);
}

#[test]
fn reset_after_growth_reuses_existing_chunks_without_new_allocation() {
    let mut arena = Arena::with_chunk_size(64);
    for i in 0..500i64 {
        arena.alloc(i);
    }
    let chunk_count_before = arena.chunk_count();
    assert!(chunk_count_before > 1);

    arena.reset();
    for i in 0..500i64 {
        arena.alloc(i);
    }
    assert_eq!(arena.chunk_count(), chunk_count_before);
}

#[test]
fn linked_list_traverses_correctly_and_reset_allows_a_fresh_structure() {
    let mut arena = Arena::new();

    {
        let mut head: Option<&Node> = None;
        for i in 0..200i64 {
            head = Some(push(&arena, i, head));
        }
        let mut sum = 0;
        let mut current = head;
        while let Some(n) = current {
            sum += n.value;
            current = n.next;
        }
        assert_eq!(sum, (0..200i64).sum::<i64>());
    }

    arena.reset();

    {
        let mut head: Option<&Node> = None;
        for i in 0..50i64 {
            head = Some(push(&arena, i * 2, head));
        }
        let mut sum = 0;
        let mut current = head;
        while let Some(n) = current {
            sum += n.value;
            current = n.next;
        }
        assert_eq!(sum, (0..50i64).map(|i| i * 2).sum::<i64>());
    }
}
