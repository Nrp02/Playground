use huffman_compressor::compress;
use huffman_compressor::decompress;

#[test]
fn round_trip_normal_text() {
    let data = b"the quick brown fox jumps over the lazy dog, the fox ran fast.";
    let compressed = compress(data);
    let decoded = decompress(&compressed);
    assert_eq!(decoded, data);
}

#[test]
fn round_trip_empty_input() {
    let data: &[u8] = &[];
    let compressed = compress(data);
    let decoded = decompress(&compressed);
    assert_eq!(decoded, data);
}

#[test]
fn round_trip_single_repeated_byte() {
    let data = vec![b'x'; 5000];
    let compressed = compress(&data);
    let decoded = decompress(&compressed);
    assert_eq!(decoded, data);
}

#[test]
fn round_trip_single_byte_input() {
    let data = vec![42u8];
    let compressed = compress(&data);
    let decoded = decompress(&compressed);
    assert_eq!(decoded, data);
}

#[test]
fn round_trip_all_256_byte_values() {
    let data: Vec<u8> = (0..=255u8).collect();
    let compressed = compress(&data);
    let decoded = decompress(&compressed);
    assert_eq!(decoded, data);
}

#[test]
fn round_trip_all_256_byte_values_repeated_with_varied_frequency() {
    let mut data = Vec::new();
    for symbol in 0..=255u8 {
        let repeat_count = 1 + (symbol as usize % 7);
        data.extend(std::iter::repeat(symbol).take(repeat_count));
    }
    let compressed = compress(&data);
    let decoded = decompress(&compressed);
    assert_eq!(decoded, data);
}

#[test]
fn compressed_size_is_smaller_for_skewed_frequency_input() {
    let mut data = Vec::new();
    data.extend(std::iter::repeat(b'a').take(9000));
    data.extend(std::iter::repeat(b'b').take(900));
    data.extend(std::iter::repeat(b'c').take(90));
    data.extend(std::iter::repeat(b'd').take(10));

    let compressed = compress(&data);
    assert!(compressed.len() < data.len());

    let decoded = decompress(&compressed);
    assert_eq!(decoded, data);
}

#[test]
fn round_trip_two_distinct_symbols() {
    let mut data = Vec::new();
    for i in 0..1000 {
        data.push(if i % 3 == 0 { b'0' } else { b'1' });
    }
    let compressed = compress(&data);
    let decoded = decompress(&compressed);
    assert_eq!(decoded, data);
}
