pub const DISPLAY_WIDTH: usize = 64;
pub const DISPLAY_HEIGHT: usize = 32;
pub const PIXEL_COUNT: usize = DISPLAY_WIDTH * DISPLAY_HEIGHT;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Display {
    pixels: [bool; PIXEL_COUNT],
}

impl Display {
    pub fn new() -> Self {
        Display {
            pixels: [false; PIXEL_COUNT],
        }
    }

    pub fn clear(&mut self) {
        self.pixels = [false; PIXEL_COUNT];
    }

    pub fn get(&self, x: usize, y: usize) -> bool {
        if x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT {
            return false;
        }
        self.pixels[y * DISPLAY_WIDTH + x]
    }

    pub fn set(&mut self, x: usize, y: usize, on: bool) {
        if x < DISPLAY_WIDTH && y < DISPLAY_HEIGHT {
            self.pixels[y * DISPLAY_WIDTH + x] = on;
        }
    }

    pub fn pixels(&self) -> &[bool] {
        &self.pixels
    }

    pub fn lit_count(&self) -> usize {
        self.pixels.iter().filter(|p| **p).count()
    }

    pub fn is_blank(&self) -> bool {
        self.lit_count() == 0
    }

    pub fn draw_sprite(&mut self, x: u8, y: u8, sprite: &[u8]) -> bool {
        let origin_x = usize::from(x) % DISPLAY_WIDTH;
        let origin_y = usize::from(y) % DISPLAY_HEIGHT;
        let mut collision = false;
        for (row, byte) in sprite.iter().enumerate() {
            let py = origin_y + row;
            if py >= DISPLAY_HEIGHT {
                break;
            }
            for bit in 0..8usize {
                if byte & (0x80 >> bit) == 0 {
                    continue;
                }
                let px = origin_x + bit;
                if px >= DISPLAY_WIDTH {
                    break;
                }
                let index = py * DISPLAY_WIDTH + px;
                if self.pixels[index] {
                    collision = true;
                }
                self.pixels[index] = !self.pixels[index];
            }
        }
        collision
    }

    pub fn to_text(&self) -> String {
        let mut out = String::with_capacity((DISPLAY_WIDTH * 2 + 3) * (DISPLAY_HEIGHT + 2));
        out.push('+');
        for _ in 0..DISPLAY_WIDTH * 2 {
            out.push('-');
        }
        out.push_str("+\n");
        for y in 0..DISPLAY_HEIGHT {
            out.push('|');
            for x in 0..DISPLAY_WIDTH {
                out.push_str(if self.get(x, y) { "██" } else { "  " });
            }
            out.push_str("|\n");
        }
        out.push('+');
        for _ in 0..DISPLAY_WIDTH * 2 {
            out.push('-');
        }
        out.push('+');
        out
    }
}

impl Default for Display {
    fn default() -> Self {
        Display::new()
    }
}
