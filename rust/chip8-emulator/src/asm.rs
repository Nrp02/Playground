use crate::error::Error;
use crate::machine::PROGRAM_START;
use std::collections::HashMap;

#[derive(Debug, Clone, PartialEq, Eq)]
enum Operand {
    Reg(u8),
    Index,
    Delay,
    Sound,
    KeyWait,
    Font,
    Bcd,
    Indirect,
    Number(u16),
    Symbol(String),
}

struct Statement {
    line: usize,
    mnemonic: String,
    operands: Vec<String>,
}

fn fail<T>(line: usize, message: impl Into<String>) -> Result<T, Error> {
    Err(Error::Assembly {
        line,
        message: message.into(),
    })
}

fn parse_number(text: &str) -> Option<u16> {
    let upper = text.trim().to_ascii_uppercase();
    if let Some(rest) = upper.strip_prefix("0X") {
        return u16::from_str_radix(rest, 16).ok();
    }
    if let Some(rest) = upper.strip_prefix('#') {
        return u16::from_str_radix(rest, 16).ok();
    }
    if let Some(rest) = upper.strip_prefix('%') {
        return u16::from_str_radix(rest, 2).ok();
    }
    upper.parse::<u16>().ok()
}

fn is_symbol(text: &str) -> bool {
    let mut chars = text.chars();
    match chars.next() {
        Some(first) if first.is_ascii_alphabetic() || first == '_' => {}
        _ => return false,
    }
    chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
}

fn parse_operand(text: &str, line: usize) -> Result<Operand, Error> {
    let trimmed = text.trim();
    if trimmed.is_empty() {
        return fail(line, "empty operand");
    }
    let upper = trimmed.to_ascii_uppercase();
    match upper.as_str() {
        "I" => return Ok(Operand::Index),
        "DT" => return Ok(Operand::Delay),
        "ST" => return Ok(Operand::Sound),
        "K" => return Ok(Operand::KeyWait),
        "F" => return Ok(Operand::Font),
        "B" => return Ok(Operand::Bcd),
        "[I]" => return Ok(Operand::Indirect),
        _ => {}
    }
    let bytes = upper.as_bytes();
    if bytes.len() == 2 && bytes[0] == b'V' {
        if let Some(value) = (bytes[1] as char).to_digit(16) {
            return Ok(Operand::Reg(value as u8));
        }
    }
    if let Some(value) = parse_number(trimmed) {
        return Ok(Operand::Number(value));
    }
    if is_symbol(trimmed) {
        return Ok(Operand::Symbol(trimmed.to_ascii_lowercase()));
    }
    fail(line, format!("cannot parse operand `{trimmed}`"))
}

fn parse_statements(source: &str) -> Result<(Vec<Statement>, HashMap<String, usize>), Error> {
    let mut statements = Vec::new();
    let mut pending_labels: Vec<(String, usize)> = Vec::new();
    let mut label_targets: HashMap<String, usize> = HashMap::new();
    for (offset, raw) in source.lines().enumerate() {
        let line = offset + 1;
        let without_comment = match raw.find(';') {
            Some(position) => &raw[..position],
            None => raw,
        };
        let mut rest = without_comment.trim();
        while let Some(colon) = rest.find(':') {
            let name = rest[..colon].trim().to_ascii_lowercase();
            if name.is_empty() || !is_symbol(&name) {
                return fail(line, format!("invalid label `{name}`"));
            }
            if label_targets.contains_key(&name) || pending_labels.iter().any(|(n, _)| *n == name) {
                return fail(line, format!("duplicate label `{name}`"));
            }
            pending_labels.push((name, line));
            rest = rest[colon + 1..].trim();
        }
        if rest.is_empty() {
            continue;
        }
        let index = statements.len();
        for (name, _) in pending_labels.drain(..) {
            label_targets.insert(name, index);
        }
        let mut parts = rest.splitn(2, char::is_whitespace);
        let mnemonic = parts.next().unwrap_or("").to_ascii_uppercase();
        let operands = match parts.next() {
            Some(tail) if !tail.trim().is_empty() => {
                tail.split(',').map(|piece| piece.trim().to_string()).collect()
            }
            _ => Vec::new(),
        };
        statements.push(Statement {
            line,
            mnemonic,
            operands,
        });
    }
    let end = statements.len();
    for (name, _) in pending_labels {
        label_targets.insert(name, end);
    }
    Ok((statements, label_targets))
}

fn statement_size(statement: &Statement) -> usize {
    if statement.mnemonic == "DB" {
        statement.operands.len()
    } else {
        2
    }
}

struct Context<'a> {
    line: usize,
    labels: &'a HashMap<String, u16>,
}

impl Context<'_> {
    fn value(&self, operand: &Operand) -> Result<u16, Error> {
        match operand {
            Operand::Number(value) => Ok(*value),
            Operand::Symbol(name) => match self.labels.get(name) {
                Some(address) => Ok(*address),
                None => fail(self.line, format!("undefined label `{name}`")),
            },
            _ => fail(self.line, "expected a numeric or label operand"),
        }
    }

    fn address(&self, operand: &Operand) -> Result<u16, Error> {
        let value = self.value(operand)?;
        if value > 0x0FFF {
            return fail(self.line, format!("address {value:X} does not fit in 12 bits"));
        }
        Ok(value)
    }

    fn byte(&self, operand: &Operand) -> Result<u8, Error> {
        let value = self.value(operand)?;
        if value > 0xFF {
            return fail(self.line, format!("value {value:X} does not fit in a byte"));
        }
        Ok(value as u8)
    }

    fn nibble(&self, operand: &Operand) -> Result<u8, Error> {
        let value = self.value(operand)?;
        if value > 0x0F {
            return fail(self.line, format!("value {value:X} does not fit in a nibble"));
        }
        Ok(value as u8)
    }

    fn reg(&self, operand: &Operand) -> Result<u8, Error> {
        match operand {
            Operand::Reg(index) => Ok(*index),
            _ => fail(self.line, "expected a V register operand"),
        }
    }
}

fn encode(statement: &Statement, labels: &HashMap<String, u16>) -> Result<Vec<u8>, Error> {
    let line = statement.line;
    let context = Context { line, labels };
    let mut operands = Vec::with_capacity(statement.operands.len());
    for text in &statement.operands {
        operands.push(parse_operand(text, line)?);
    }
    let word = |value: u16| -> Result<Vec<u8>, Error> { Ok(vec![(value >> 8) as u8, value as u8]) };
    let arity = |expected: usize| -> Result<(), Error> {
        if operands.len() == expected {
            Ok(())
        } else {
            fail(
                line,
                format!(
                    "`{}` expects {expected} operand(s), got {}",
                    statement.mnemonic,
                    operands.len()
                ),
            )
        }
    };

    match statement.mnemonic.as_str() {
        "CLS" => {
            arity(0)?;
            word(0x00E0)
        }
        "RET" => {
            arity(0)?;
            word(0x00EE)
        }
        "JP" => match operands.len() {
            1 => word(0x1000 | context.address(&operands[0])?),
            2 => {
                if context.reg(&operands[0])? != 0 {
                    return fail(line, "indexed jump must use V0");
                }
                word(0xB000 | context.address(&operands[1])?)
            }
            _ => fail(line, "`JP` expects 1 or 2 operands"),
        },
        "CALL" => {
            arity(1)?;
            word(0x2000 | context.address(&operands[0])?)
        }
        "SE" | "SNE" => {
            arity(2)?;
            let x = u16::from(context.reg(&operands[0])?);
            let equal = statement.mnemonic == "SE";
            match &operands[1] {
                Operand::Reg(y) => {
                    let base = if equal { 0x5000 } else { 0x9000 };
                    word(base | (x << 8) | (u16::from(*y) << 4))
                }
                other => {
                    let base = if equal { 0x3000 } else { 0x4000 };
                    word(base | (x << 8) | u16::from(context.byte(other)?))
                }
            }
        }
        "ADD" => {
            arity(2)?;
            match (&operands[0], &operands[1]) {
                (Operand::Index, second) => {
                    let x = u16::from(context.reg(second)?);
                    word(0xF01E | (x << 8))
                }
                (first, Operand::Reg(y)) => {
                    let x = u16::from(context.reg(first)?);
                    word(0x8004 | (x << 8) | (u16::from(*y) << 4))
                }
                (first, second) => {
                    let x = u16::from(context.reg(first)?);
                    word(0x7000 | (x << 8) | u16::from(context.byte(second)?))
                }
            }
        }
        "OR" | "AND" | "XOR" | "SUB" | "SUBN" => {
            arity(2)?;
            let x = u16::from(context.reg(&operands[0])?);
            let y = u16::from(context.reg(&operands[1])?);
            let low = match statement.mnemonic.as_str() {
                "OR" => 0x1,
                "AND" => 0x2,
                "XOR" => 0x3,
                "SUB" => 0x5,
                _ => 0x7,
            };
            word(0x8000 | (x << 8) | (y << 4) | low)
        }
        "SHR" | "SHL" => {
            let low: u16 = if statement.mnemonic == "SHR" { 0x6 } else { 0xE };
            match operands.len() {
                1 => {
                    let x = u16::from(context.reg(&operands[0])?);
                    word(0x8000 | (x << 8) | (x << 4) | low)
                }
                2 => {
                    let x = u16::from(context.reg(&operands[0])?);
                    let y = u16::from(context.reg(&operands[1])?);
                    word(0x8000 | (x << 8) | (y << 4) | low)
                }
                _ => fail(line, "shift expects 1 or 2 operands"),
            }
        }
        "RND" => {
            arity(2)?;
            let x = u16::from(context.reg(&operands[0])?);
            word(0xC000 | (x << 8) | u16::from(context.byte(&operands[1])?))
        }
        "DRW" => {
            arity(3)?;
            let x = u16::from(context.reg(&operands[0])?);
            let y = u16::from(context.reg(&operands[1])?);
            let n = u16::from(context.nibble(&operands[2])?);
            word(0xD000 | (x << 8) | (y << 4) | n)
        }
        "SKP" | "SKNP" => {
            arity(1)?;
            let x = u16::from(context.reg(&operands[0])?);
            let low = if statement.mnemonic == "SKP" { 0x9E } else { 0xA1 };
            word(0xE000 | (x << 8) | low)
        }
        "LD" => {
            arity(2)?;
            match (&operands[0], &operands[1]) {
                (Operand::Index, second) => word(0xA000 | context.address(second)?),
                (Operand::Delay, second) => {
                    word(0xF015 | (u16::from(context.reg(second)?) << 8))
                }
                (Operand::Sound, second) => {
                    word(0xF018 | (u16::from(context.reg(second)?) << 8))
                }
                (Operand::Font, second) => word(0xF029 | (u16::from(context.reg(second)?) << 8)),
                (Operand::Bcd, second) => word(0xF033 | (u16::from(context.reg(second)?) << 8)),
                (Operand::Indirect, second) => {
                    word(0xF055 | (u16::from(context.reg(second)?) << 8))
                }
                (first, Operand::Delay) => word(0xF007 | (u16::from(context.reg(first)?) << 8)),
                (first, Operand::KeyWait) => word(0xF00A | (u16::from(context.reg(first)?) << 8)),
                (first, Operand::Indirect) => word(0xF065 | (u16::from(context.reg(first)?) << 8)),
                (first, Operand::Reg(y)) => {
                    let x = u16::from(context.reg(first)?);
                    word(0x8000 | (x << 8) | (u16::from(*y) << 4))
                }
                (first, second) => {
                    let x = u16::from(context.reg(first)?);
                    word(0x6000 | (x << 8) | u16::from(context.byte(second)?))
                }
            }
        }
        "DB" => {
            if operands.is_empty() {
                return fail(line, "`DB` expects at least one byte");
            }
            let mut bytes = Vec::with_capacity(operands.len());
            for operand in &operands {
                bytes.push(context.byte(operand)?);
            }
            Ok(bytes)
        }
        other => fail(line, format!("unknown mnemonic `{other}`")),
    }
}

pub fn assemble(source: &str) -> Result<Vec<u8>, Error> {
    let (statements, label_targets) = parse_statements(source)?;
    let mut offsets = Vec::with_capacity(statements.len() + 1);
    let mut cursor = PROGRAM_START;
    for statement in &statements {
        offsets.push(cursor);
        cursor = cursor.wrapping_add(statement_size(statement) as u16);
        if cursor > 0x0FFF {
            return fail(statement.line, "program overruns addressable memory");
        }
    }
    offsets.push(cursor);

    let mut labels = HashMap::with_capacity(label_targets.len());
    for (name, index) in label_targets {
        labels.insert(name, offsets[index]);
    }

    let mut rom = Vec::new();
    for statement in &statements {
        rom.extend_from_slice(&encode(statement, &labels)?);
    }
    Ok(rom)
}
