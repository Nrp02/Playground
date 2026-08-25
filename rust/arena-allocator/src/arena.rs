use std::alloc::{alloc, dealloc, handle_alloc_error, Layout};
use std::cell::{Cell, RefCell};
use std::ptr::NonNull;

const CHUNK_ALIGN: usize = 16;
const DEFAULT_CHUNK_SIZE: usize = 4096;
const MIN_CHUNK_SIZE: usize = 64;

fn align_up(addr: usize, align: usize) -> usize {
    (addr + align - 1) & !(align - 1)
}

struct Chunk {
    data: NonNull<u8>,
    layout: Layout,
    used: Cell<usize>,
}

impl Chunk {
    fn new(size: usize) -> Self {
        let layout = Layout::from_size_align(size, CHUNK_ALIGN).expect("invalid arena chunk layout");
        let raw = unsafe { alloc(layout) };
        let data = NonNull::new(raw).unwrap_or_else(|| handle_alloc_error(layout));
        Chunk {
            data,
            layout,
            used: Cell::new(0),
        }
    }

    fn size(&self) -> usize {
        self.layout.size()
    }

    fn try_allocate(&self, layout: Layout) -> Option<NonNull<u8>> {
        let base = self.data.as_ptr() as usize;
        let start = base + self.used.get();
        let aligned = align_up(start, layout.align());
        let end = aligned.checked_add(layout.size())?;
        if end > base + self.size() {
            return None;
        }
        self.used.set(end - base);
        NonNull::new(aligned as *mut u8)
    }

    fn reset(&self) {
        self.used.set(0);
    }
}

impl Drop for Chunk {
    fn drop(&mut self) {
        unsafe { dealloc(self.data.as_ptr(), self.layout) }
    }
}

pub struct Arena {
    chunks: RefCell<Vec<Chunk>>,
    current: Cell<usize>,
    next_chunk_size: Cell<usize>,
}

impl Arena {
    pub fn new() -> Self {
        Self::with_chunk_size(DEFAULT_CHUNK_SIZE)
    }

    pub fn with_chunk_size(chunk_size: usize) -> Self {
        let chunk_size = chunk_size.max(MIN_CHUNK_SIZE);
        Arena {
            chunks: RefCell::new(vec![Chunk::new(chunk_size)]),
            current: Cell::new(0),
            next_chunk_size: Cell::new(chunk_size.saturating_mul(2)),
        }
    }

    pub fn alloc<T>(&self, value: T) -> &mut T {
        let layout = Layout::new::<T>();
        let ptr = self.alloc_raw(layout).cast::<T>();
        unsafe {
            ptr.as_ptr().write(value);
            &mut *ptr.as_ptr()
        }
    }

    fn alloc_raw(&self, layout: Layout) -> NonNull<u8> {
        loop {
            if let Some(ptr) = self.try_allocate_in_current(layout) {
                return ptr;
            }
            self.advance_chunk(layout);
        }
    }

    fn try_allocate_in_current(&self, layout: Layout) -> Option<NonNull<u8>> {
        let chunks = self.chunks.borrow();
        chunks.get(self.current.get()).and_then(|c| c.try_allocate(layout))
    }

    fn advance_chunk(&self, layout: Layout) {
        let next_index = self.current.get() + 1;
        let has_existing = next_index < self.chunks.borrow().len();
        if !has_existing {
            let size = self
                .next_chunk_size
                .get()
                .max(layout.size().saturating_add(layout.align()));
            self.next_chunk_size.set(size.saturating_mul(2));
            self.chunks.borrow_mut().push(Chunk::new(size));
        }
        self.current.set(next_index);
    }

    pub fn reset(&mut self) {
        for chunk in self.chunks.get_mut().iter() {
            chunk.reset();
        }
        self.current.set(0);
    }

    pub fn chunk_count(&self) -> usize {
        self.chunks.borrow().len()
    }

    pub fn bytes_allocated(&self) -> usize {
        self.chunks.borrow().iter().map(|c| c.used.get()).sum()
    }

    pub fn bytes_reserved(&self) -> usize {
        self.chunks.borrow().iter().map(|c| c.size()).sum()
    }
}

impl Default for Arena {
    fn default() -> Self {
        Self::new()
    }
}
