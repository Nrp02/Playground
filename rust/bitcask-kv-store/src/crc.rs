use std::sync::OnceLock;

const POLYNOMIAL: u32 = 0xEDB8_8320;

static TABLE: OnceLock<[u32; 256]> = OnceLock::new();

fn table() -> &'static [u32; 256] {
    TABLE.get_or_init(|| {
        let mut table = [0u32; 256];
        let mut index = 0usize;
        while index < 256 {
            let mut value = index as u32;
            let mut bit = 0;
            while bit < 8 {
                if value & 1 == 1 {
                    value = (value >> 1) ^ POLYNOMIAL;
                } else {
                    value >>= 1;
                }
                bit += 1;
            }
            table[index] = value;
            index += 1;
        }
        table
    })
}

#[derive(Debug, Clone, Copy)]
pub struct Crc32 {
    state: u32,
}

impl Crc32 {
    pub fn new() -> Crc32 {
        Crc32 { state: 0xFFFF_FFFF }
    }

    pub fn update(&mut self, bytes: &[u8]) {
        let table = table();
        let mut state = self.state;
        for &byte in bytes {
            let index = ((state ^ byte as u32) & 0xFF) as usize;
            state = (state >> 8) ^ table[index];
        }
        self.state = state;
    }

    pub fn finish(self) -> u32 {
        self.state ^ 0xFFFF_FFFF
    }
}

impl Default for Crc32 {
    fn default() -> Crc32 {
        Crc32::new()
    }
}

pub fn crc32(bytes: &[u8]) -> u32 {
    let mut digest = Crc32::new();
    digest.update(bytes);
    digest.finish()
}
