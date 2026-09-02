use chip8_emulator::{
    assemble, Chip8, Error, Quirks, DEFAULT_CYCLES_PER_FRAME, PROGRAM_START,
};

const FONT_PARADE: &str = "
    LD V0, 0
    LD V1, 2
    LD V2, 4
next:
    LD F, V0
    DRW V1, V2, 5
    ADD V0, 1
    ADD V1, 7
    SE V0, 8
    JP check
    LD V1, 2
    LD V2, 16
check:
    SE V0, 16
    JP next
halt:
    JP halt
";

const ALU_EXERCISER: &str = "
    LD V0, 200
    LD V1, 100
    ADD V0, V1
    LD V2, VF
    LD V3, 10
    LD V4, 25
    SUB V3, V4
    LD V5, VF
    LD V6, 0x81
    SHR V6
    LD V7, VF
    LD V8, 0x81
    SHL V8
    LD V9, VF
    LD VA, 0x0F
    LD VB, 0xF0
    OR VA, VB
    LD VC, 0x3C
    LD VD, 0x0F
    AND VC, VD
    LD VE, 0xAA
    XOR VE, VD
halt:
    JP halt
";

const TIMER_LOOP: &str = "
    LD V0, 5
    LD DT, V0
wait:
    LD V1, DT
    SE V1, 0
    JP wait
    LD V2, 0xAB
halt:
    JP halt
";

const COLLISION: &str = "
    LD I, block
    LD V0, 10
    LD V1, 8
    DRW V0, V1, 4
    LD V4, VF
    LD V0, 14
    DRW V0, V1, 4
    LD V5, VF
    LD V0, 12
    DRW V0, V1, 4
    LD V6, VF
halt:
    JP halt
block:
    DB 0xF0, 0x90, 0x90, 0xF0
";

const BCD_AND_SPAN: &str = "
    LD V0, 255
    LD I, buffer
    LD B, V0
    LD I, buffer
    LD V2, [I]
    LD I, buffer
    LD V3, [I]
halt:
    JP halt
buffer:
    DB 0, 0, 0
";

const KEY_WAIT: &str = "
    LD V0, K
    LD V1, 0xFF
halt:
    JP halt
";

const EDGE_CLIPPING: &str = "
    LD I, bar
    LD V0, 62
    LD V1, 30
    DRW V0, V1, 4
    LD V2, VF
    LD V0, 70
    LD V1, 3
    DRW V0, V1, 4
    LD V3, VF
halt:
    JP halt
bar:
    DB 0xFF, 0xFF, 0xFF, 0xFF
";

fn banner(title: &str) {
    println!("\n{}", "=".repeat(78));
    println!("  {title}");
    println!("{}", "=".repeat(78));
}

fn rom_preview(rom: &[u8]) -> String {
    rom.chunks(2)
        .take(8)
        .enumerate()
        .map(|(index, pair)| {
            let address = PROGRAM_START as usize + index * 2;
            let word = if pair.len() == 2 {
                (u16::from(pair[0]) << 8) | u16::from(pair[1])
            } else {
                u16::from(pair[0]) << 8
            };
            format!("{address:03X}:{word:04X}")
        })
        .collect::<Vec<_>>()
        .join(" ")
}

fn build(source: &str) -> Result<(Chip8, Vec<u8>), Error> {
    let rom = assemble(source)?;
    let mut machine = Chip8::with_seed(0xC0FFEE);
    machine.load_rom(&rom)?;
    Ok((machine, rom))
}

fn font_parade() -> Result<(), Error> {
    banner("Demo 1: hexadecimal font sprites drawn across the framebuffer");
    let (mut machine, rom) = build(FONT_PARADE)?;
    println!("rom: {} bytes   {}", rom.len(), rom_preview(&rom));
    let cycles = machine.run_until_halt(4000)?;
    println!("{}", machine.display().to_text());
    println!("instructions executed: {cycles}");
    println!("{}", machine.state_summary());
    Ok(())
}

fn alu_exerciser() -> Result<(), Error> {
    banner("Demo 2: ALU opcodes and VF flag semantics");
    let (mut machine, rom) = build(ALU_EXERCISER)?;
    println!("rom: {} bytes   {}", rom.len(), rom_preview(&rom));
    let cycles = machine.run_until_halt(4000)?;
    println!("instructions executed: {cycles}");
    println!("  200 + 100 wraps to V0={} with carry V2={}", machine.v(0), machine.v(2));
    println!("  10 - 25 wraps to V3={} with borrow flag V5={}", machine.v(3), machine.v(5));
    println!("  0x81 >> 1 = 0x{:02X} shifted-out bit V7={}", machine.v(6), machine.v(7));
    println!("  0x81 << 1 = 0x{:02X} shifted-out bit V9={}", machine.v(8), machine.v(9));
    println!("  0x0F | 0xF0 = 0x{:02X}", machine.v(0xA));
    println!("  0x3C & 0x0F = 0x{:02X}", machine.v(0xC));
    println!("  0xAA ^ 0x0F = 0x{:02X}", machine.v(0xE));
    println!("{}", machine.state_summary());
    Ok(())
}

fn timer_loop() -> Result<(), Error> {
    banner("Demo 3: delay timer counting down at 60Hz, decoupled from cycle rate");
    let (mut machine, rom) = build(TIMER_LOOP)?;
    println!("rom: {} bytes   {}", rom.len(), rom_preview(&rom));
    let mut frames = 0;
    let mut executed = 0;
    while !machine.is_halted() && frames < 600 {
        executed += machine.run_frame(DEFAULT_CYCLES_PER_FRAME)?;
        frames += 1;
        println!("  frame {frames:2}: DT={} V1={} pc={:03X}", machine.delay_timer(), machine.v(1), machine.pc());
    }
    println!("halted after {frames} frames and {executed} instructions");
    println!("sentinel V2=0x{:02X} (0xAB means the wait loop exited)", machine.v(2));
    println!("{}", machine.state_summary());
    Ok(())
}

fn collision() -> Result<(), Error> {
    banner("Demo 4: XOR sprite drawing and the VF collision flag");
    let (mut machine, rom) = build(COLLISION)?;
    println!("rom: {} bytes   {}", rom.len(), rom_preview(&rom));
    let cycles = machine.run_until_halt(4000)?;
    println!("{}", machine.display().to_text());
    println!("instructions executed: {cycles}");
    println!("  draw at x=10 over empty space: VF={}", machine.v(4));
    println!("  draw at x=14 beside it:        VF={}", machine.v(5));
    println!("  draw at x=12 overlapping both: VF={}", machine.v(6));
    println!("  lit pixels remaining: {}", machine.display().lit_count());
    Ok(())
}

fn bcd_and_span() -> Result<(), Error> {
    banner("Demo 5: FX33 binary-coded decimal plus FX55/FX65 register spans");
    let (mut machine, rom) = build(BCD_AND_SPAN)?;
    println!("rom: {} bytes   {}", rom.len(), rom_preview(&rom));
    let cycles = machine.run_until_halt(4000)?;
    println!("instructions executed: {cycles}");
    println!(
        "  BCD of 255 loaded back as V0={} V1={} V2={}",
        machine.v(0),
        machine.v(1),
        machine.v(2)
    );
    println!(
        "  the second load reads a four-register span, so V3={} (the padding byte)",
        machine.v(3)
    );
    println!("  quirks in effect: {:?}", machine.quirks());
    Ok(())
}

fn key_wait() -> Result<(), Error> {
    banner("Demo 6: FX0A blocking until a key is pressed");
    let (mut machine, rom) = build(KEY_WAIT)?;
    println!("rom: {} bytes   {}", rom.len(), rom_preview(&rom));
    for frame in 1..=3 {
        machine.run_frame(DEFAULT_CYCLES_PER_FRAME)?;
        println!(
            "  frame {frame}: pc={:03X} waiting={} V0={}",
            machine.pc(),
            machine.waiting_for_key(),
            machine.v(0)
        );
    }
    machine.set_key(7, true);
    println!("  key 7 pressed");
    machine.run_frame(DEFAULT_CYCLES_PER_FRAME)?;
    println!(
        "  after release of the block: pc={:03X} waiting={} V0={} V1=0x{:02X}",
        machine.pc(),
        machine.waiting_for_key(),
        machine.v(0),
        machine.v(1)
    );
    println!("total instructions executed: {}", machine.cycles());
    Ok(())
}

fn edge_clipping() -> Result<(), Error> {
    banner("Demo 7: sprite origins wrap, sprite bodies clip at the screen edge");
    let (mut machine, rom) = build(EDGE_CLIPPING)?;
    println!("rom: {} bytes   {}", rom.len(), rom_preview(&rom));
    let cycles = machine.run_until_halt(4000)?;
    println!("{}", machine.display().to_text());
    println!("instructions executed: {cycles}");
    println!("  an 8x4 block drawn at (62,30) keeps only 2 columns and 2 rows on screen");
    println!("  an 8x4 block drawn at x=70 wraps its origin to x=6");
    println!("  lit pixels: {} (2*2 clipped + 8*4 wrapped = 36)", machine.display().lit_count());
    println!("  collision flags: V2={} V3={}", machine.v(2), machine.v(3));
    Ok(())
}

fn quirk_comparison() -> Result<(), Error> {
    banner("Demo 8: configurable quirks change 8XY6 and FX55/FX65 behaviour");
    let rom = assemble("    LD V1, 0x0F\n    LD V2, 0xF0\n    SHR V1, V2\nhalt:\n    JP halt\n")?;
    for quirks in [Quirks::original(), Quirks::modern()] {
        let mut machine = Chip8::with_seed(1);
        machine.set_quirks(quirks);
        machine.load_rom(&rom)?;
        machine.run_until_halt(100)?;
        println!(
            "  shift_uses_vy={:<5} -> V1=0x{:02X} VF={}",
            quirks.shift_uses_vy,
            machine.v(1),
            machine.v(0xF)
        );
    }
    Ok(())
}

fn error_handling() {
    banner("Demo 9: malformed input is reported as an error, never a panic");
    let mut machine = Chip8::with_seed(1);
    match machine.load_rom(&[0u8; 4000]) {
        Ok(()) => println!("  unexpectedly accepted an oversized rom"),
        Err(error) => println!("  oversized rom: {error}"),
    }
    let mut machine = Chip8::with_seed(1);
    machine.load_rom(&[0x80, 0x18]).expect("rom fits");
    match machine.step() {
        Ok(opcode) => println!("  unexpectedly executed opcode {opcode:04X}"),
        Err(error) => println!("  bad opcode:    {error}"),
    }
    let mut machine = Chip8::with_seed(1);
    machine.load_rom(&[0x00, 0xEE]).expect("rom fits");
    match machine.step() {
        Ok(_) => println!("  unexpectedly returned from an empty stack"),
        Err(error) => println!("  empty stack:   {error}"),
    }
    let rom = assemble("deep:\n    CALL deep\n").expect("assembles");
    let mut machine = Chip8::with_seed(1);
    machine.load_rom(&rom).expect("rom fits");
    let mut depth = 0;
    loop {
        match machine.step() {
            Ok(_) => depth += 1,
            Err(error) => {
                println!("  runaway recursion after {depth} calls: {error}");
                break;
            }
        }
    }
    match assemble("    LD V0, VZ\n") {
        Ok(_) => println!("  unexpectedly assembled a bad register"),
        Err(error) => println!("  bad assembly:  {error}"),
    }
}

fn main() {
    let demos: [(&str, fn() -> Result<(), Error>); 8] = [
        ("font parade", font_parade),
        ("alu exerciser", alu_exerciser),
        ("timer loop", timer_loop),
        ("collision", collision),
        ("bcd and span", bcd_and_span),
        ("key wait", key_wait),
        ("edge clipping", edge_clipping),
        ("quirk comparison", quirk_comparison),
    ];
    for (name, demo) in demos {
        if let Err(error) = demo() {
            eprintln!("demo `{name}` failed: {error}");
            std::process::exit(1);
        }
    }
    error_handling();
    println!("\nall demos completed");
}
