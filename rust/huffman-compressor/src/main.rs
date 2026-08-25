use huffman_compressor::compress;
use huffman_compressor::compression_ratio;
use huffman_compressor::decompress;

fn main() {
    let sample = "the quick brown fox jumps over the lazy dog. \
the dog barks at the fox. the fox runs away quickly. \
the quick brown fox jumps over the lazy dog again and again."
        .repeat(20);
    let data = sample.as_bytes();

    let compressed = compress(data);
    let decoded = decompress(&compressed);

    assert_eq!(decoded, data, "round-trip must reproduce the original bytes exactly");

    let ratio = compression_ratio(data, &compressed);
    println!("original size:   {} bytes", data.len());
    println!("compressed size: {} bytes", compressed.len());
    println!("compression ratio: {:.4} ({:.1}% of original)", ratio, ratio * 100.0);
    println!("round-trip verified: decoded output matches original exactly");
}
