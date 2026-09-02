use crate::display::{Display, DISPLAY_HEIGHT, DISPLAY_WIDTH};
use crate::error::Error;
use crate::font::{glyph_address, FONT_SET, FONT_START};
use crate::rng::Rng;

pub const MEMORY_SIZE: usize = 4096;
pub const PROGRAM_START: u16 = 0x200;
pub const STACK_DEPTH: usize = 16;
pub const REGISTER_COUNT: usize = 16;
pub const KEY_COUNT: usize = 16;
pub const DEFAULT_CYCLES_PER_FRAME: usize = 12;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Quirks {
    pub shift_uses_vy: bool,
    pub load_store_increments_i: bool,
}

impl Quirks {
    pub fn original() -> Self {
        Quirks {
            shift_uses_vy: true,
            load_store_increments_i: true,
        }
    }

    pub fn modern() -> Self {
        Quirks {
            shift_uses_vy: false,
            load_store_increments_i: false,
        }
    }
}

impl Default for Quirks {
    fn default() -> Self {
        Quirks::original()
    }
}

#[derive(Debug, Clone)]
pub struct Chip8 {
    memory: [u8; MEMORY_SIZE],
    v: [u8; REGISTER_COUNT],
    i: u16,
    pc: u16,
    stack: [u16; STACK_DEPTH],
    sp: usize,
    delay_timer: u8,
    sound_timer: u8,
    display: Display,
    keys: [bool; KEY_COUNT],
    rng: Rng,
    quirks: Quirks,
    cycles: u64,
    waiting_for_key: bool,
}

impl Chip8 {
    pub fn new() -> Self {
        Chip8::with_seed(Rng::default().seed())
    }

    pub fn with_seed(seed: u64) -> Self {
        let mut memory = [0u8; MEMORY_SIZE];
        let base = FONT_START as usize;
        memory[base..base + FONT_SET.len()].copy_from_slice(&FONT_SET);
        Chip8 {
            memory,
            v: [0; REGISTER_COUNT],
            i: 0,
            pc: PROGRAM_START,
            stack: [0; STACK_DEPTH],
            sp: 0,
            delay_timer: 0,
            sound_timer: 0,
            display: Display::new(),
            keys: [false; KEY_COUNT],
            rng: Rng::new(seed),
            quirks: Quirks::default(),
            cycles: 0,
            waiting_for_key: false,
        }
    }

    pub fn set_quirks(&mut self, quirks: Quirks) {
        self.quirks = quirks;
    }

    pub fn quirks(&self) -> Quirks {
        self.quirks
    }

    pub fn load_rom(&mut self, rom: &[u8]) -> Result<(), Error> {
        let capacity = MEMORY_SIZE - PROGRAM_START as usize;
        if rom.len() > capacity {
            return Err(Error::RomTooLarge {
                size: rom.len(),
                capacity,
            });
        }
        let base = PROGRAM_START as usize;
        self.memory[base..].fill(0);
        self.memory[base..base + rom.len()].copy_from_slice(rom);
        self.pc = PROGRAM_START;
        self.sp = 0;
        self.i = 0;
        self.v = [0; REGISTER_COUNT];
        self.delay_timer = 0;
        self.sound_timer = 0;
        self.waiting_for_key = false;
        self.cycles = 0;
        self.display.clear();
        Ok(())
    }

    pub fn pc(&self) -> u16 {
        self.pc
    }

    pub fn index(&self) -> u16 {
        self.i
    }

    pub fn sp(&self) -> usize {
        self.sp
    }

    pub fn stack(&self) -> &[u16] {
        &self.stack[..self.sp]
    }

    pub fn v(&self, index: usize) -> u8 {
        self.v[index & 0x0F]
    }

    pub fn registers(&self) -> &[u8; REGISTER_COUNT] {
        &self.v
    }

    pub fn set_register(&mut self, index: usize, value: u8) {
        self.v[index & 0x0F] = value;
    }

    pub fn set_index(&mut self, value: u16) {
        self.i = value;
    }

    pub fn delay_timer(&self) -> u8 {
        self.delay_timer
    }

    pub fn sound_timer(&self) -> u8 {
        self.sound_timer
    }

    pub fn set_delay_timer(&mut self, value: u8) {
        self.delay_timer = value;
    }

    pub fn set_sound_timer(&mut self, value: u8) {
        self.sound_timer = value;
    }

    pub fn display(&self) -> &Display {
        &self.display
    }

    pub fn cycles(&self) -> u64 {
        self.cycles
    }

    pub fn waiting_for_key(&self) -> bool {
        self.waiting_for_key
    }

    pub fn set_key(&mut self, key: usize, pressed: bool) {
        if key < KEY_COUNT {
            self.keys[key] = pressed;
        }
    }

    pub fn release_all_keys(&mut self) {
        self.keys = [false; KEY_COUNT];
    }

    pub fn key(&self, key: usize) -> bool {
        key < KEY_COUNT && self.keys[key]
    }

    pub fn read_memory(&self, address: u16) -> Result<u8, Error> {
        let index = address as usize;
        if index >= MEMORY_SIZE {
            return Err(Error::MemoryOutOfBounds {
                address: u32::from(address),
                length: 1,
            });
        }
        Ok(self.memory[index])
    }

    pub fn write_memory(&mut self, address: u16, value: u8) -> Result<(), Error> {
        let index = address as usize;
        if index >= MEMORY_SIZE {
            return Err(Error::MemoryOutOfBounds {
                address: u32::from(address),
                length: 1,
            });
        }
        self.memory[index] = value;
        Ok(())
    }

    pub fn opcode_at(&self, address: u16) -> Result<u16, Error> {
        let index = address as usize;
        if index + 1 >= MEMORY_SIZE {
            return Err(Error::PcOutOfBounds(address));
        }
        Ok((u16::from(self.memory[index]) << 8) | u16::from(self.memory[index + 1]))
    }

    pub fn is_halted(&self) -> bool {
        match self.opcode_at(self.pc) {
            Ok(opcode) => opcode == 0x1000 | self.pc,
            Err(_) => false,
        }
    }

    pub fn tick_timers(&mut self) {
        self.delay_timer = self.delay_timer.saturating_sub(1);
        self.sound_timer = self.sound_timer.saturating_sub(1);
    }

    pub fn run_frame(&mut self, cycles_per_frame: usize) -> Result<usize, Error> {
        let mut executed = 0;
        for _ in 0..cycles_per_frame {
            if self.is_halted() {
                break;
            }
            self.step()?;
            executed += 1;
        }
        self.tick_timers();
        Ok(executed)
    }

    pub fn run_until_halt(&mut self, max_cycles: usize) -> Result<usize, Error> {
        for executed in 0..max_cycles {
            if self.is_halted() {
                return Ok(executed);
            }
            self.step()?;
        }
        Err(Error::NotHalted { cycles: max_cycles })
    }

    pub fn step(&mut self) -> Result<u16, Error> {
        let address = self.pc;
        let opcode = self.opcode_at(address)?;
        self.pc = address.wrapping_add(2) & 0x0FFF;
        self.cycles += 1;
        self.execute(address, opcode)?;
        Ok(opcode)
    }

    fn skip(&mut self) {
        self.pc = self.pc.wrapping_add(2) & 0x0FFF;
    }

    fn slice_bounds(&self, start: u16, length: usize) -> Result<(usize, usize), Error> {
        let begin = start as usize;
        let end = begin + length;
        if end > MEMORY_SIZE {
            return Err(Error::MemoryOutOfBounds {
                address: u32::from(start),
                length,
            });
        }
        Ok((begin, end))
    }

    fn execute(&mut self, address: u16, opcode: u16) -> Result<(), Error> {
        let x = ((opcode & 0x0F00) >> 8) as usize;
        let y = ((opcode & 0x00F0) >> 4) as usize;
        let n = (opcode & 0x000F) as u8;
        let nn = (opcode & 0x00FF) as u8;
        let nnn = opcode & 0x0FFF;
        let unknown = Error::UnknownOpcode { address, opcode };

        match opcode & 0xF000 {
            0x0000 => match opcode {
                0x00E0 => self.display.clear(),
                0x00EE => {
                    if self.sp == 0 {
                        return Err(Error::StackUnderflow(address));
                    }
                    self.sp -= 1;
                    self.pc = self.stack[self.sp];
                }
                _ => return Err(unknown),
            },
            0x1000 => self.pc = nnn,
            0x2000 => {
                if self.sp >= STACK_DEPTH {
                    return Err(Error::StackOverflow(address));
                }
                self.stack[self.sp] = self.pc;
                self.sp += 1;
                self.pc = nnn;
            }
            0x3000 => {
                if self.v[x] == nn {
                    self.skip();
                }
            }
            0x4000 => {
                if self.v[x] != nn {
                    self.skip();
                }
            }
            0x5000 => {
                if n != 0 {
                    return Err(unknown);
                }
                if self.v[x] == self.v[y] {
                    self.skip();
                }
            }
            0x6000 => self.v[x] = nn,
            0x7000 => self.v[x] = self.v[x].wrapping_add(nn),
            0x8000 => {
                let vx = self.v[x];
                let vy = self.v[y];
                match n {
                    0x0 => self.v[x] = vy,
                    0x1 => self.v[x] = vx | vy,
                    0x2 => self.v[x] = vx & vy,
                    0x3 => self.v[x] = vx ^ vy,
                    0x4 => {
                        let (result, carry) = vx.overflowing_add(vy);
                        self.v[x] = result;
                        self.v[0x0F] = u8::from(carry);
                    }
                    0x5 => {
                        let (result, borrow) = vx.overflowing_sub(vy);
                        self.v[x] = result;
                        self.v[0x0F] = u8::from(!borrow);
                    }
                    0x6 => {
                        let source = if self.quirks.shift_uses_vy { vy } else { vx };
                        self.v[x] = source >> 1;
                        self.v[0x0F] = source & 0x01;
                    }
                    0x7 => {
                        let (result, borrow) = vy.overflowing_sub(vx);
                        self.v[x] = result;
                        self.v[0x0F] = u8::from(!borrow);
                    }
                    0xE => {
                        let source = if self.quirks.shift_uses_vy { vy } else { vx };
                        self.v[x] = source << 1;
                        self.v[0x0F] = (source & 0x80) >> 7;
                    }
                    _ => return Err(unknown),
                }
            }
            0x9000 => {
                if n != 0 {
                    return Err(unknown);
                }
                if self.v[x] != self.v[y] {
                    self.skip();
                }
            }
            0xA000 => self.i = nnn,
            0xB000 => self.pc = (nnn + u16::from(self.v[0])) & 0x0FFF,
            0xC000 => self.v[x] = self.rng.next_byte() & nn,
            0xD000 => {
                let length = usize::from(n);
                let (begin, end) = self.slice_bounds(self.i, length)?;
                let mut sprite = [0u8; 15];
                sprite[..length].copy_from_slice(&self.memory[begin..end]);
                let collision = self.display.draw_sprite(self.v[x], self.v[y], &sprite[..length]);
                self.v[0x0F] = u8::from(collision);
            }
            0xE000 => match nn {
                0x9E => {
                    if self.key(usize::from(self.v[x] & 0x0F)) {
                        self.skip();
                    }
                }
                0xA1 => {
                    if !self.key(usize::from(self.v[x] & 0x0F)) {
                        self.skip();
                    }
                }
                _ => return Err(unknown),
            },
            0xF000 => match nn {
                0x07 => self.v[x] = self.delay_timer,
                0x0A => match self.keys.iter().position(|pressed| *pressed) {
                    Some(key) => {
                        self.v[x] = key as u8;
                        self.waiting_for_key = false;
                    }
                    None => {
                        self.waiting_for_key = true;
                        self.pc = address;
                    }
                },
                0x15 => self.delay_timer = self.v[x],
                0x18 => self.sound_timer = self.v[x],
                0x1E => self.i = self.i.wrapping_add(u16::from(self.v[x])),
                0x29 => self.i = glyph_address(self.v[x]),
                0x33 => {
                    let (begin, _) = self.slice_bounds(self.i, 3)?;
                    let value = self.v[x];
                    self.memory[begin] = value / 100;
                    self.memory[begin + 1] = (value / 10) % 10;
                    self.memory[begin + 2] = value % 10;
                }
                0x55 => {
                    let length = x + 1;
                    let (begin, _) = self.slice_bounds(self.i, length)?;
                    for offset in 0..length {
                        self.memory[begin + offset] = self.v[offset];
                    }
                    if self.quirks.load_store_increments_i {
                        self.i = self.i.wrapping_add(length as u16);
                    }
                }
                0x65 => {
                    let length = x + 1;
                    let (begin, _) = self.slice_bounds(self.i, length)?;
                    for offset in 0..length {
                        self.v[offset] = self.memory[begin + offset];
                    }
                    if self.quirks.load_store_increments_i {
                        self.i = self.i.wrapping_add(length as u16);
                    }
                }
                _ => return Err(unknown),
            },
            _ => return Err(unknown),
        }
        Ok(())
    }

    pub fn state_summary(&self) -> String {
        let mut out = String::new();
        for row in 0..4 {
            for column in 0..4 {
                let index = row * 4 + column;
                out.push_str(&format!("V{index:X}={:3}  ", self.v[index]));
            }
            out.push('\n');
        }
        out.push_str(&format!(
            "I={:03X}  PC={:03X}  SP={}  DT={}  ST={}  cycles={}  lit={}/{}",
            self.i,
            self.pc,
            self.sp,
            self.delay_timer,
            self.sound_timer,
            self.cycles,
            self.display.lit_count(),
            DISPLAY_WIDTH * DISPLAY_HEIGHT
        ));
        out
    }
}

impl Default for Chip8 {
    fn default() -> Self {
        Chip8::new()
    }
}
