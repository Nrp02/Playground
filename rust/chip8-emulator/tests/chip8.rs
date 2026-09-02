use chip8_emulator::{
    assemble, Chip8, Error, Quirks, DISPLAY_HEIGHT, DISPLAY_WIDTH, FONT_SET, FONT_START,
    MEMORY_SIZE, PROGRAM_START, STACK_DEPTH,
};

fn boot(source: &str) -> Chip8 {
    let rom = assemble(source).expect("source assembles");
    let mut machine = Chip8::with_seed(0xABCD_EF01);
    machine.load_rom(&rom).expect("rom fits");
    machine
}

fn boot_raw(rom: &[u8]) -> Chip8 {
    let mut machine = Chip8::with_seed(0xABCD_EF01);
    machine.load_rom(rom).expect("rom fits");
    machine
}

fn read_span(machine: &Chip8, start: u16, length: u16) -> Vec<u8> {
    (0..length)
        .map(|offset| machine.read_memory(start + offset).expect("in bounds"))
        .collect()
}

#[test]
fn assembler_encodes_the_full_instruction_table() {
    let cases: [(&str, u16); 26] = [
        ("CLS", 0x00E0),
        ("RET", 0x00EE),
        ("JP 0x321", 0x1321),
        ("CALL 0x456", 0x2456),
        ("SE V3, 0x2A", 0x332A),
        ("SNE V3, 0x2A", 0x432A),
        ("SE V3, V7", 0x5370),
        ("LD V9, 0xBE", 0x69BE),
        ("ADD V9, 0x01", 0x7901),
        ("LD V1, V2", 0x8120),
        ("OR V1, V2", 0x8121),
        ("AND V1, V2", 0x8122),
        ("XOR V1, V2", 0x8123),
        ("ADD V1, V2", 0x8124),
        ("SUB V1, V2", 0x8125),
        ("SHR V1, V2", 0x8126),
        ("SUBN V1, V2", 0x8127),
        ("SHL V1, V2", 0x812E),
        ("SNE V4, V5", 0x9450),
        ("LD I, 0x2F0", 0xA2F0),
        ("JP V0, 0x100", 0xB100),
        ("RND VA, 0x0F", 0xCA0F),
        ("DRW V1, V2, 5", 0xD125),
        ("SKP VE", 0xEE9E),
        ("SKNP VE", 0xEEA1),
        ("LD VB, K", 0xFB0A),
    ];
    for (source, expected) in cases {
        let rom = assemble(source).unwrap_or_else(|error| panic!("{source}: {error}"));
        let actual = (u16::from(rom[0]) << 8) | u16::from(rom[1]);
        assert_eq!(actual, expected, "encoding of `{source}`");
    }

    let fx = [
        ("LD V2, DT", 0xF207u16),
        ("LD DT, V2", 0xF215),
        ("LD ST, V2", 0xF218),
        ("ADD I, V2", 0xF21E),
        ("LD F, V2", 0xF229),
        ("LD B, V2", 0xF233),
        ("LD [I], V2", 0xF255),
        ("LD V2, [I]", 0xF265),
    ];
    for (source, expected) in fx {
        let rom = assemble(source).unwrap_or_else(|error| panic!("{source}: {error}"));
        assert_eq!((u16::from(rom[0]) << 8) | u16::from(rom[1]), expected, "{source}");
    }
}

#[test]
fn assembler_resolves_labels_and_raw_bytes() {
    let rom = assemble(
        "start:\n    JP body\n    DB 0xAA, 0xBB, 3\nbody:\n    LD I, data\n    JP start\ndata:\n    DB 1, 2\n",
    )
    .expect("assembles");
    assert_eq!(rom.len(), 11);
    assert_eq!(rom[0], 0x12);
    assert_eq!(rom[1], 0x05);
    assert_eq!(&rom[2..5], &[0xAA, 0xBB, 3]);
    assert_eq!(rom[5], 0xA2);
    assert_eq!(rom[6], 0x09);
    assert_eq!(rom[7], 0x12);
    assert_eq!(rom[8], 0x00);
    assert_eq!(&rom[9..], &[1, 2]);
}

#[test]
fn assembler_reports_bad_input() {
    for source in [
        "FOO V0, 1",
        "LD V0",
        "JP",
        "LD V0, missing_label",
        "DRW V0, V1, 0x20",
        "LD V0, 0x1FF",
        "loop:\nloop:\n    RET",
        "JP 0x1234",
    ] {
        match assemble(source) {
            Err(Error::Assembly { .. }) => {}
            other => panic!("expected an assembly error for `{source}`, got {other:?}"),
        }
    }
}

#[test]
fn call_and_return_update_pc_and_stack() {
    let mut machine = boot(
        "    CALL sub\n    LD V1, 0x22\nhalt:\n    JP halt\nsub:\n    LD V0, 0x11\n    RET\n",
    );
    assert_eq!(machine.pc(), 0x200);
    machine.step().expect("call");
    assert_eq!(machine.pc(), 0x206);
    assert_eq!(machine.sp(), 1);
    assert_eq!(machine.stack(), &[0x202]);
    machine.step().expect("load");
    machine.step().expect("ret");
    assert_eq!(machine.pc(), 0x202);
    assert_eq!(machine.sp(), 0);
    assert!(machine.stack().is_empty());
    machine.run_until_halt(20).expect("halts");
    assert_eq!(machine.v(0), 0x11);
    assert_eq!(machine.v(1), 0x22);
}

#[test]
fn jump_with_offset_lands_on_the_computed_address() {
    let mut machine = boot(
        "    LD V0, 4\n    JP V0, base\nbase:\n    LD V1, 0x11\n    LD V2, 0x22\n    LD V3, 0x33\nhalt:\n    JP halt\n",
    );
    machine.run_until_halt(50).expect("halts");
    assert_eq!(machine.v(1), 0);
    assert_eq!(machine.v(2), 0);
    assert_eq!(machine.v(3), 0x33);
}

#[test]
fn skip_instructions_take_the_right_branch() {
    let mut machine = boot(
        "    LD V0, 5\n    LD V1, 5\n    SE V0, 5\n    LD V2, 0xFF\n    SNE V0, 5\n    LD V3, 0xFF\n    SE V0, V1\n    LD V4, 0xFF\n    SNE V0, V1\n    LD V5, 0xFF\nhalt:\n    JP halt\n",
    );
    machine.run_until_halt(50).expect("halts");
    assert_eq!(machine.v(2), 0x00);
    assert_eq!(machine.v(3), 0xFF);
    assert_eq!(machine.v(4), 0x00);
    assert_eq!(machine.v(5), 0xFF);
}

#[test]
fn alu_logic_and_copy_operations() {
    let mut machine = boot(
        "    LD V0, 0x0F\n    LD V1, 0xF0\n    LD V2, V0\n    OR V2, V1\n    LD V3, 0x3C\n    AND V3, V0\n    LD V4, 0xAA\n    XOR V4, V0\nhalt:\n    JP halt\n",
    );
    machine.run_until_halt(50).expect("halts");
    assert_eq!(machine.v(2), 0xFF);
    assert_eq!(machine.v(3), 0x0C);
    assert_eq!(machine.v(4), 0xA5);
}

#[test]
fn add_sets_carry_and_sub_sets_borrow() {
    let mut machine = boot(
        "    LD V0, 200\n    LD V1, 100\n    ADD V0, V1\n    LD V2, VF\n    LD V3, 100\n    LD V4, 100\n    ADD V3, V4\n    LD V5, VF\n    LD V6, 10\n    LD V7, 25\n    SUB V6, V7\n    LD V8, VF\n    LD V9, 25\n    LD VA, 10\n    SUB V9, VA\n    LD VB, VF\nhalt:\n    JP halt\n",
    );
    machine.run_until_halt(80).expect("halts");
    assert_eq!(machine.v(0), 44);
    assert_eq!(machine.v(2), 1);
    assert_eq!(machine.v(3), 200);
    assert_eq!(machine.v(5), 0);
    assert_eq!(machine.v(6), 241);
    assert_eq!(machine.v(8), 0);
    assert_eq!(machine.v(9), 15);
    assert_eq!(machine.v(0xB), 1);
}

#[test]
fn subn_reverses_the_operands() {
    let mut machine = boot(
        "    LD V0, 10\n    LD V1, 25\n    SUBN V0, V1\n    LD V2, VF\n    LD V3, 25\n    LD V4, 10\n    SUBN V3, V4\n    LD V5, VF\nhalt:\n    JP halt\n",
    );
    machine.run_until_halt(50).expect("halts");
    assert_eq!(machine.v(0), 15);
    assert_eq!(machine.v(2), 1);
    assert_eq!(machine.v(3), 241);
    assert_eq!(machine.v(5), 0);
}

#[test]
fn shifts_report_the_bit_that_left_the_register() {
    let source = "    LD V1, 0x81\n    LD V2, 0x81\n    SHR V1, V1\n    LD V3, VF\n    SHL V2, V2\n    LD V4, VF\nhalt:\n    JP halt\n";
    let mut machine = boot(source);
    machine.run_until_halt(50).expect("halts");
    assert_eq!(machine.v(1), 0x40);
    assert_eq!(machine.v(3), 1);
    assert_eq!(machine.v(2), 0x02);
    assert_eq!(machine.v(4), 1);

    let mut even = boot("    LD V1, 0x10\n    SHR V1, V1\n    LD V2, VF\nhalt:\n    JP halt\n");
    even.run_until_halt(50).expect("halts");
    assert_eq!(even.v(1), 0x08);
    assert_eq!(even.v(2), 0);
}

#[test]
fn shift_source_follows_the_configured_quirk() {
    let rom = assemble("    LD V1, 0x0F\n    LD V2, 0xF0\n    SHR V1, V2\nhalt:\n    JP halt\n")
        .expect("assembles");

    let mut original = Chip8::with_seed(1);
    original.set_quirks(Quirks::original());
    original.load_rom(&rom).expect("rom fits");
    original.run_until_halt(50).expect("halts");
    assert_eq!(original.v(1), 0x78);
    assert_eq!(original.v(0xF), 0);

    let mut modern = Chip8::with_seed(1);
    modern.set_quirks(Quirks::modern());
    modern.load_rom(&rom).expect("rom fits");
    modern.run_until_halt(50).expect("halts");
    assert_eq!(modern.v(1), 0x07);
    assert_eq!(modern.v(0xF), 1);
}

#[test]
fn vf_is_written_after_the_result_when_it_is_the_destination() {
    let mut carry = boot("    LD VF, 200\n    LD V1, 100\n    ADD VF, V1\nhalt:\n    JP halt\n");
    carry.run_until_halt(20).expect("halts");
    assert_eq!(carry.v(0xF), 1);

    let mut borrow = boot("    LD VF, 10\n    LD V1, 25\n    SUB VF, V1\nhalt:\n    JP halt\n");
    borrow.run_until_halt(20).expect("halts");
    assert_eq!(borrow.v(0xF), 0);

    let mut shift = boot("    LD VF, 0x81\n    SHR VF, VF\nhalt:\n    JP halt\n");
    shift.run_until_halt(20).expect("halts");
    assert_eq!(shift.v(0xF), 1);

    let mut source = boot("    LD V0, 1\n    LD VF, 200\n    ADD V0, VF\nhalt:\n    JP halt\n");
    source.run_until_halt(20).expect("halts");
    assert_eq!(source.v(0), 201);
    assert_eq!(source.v(0xF), 0);
}

#[test]
fn bcd_splits_a_byte_into_three_digits() {
    let source = "    LD I, buffer\n    LD B, V0\nhalt:\n    JP halt\nbuffer:\n    DB 0, 0, 0\n";
    for (value, expected) in [
        (0u8, [0u8, 0, 0]),
        (9, [0, 0, 9]),
        (10, [0, 1, 0]),
        (100, [1, 0, 0]),
        (123, [1, 2, 3]),
        (255, [2, 5, 5]),
    ] {
        let mut machine = boot(source);
        machine.set_register(0, value);
        machine.run_until_halt(20).expect("halts");
        let digits = read_span(&machine, PROGRAM_START + 6, 3);
        assert_eq!(digits, expected.to_vec(), "bcd of {value}");
    }
}

#[test]
fn register_store_and_load_cover_the_right_span() {
    let store = "    LD V0, 0x11\n    LD V1, 0x22\n    LD V2, 0x33\n    LD V3, 0x44\n    LD I, buffer\n    LD [I], V2\nhalt:\n    JP halt\nbuffer:\n    DB 0, 0, 0, 0, 0\n";
    let buffer = PROGRAM_START + 14;

    let mut machine = boot(store);
    machine.run_until_halt(40).expect("halts");
    assert_eq!(read_span(&machine, buffer, 5), vec![0x11, 0x22, 0x33, 0x00, 0x00]);
    assert_eq!(machine.index(), buffer + 3);

    let rom = assemble(store).expect("assembles");
    let mut modern = Chip8::with_seed(1);
    modern.set_quirks(Quirks::modern());
    modern.load_rom(&rom).expect("rom fits");
    modern.run_until_halt(40).expect("halts");
    assert_eq!(read_span(&modern, buffer, 5), vec![0x11, 0x22, 0x33, 0x00, 0x00]);
    assert_eq!(modern.index(), buffer);

    let load = "    LD I, data\n    LD V2, [I]\nhalt:\n    JP halt\ndata:\n    DB 0xAA, 0xBB, 0xCC, 0xDD\n";
    let mut reader = boot(load);
    reader.run_until_halt(20).expect("halts");
    assert_eq!(reader.v(0), 0xAA);
    assert_eq!(reader.v(1), 0xBB);
    assert_eq!(reader.v(2), 0xCC);
    assert_eq!(reader.v(3), 0x00);
    assert_eq!(reader.index(), PROGRAM_START + 6 + 3);
}

#[test]
fn add_to_index_and_font_lookup() {
    let mut machine = boot("    LD I, 0x300\n    LD V0, 0x20\n    ADD I, V0\nhalt:\n    JP halt\n");
    machine.run_until_halt(20).expect("halts");
    assert_eq!(machine.index(), 0x320);

    for digit in 0u8..16 {
        let mut fonts = boot("    LD F, V0\nhalt:\n    JP halt\n");
        fonts.set_register(0, digit);
        fonts.run_until_halt(20).expect("halts");
        let expected = FONT_START + u16::from(digit) * 5;
        assert_eq!(fonts.index(), expected, "font address for {digit:X}");
        let glyph = read_span(&fonts, expected, 5);
        let base = usize::from(digit) * 5;
        assert_eq!(glyph, FONT_SET[base..base + 5].to_vec());
    }
}

#[test]
fn drawing_xors_pixels_and_flags_collisions() {
    let mut machine = boot(
        "    LD I, dot\n    LD V0, 0\n    LD V1, 0\n    DRW V0, V1, 1\n    LD V2, VF\n    DRW V0, V1, 1\n    LD V3, VF\nhalt:\n    JP halt\ndot:\n    DB 0x80\n",
    );
    machine.step().expect("ld i");
    machine.step().expect("ld v0");
    machine.step().expect("ld v1");
    machine.step().expect("draw");
    assert!(machine.display().get(0, 0));
    assert_eq!(machine.display().lit_count(), 1);
    machine.run_until_halt(20).expect("halts");
    assert_eq!(machine.v(2), 0);
    assert_eq!(machine.v(3), 1);
    assert!(!machine.display().get(0, 0));
    assert!(machine.display().is_blank());
}

#[test]
fn partial_overlap_sets_the_collision_flag_once() {
    let mut machine = boot(
        "    LD I, block\n    LD V0, 10\n    LD V1, 8\n    DRW V0, V1, 4\n    LD V4, VF\n    LD V0, 14\n    DRW V0, V1, 4\n    LD V5, VF\n    LD V0, 12\n    DRW V0, V1, 4\n    LD V6, VF\nhalt:\n    JP halt\nblock:\n    DB 0xF0, 0x90, 0x90, 0xF0\n",
    );
    machine.run_until_halt(60).expect("halts");
    assert_eq!(machine.v(4), 0);
    assert_eq!(machine.v(5), 0);
    assert_eq!(machine.v(6), 1);
}

#[test]
fn sprites_clip_at_the_edges_but_origins_wrap() {
    let mut clipped = boot(
        "    LD I, bar\n    LD V0, 62\n    LD V1, 31\n    DRW V0, V1, 3\n    LD V2, VF\nhalt:\n    JP halt\nbar:\n    DB 0xFF, 0xFF, 0xFF\n",
    );
    clipped.run_until_halt(30).expect("halts");
    assert_eq!(clipped.display().lit_count(), 2);
    assert!(clipped.display().get(62, 31));
    assert!(clipped.display().get(63, 31));
    assert!(!clipped.display().get(0, 31));
    assert!(!clipped.display().get(62, 0));
    assert_eq!(clipped.v(2), 0);

    let mut wrapped = boot(
        "    LD I, bar\n    LD V0, 70\n    LD V1, 35\n    DRW V0, V1, 1\nhalt:\n    JP halt\nbar:\n    DB 0xFF\n",
    );
    wrapped.run_until_halt(30).expect("halts");
    assert_eq!(wrapped.display().lit_count(), 8);
    assert!(wrapped.display().get(6, 3));
    assert!(wrapped.display().get(13, 3));
}

#[test]
fn clear_screen_blanks_the_framebuffer() {
    let mut machine = boot(
        "    LD I, bar\n    LD V0, 4\n    LD V1, 4\n    DRW V0, V1, 4\n    CLS\nhalt:\n    JP halt\nbar:\n    DB 0xFF, 0xFF, 0xFF, 0xFF\n",
    );
    machine.run_until_halt(30).expect("halts");
    assert!(machine.display().is_blank());
    assert_eq!(machine.display().pixels().len(), DISPLAY_WIDTH * DISPLAY_HEIGHT);
}

#[test]
fn key_skips_follow_the_keypad_state() {
    let source = "    SKP V0\n    LD V1, 1\n    SKNP V0\n    LD V2, 1\nhalt:\n    JP halt\n";

    let mut pressed = boot(source);
    pressed.set_register(0, 3);
    pressed.set_key(3, true);
    pressed.run_until_halt(30).expect("halts");
    assert_eq!(pressed.v(1), 0);
    assert_eq!(pressed.v(2), 1);

    let mut idle = boot(source);
    idle.set_register(0, 3);
    idle.run_until_halt(30).expect("halts");
    assert_eq!(idle.v(1), 1);
    assert_eq!(idle.v(2), 0);
}

#[test]
fn key_wait_blocks_until_a_key_arrives() {
    let mut machine = boot("    LD V5, K\n    LD V6, 0x77\nhalt:\n    JP halt\n");
    for _ in 0..5 {
        machine.step().expect("blocked step");
        assert_eq!(machine.pc(), 0x200);
        assert!(machine.waiting_for_key());
        assert_eq!(machine.v(5), 0);
    }
    assert_eq!(machine.cycles(), 5);

    machine.set_key(0x0A, true);
    machine.step().expect("key accepted");
    assert!(!machine.waiting_for_key());
    assert_eq!(machine.v(5), 0x0A);
    assert_eq!(machine.pc(), 0x202);
    machine.run_until_halt(20).expect("halts");
    assert_eq!(machine.v(6), 0x77);

    machine.release_all_keys();
    assert!(!machine.key(0x0A));
}

#[test]
fn timers_count_down_to_zero_and_stop() {
    let mut machine = Chip8::with_seed(1);
    machine.set_delay_timer(3);
    machine.set_sound_timer(1);
    machine.tick_timers();
    assert_eq!(machine.delay_timer(), 2);
    assert_eq!(machine.sound_timer(), 0);
    machine.tick_timers();
    machine.tick_timers();
    assert_eq!(machine.delay_timer(), 0);
    for _ in 0..5 {
        machine.tick_timers();
    }
    assert_eq!(machine.delay_timer(), 0);
    assert_eq!(machine.sound_timer(), 0);
}

#[test]
fn timers_are_decoupled_from_the_instruction_rate() {
    let mut machine = boot(
        "    LD V0, 3\n    LD DT, V0\n    LD V1, 9\n    LD ST, V1\nspin:\n    LD V2, DT\n    JP spin\n",
    );
    let executed = machine.run_frame(30).expect("frame runs");
    assert_eq!(executed, 30);
    assert_eq!(machine.cycles(), 30);
    assert_eq!(machine.delay_timer(), 2);
    assert_eq!(machine.sound_timer(), 8);

    machine.run_frame(30).expect("frame runs");
    machine.run_frame(30).expect("frame runs");
    assert_eq!(machine.cycles(), 90);
    assert_eq!(machine.delay_timer(), 0);
    assert_eq!(machine.sound_timer(), 6);
    assert_eq!(machine.v(2), 1);

    machine.run_frame(30).expect("frame runs");
    assert_eq!(machine.cycles(), 120);
    assert_eq!(machine.delay_timer(), 0);
    assert_eq!(machine.sound_timer(), 5);
    assert_eq!(machine.v(2), 0);
}

#[test]
fn a_delay_timer_wait_loop_terminates() {
    let mut machine = boot(
        "    LD V0, 4\n    LD DT, V0\nwait:\n    LD V1, DT\n    SE V1, 0\n    JP wait\n    LD V2, 0xAB\nhalt:\n    JP halt\n",
    );
    let mut frames = 0;
    while !machine.is_halted() && frames < 100 {
        machine.run_frame(12).expect("frame runs");
        frames += 1;
    }
    assert!(machine.is_halted());
    assert_eq!(machine.v(2), 0xAB);
    assert_eq!(frames, 5);
}

#[test]
fn random_is_seeded_and_masked() {
    let rom = assemble(
        "    RND V0, 0x0F\n    RND V1, 0x0F\n    RND V2, 0xFF\n    RND V3, 0x00\nhalt:\n    JP halt\n",
    )
    .expect("assembles");

    let mut first = Chip8::with_seed(0x5EED);
    first.load_rom(&rom).expect("rom fits");
    first.run_until_halt(20).expect("halts");

    let mut second = Chip8::with_seed(0x5EED);
    second.load_rom(&rom).expect("rom fits");
    second.run_until_halt(20).expect("halts");

    let mut different = Chip8::with_seed(0xD1FF);
    different.load_rom(&rom).expect("rom fits");
    different.run_until_halt(20).expect("halts");

    assert_eq!(first.registers(), second.registers());
    assert!(first.v(0) <= 0x0F);
    assert!(first.v(1) <= 0x0F);
    assert_eq!(first.v(3), 0);
    assert_ne!(
        (first.v(0), first.v(1), first.v(2)),
        (different.v(0), different.v(1), different.v(2))
    );
}

#[test]
fn unknown_opcodes_are_errors_not_panics() {
    for opcode in [0x00FFu16, 0x0123, 0x5001, 0x8008, 0x800F, 0x9001, 0xE000, 0xE0FF, 0xF0FF, 0xF001] {
        let mut machine = boot_raw(&[(opcode >> 8) as u8, opcode as u8]);
        match machine.step() {
            Err(Error::UnknownOpcode { address, opcode: seen }) => {
                assert_eq!(address, PROGRAM_START);
                assert_eq!(seen, opcode);
            }
            other => panic!("expected an unknown-opcode error for {opcode:04X}, got {other:?}"),
        }
    }
}

#[test]
fn oversized_roms_are_rejected() {
    let capacity = MEMORY_SIZE - PROGRAM_START as usize;
    let mut machine = Chip8::with_seed(1);
    assert!(machine.load_rom(&vec![0u8; capacity]).is_ok());
    match machine.load_rom(&vec![0u8; capacity + 1]) {
        Err(Error::RomTooLarge { size, capacity: reported }) => {
            assert_eq!(size, capacity + 1);
            assert_eq!(reported, capacity);
        }
        other => panic!("expected a rom-too-large error, got {other:?}"),
    }
}

#[test]
fn stack_overflow_and_underflow_are_reported() {
    let mut deep = boot("deep:\n    CALL deep\n");
    for _ in 0..STACK_DEPTH {
        deep.step().expect("call succeeds");
    }
    match deep.step() {
        Err(Error::StackOverflow(address)) => assert_eq!(address, PROGRAM_START),
        other => panic!("expected a stack overflow, got {other:?}"),
    }

    let mut empty = boot("    RET\n");
    match empty.step() {
        Err(Error::StackUnderflow(address)) => assert_eq!(address, PROGRAM_START),
        other => panic!("expected a stack underflow, got {other:?}"),
    }
}

#[test]
fn leaving_addressable_memory_is_reported() {
    let mut machine = boot("    JP 0xFFF\n");
    machine.step().expect("jump succeeds");
    match machine.step() {
        Err(Error::PcOutOfBounds(pc)) => assert_eq!(pc, 0x0FFF),
        other => panic!("expected a pc-out-of-bounds error, got {other:?}"),
    }

    let mut drawing = boot("    LD I, 0xFFE\n    LD V0, 0\n    LD V1, 0\n    DRW V0, V1, 5\n");
    for _ in 0..3 {
        drawing.step().expect("setup succeeds");
    }
    match drawing.step() {
        Err(Error::MemoryOutOfBounds { address, length }) => {
            assert_eq!(address, 0x0FFE);
            assert_eq!(length, 5);
        }
        other => panic!("expected a memory-out-of-bounds error, got {other:?}"),
    }

    let mut storing = boot("    LD I, 0xFFE\n    LD [I], V5\n");
    storing.step().expect("setup succeeds");
    match storing.step() {
        Err(Error::MemoryOutOfBounds { length, .. }) => assert_eq!(length, 6),
        other => panic!("expected a memory-out-of-bounds error, got {other:?}"),
    }

    let mut bcd = boot("    LD I, 0xFFF\n    LD B, V0\n");
    bcd.step().expect("setup succeeds");
    assert!(matches!(bcd.step(), Err(Error::MemoryOutOfBounds { .. })));
}

#[test]
fn a_program_that_never_halts_is_reported() {
    let mut machine = boot("    LD V0, 1\nloop:\n    ADD V0, 1\n    JP loop\n");
    match machine.run_until_halt(64) {
        Err(Error::NotHalted { cycles }) => assert_eq!(cycles, 64),
        other => panic!("expected a not-halted error, got {other:?}"),
    }
    assert_eq!(machine.cycles(), 64);
}

#[test]
fn the_font_is_present_in_low_memory_and_programs_can_draw_it() {
    let mut machine = Chip8::with_seed(1);
    let loaded = read_span(&machine, FONT_START, FONT_SET.len() as u16);
    assert_eq!(loaded, FONT_SET.to_vec());

    machine
        .load_rom(&assemble("    LD F, V0\n    LD V1, 0\n    DRW V1, V1, 5\nhalt:\n    JP halt\n").unwrap())
        .expect("rom fits");
    machine.set_register(0, 0);
    machine.run_until_halt(20).expect("halts");
    assert_eq!(machine.display().lit_count(), 14);
    assert!(machine.display().get(0, 0));
    assert!(machine.display().get(3, 0));
    assert!(!machine.display().get(1, 1));
}

#[test]
fn rendering_produces_a_framed_text_picture() {
    let mut machine = boot(
        "    LD I, dot\n    LD V0, 0\n    DRW V0, V0, 1\nhalt:\n    JP halt\ndot:\n    DB 0x80\n",
    );
    machine.run_until_halt(20).expect("halts");
    let text = machine.display().to_text();
    let lines: Vec<&str> = text.lines().collect();
    assert_eq!(lines.len(), DISPLAY_HEIGHT + 2);
    assert!(lines[0].starts_with('+') && lines[0].ends_with('+'));
    assert!(lines[1].starts_with("|██"));
    assert!(lines[2].starts_with("|  "));
    assert!(text.contains('█'));
}
