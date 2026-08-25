use std::cmp::Ordering;
use std::cmp::Reverse;
use std::collections::BinaryHeap;
use std::collections::HashMap;

#[derive(Debug, Clone)]
enum Node {
    Leaf { symbol: u8 },
    Internal { left: Box<Node>, right: Box<Node> },
}

struct HeapItem {
    freq: u64,
    seq: u32,
    node: Node,
}

impl PartialEq for HeapItem {
    fn eq(&self, other: &Self) -> bool {
        self.freq == other.freq && self.seq == other.seq
    }
}

impl Eq for HeapItem {}

impl PartialOrd for HeapItem {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for HeapItem {
    fn cmp(&self, other: &Self) -> Ordering {
        (self.freq, self.seq).cmp(&(other.freq, other.seq))
    }
}

fn build_tree(leaves: &[(u8, u64)]) -> Node {
    let mut heap: BinaryHeap<Reverse<HeapItem>> = BinaryHeap::new();
    let mut next_seq: u32 = 0;

    for &(symbol, freq) in leaves {
        heap.push(Reverse(HeapItem {
            freq,
            seq: next_seq,
            node: Node::Leaf { symbol },
        }));
        next_seq += 1;
    }

    while heap.len() > 1 {
        let Reverse(a) = heap.pop().expect("heap has at least two items");
        let Reverse(b) = heap.pop().expect("heap has at least two items");
        let merged = HeapItem {
            freq: a.freq + b.freq,
            seq: next_seq,
            node: Node::Internal {
                left: Box::new(a.node),
                right: Box::new(b.node),
            },
        };
        next_seq += 1;
        heap.push(Reverse(merged));
    }

    heap.pop().expect("heap has at least one item").0.node
}

fn generate_codes(root: &Node) -> HashMap<u8, Vec<bool>> {
    let mut table = HashMap::new();
    let mut path = Vec::new();
    walk_codes(root, &mut path, &mut table);
    table
}

fn walk_codes(node: &Node, path: &mut Vec<bool>, table: &mut HashMap<u8, Vec<bool>>) {
    match node {
        Node::Leaf { symbol } => {
            table.insert(*symbol, path.clone());
        }
        Node::Internal { left, right } => {
            path.push(false);
            walk_codes(left, path, table);
            path.pop();

            path.push(true);
            walk_codes(right, path, table);
            path.pop();
        }
    }
}

struct BitWriter {
    bytes: Vec<u8>,
    current: u8,
    filled: u8,
}

impl BitWriter {
    fn new() -> Self {
        BitWriter {
            bytes: Vec::new(),
            current: 0,
            filled: 0,
        }
    }

    fn write_bit(&mut self, bit: bool) {
        self.current <<= 1;
        if bit {
            self.current |= 1;
        }
        self.filled += 1;
        if self.filled == 8 {
            self.bytes.push(self.current);
            self.current = 0;
            self.filled = 0;
        }
    }

    fn finish(mut self) -> Vec<u8> {
        if self.filled > 0 {
            self.current <<= 8 - self.filled;
            self.bytes.push(self.current);
        }
        self.bytes
    }
}

struct BitReader<'a> {
    bytes: &'a [u8],
    byte_pos: usize,
    bit_pos: u8,
}

impl<'a> BitReader<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        BitReader {
            bytes,
            byte_pos: 0,
            bit_pos: 0,
        }
    }

    fn read_bit(&mut self) -> Option<bool> {
        let byte = *self.bytes.get(self.byte_pos)?;
        let bit = (byte >> (7 - self.bit_pos)) & 1 == 1;
        self.bit_pos += 1;
        if self.bit_pos == 8 {
            self.bit_pos = 0;
            self.byte_pos += 1;
        }
        Some(bit)
    }
}

fn count_frequencies(data: &[u8]) -> Vec<(u8, u64)> {
    let mut counts = [0u64; 256];
    for &byte in data {
        counts[byte as usize] += 1;
    }
    let mut leaves = Vec::new();
    for symbol in 0..256usize {
        if counts[symbol] > 0 {
            leaves.push((symbol as u8, counts[symbol]));
        }
    }
    leaves
}

fn write_header(leaves: &[(u8, u64)], out: &mut Vec<u8>) {
    let count = leaves.len() as u16;
    out.extend_from_slice(&count.to_le_bytes());
    for &(symbol, freq) in leaves {
        out.push(symbol);
        let freq32 = freq as u32;
        out.extend_from_slice(&freq32.to_le_bytes());
    }
}

fn read_header(input: &[u8]) -> (Vec<(u8, u64)>, usize) {
    let count = u16::from_le_bytes([input[0], input[1]]) as usize;
    let mut pos = 2;
    let mut leaves = Vec::with_capacity(count);
    for _ in 0..count {
        let symbol = input[pos];
        let freq = u32::from_le_bytes([
            input[pos + 1],
            input[pos + 2],
            input[pos + 3],
            input[pos + 4],
        ]) as u64;
        leaves.push((symbol, freq));
        pos += 5;
    }
    (leaves, pos)
}

pub fn compress(data: &[u8]) -> Vec<u8> {
    let leaves = count_frequencies(data);
    let mut out = Vec::new();
    write_header(&leaves, &mut out);

    if leaves.len() < 2 {
        return out;
    }

    let tree = build_tree(&leaves);
    let codes = generate_codes(&tree);

    let mut writer = BitWriter::new();
    for &byte in data {
        let code = codes.get(&byte).expect("every byte has an assigned code");
        for &bit in code {
            writer.write_bit(bit);
        }
    }
    out.extend(writer.finish());
    out
}

pub fn decompress(input: &[u8]) -> Vec<u8> {
    if input.is_empty() {
        return Vec::new();
    }

    let (leaves, header_len) = read_header(input);
    let total_len: u64 = leaves.iter().map(|&(_, freq)| freq).sum();

    if leaves.is_empty() {
        return Vec::new();
    }

    if leaves.len() == 1 {
        let (symbol, _) = leaves[0];
        return vec![symbol; total_len as usize];
    }

    let tree = build_tree(&leaves);
    let mut reader = BitReader::new(&input[header_len..]);
    let mut result = Vec::with_capacity(total_len as usize);

    while (result.len() as u64) < total_len {
        let mut node = &tree;
        loop {
            match node {
                Node::Leaf { symbol } => {
                    result.push(*symbol);
                    break;
                }
                Node::Internal { left, right } => {
                    let bit = reader
                        .read_bit()
                        .expect("bitstream has enough bits for the declared length");
                    node = if bit { right } else { left };
                }
            }
        }
    }

    result
}

pub fn compression_ratio(original: &[u8], compressed: &[u8]) -> f64 {
    if original.is_empty() {
        return 1.0;
    }
    compressed.len() as f64 / original.len() as f64
}
