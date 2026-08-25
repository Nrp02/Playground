#[derive(Debug, Clone)]
enum Ast {
    Char(char),
    Any,
    Class(Vec<(char, char)>, bool),
    Start,
    End,
    Concat(Vec<Ast>),
    Alternate(Vec<Ast>),
    Star(Box<Ast>),
    Plus(Box<Ast>),
    Question(Box<Ast>),
}

struct Parser {
    chars: Vec<char>,
    pos: usize,
}

impl Parser {
    fn new(pattern: &str) -> Self {
        Parser {
            chars: pattern.chars().collect(),
            pos: 0,
        }
    }

    fn peek(&self) -> Option<char> {
        self.chars.get(self.pos).copied()
    }

    fn bump(&mut self) -> Option<char> {
        let c = self.peek();
        if c.is_some() {
            self.pos += 1;
        }
        c
    }

    fn parse(&mut self) -> Result<Ast, String> {
        let ast = self.parse_alternation()?;
        if self.pos != self.chars.len() {
            return Err(format!("unexpected character at position {}", self.pos));
        }
        Ok(ast)
    }

    fn parse_alternation(&mut self) -> Result<Ast, String> {
        let mut branches = vec![self.parse_concat()?];
        while self.peek() == Some('|') {
            self.bump();
            branches.push(self.parse_concat()?);
        }
        if branches.len() == 1 {
            Ok(branches.pop().unwrap())
        } else {
            Ok(Ast::Alternate(branches))
        }
    }

    fn parse_concat(&mut self) -> Result<Ast, String> {
        let mut items = Vec::new();
        while let Some(c) = self.peek() {
            if c == '|' || c == ')' {
                break;
            }
            items.push(self.parse_repeat()?);
        }
        Ok(Ast::Concat(items))
    }

    fn parse_repeat(&mut self) -> Result<Ast, String> {
        let atom = self.parse_atom()?;
        match self.peek() {
            Some('*') => {
                self.bump();
                Ok(Ast::Star(Box::new(atom)))
            }
            Some('+') => {
                self.bump();
                Ok(Ast::Plus(Box::new(atom)))
            }
            Some('?') => {
                self.bump();
                Ok(Ast::Question(Box::new(atom)))
            }
            _ => Ok(atom),
        }
    }

    fn parse_atom(&mut self) -> Result<Ast, String> {
        match self.bump() {
            Some('(') => {
                let inner = self.parse_alternation()?;
                if self.bump() != Some(')') {
                    return Err("expected closing parenthesis".to_string());
                }
                Ok(inner)
            }
            Some('.') => Ok(Ast::Any),
            Some('^') => Ok(Ast::Start),
            Some('$') => Ok(Ast::End),
            Some('[') => self.parse_class(),
            Some('\\') => match self.bump() {
                Some(c) => Ok(Ast::Char(c)),
                None => Err("trailing backslash".to_string()),
            },
            Some(c) => Ok(Ast::Char(c)),
            None => Err("unexpected end of pattern".to_string()),
        }
    }

    fn parse_class(&mut self) -> Result<Ast, String> {
        let negated = if self.peek() == Some('^') {
            self.bump();
            true
        } else {
            false
        };
        let mut ranges = Vec::new();
        let mut first = true;
        loop {
            match self.peek() {
                None => return Err("unterminated character class".to_string()),
                Some(']') if !first => {
                    self.bump();
                    break;
                }
                _ => {
                    first = false;
                    let lo = self.bump().unwrap();
                    if self.peek() == Some('-') {
                        let save = self.pos;
                        self.bump();
                        match self.peek() {
                            Some(hi) if hi != ']' => {
                                self.bump();
                                ranges.push((lo, hi));
                                continue;
                            }
                            _ => {
                                self.pos = save;
                            }
                        }
                    }
                    ranges.push((lo, lo));
                }
            }
        }
        Ok(Ast::Class(ranges, negated))
    }
}

#[derive(Debug, Clone)]
enum Inst {
    Char(char, usize),
    Any(usize),
    Class(Vec<(char, char)>, bool, usize),
    Start(usize),
    End(usize),
    Split(usize, usize),
    Jmp(usize),
    Match,
}

struct Program {
    insts: Vec<Inst>,
    start: usize,
}

#[derive(Clone, Copy)]
enum Hole {
    Next(usize),
    Split1(usize),
    Split2(usize),
}

struct Frag {
    start: usize,
    holes: Vec<Hole>,
}

fn fill(insts: &mut [Inst], hole: Hole, target: usize) {
    match hole {
        Hole::Next(i) => match &mut insts[i] {
            Inst::Char(_, n) => *n = target,
            Inst::Any(n) => *n = target,
            Inst::Class(_, _, n) => *n = target,
            Inst::Start(n) => *n = target,
            Inst::End(n) => *n = target,
            Inst::Jmp(n) => *n = target,
            _ => unreachable!(),
        },
        Hole::Split1(i) => match &mut insts[i] {
            Inst::Split(a, _) => *a = target,
            _ => unreachable!(),
        },
        Hole::Split2(i) => match &mut insts[i] {
            Inst::Split(_, b) => *b = target,
            _ => unreachable!(),
        },
    }
}

fn compile_alternate(items: &[Ast], insts: &mut Vec<Inst>) -> Frag {
    if items.len() == 1 {
        return compile_ast(&items[0], insts);
    }
    let i = insts.len();
    insts.push(Inst::Split(usize::MAX, usize::MAX));
    let frag1 = compile_ast(&items[0], insts);
    fill(insts, Hole::Split1(i), frag1.start);
    let frag_rest = compile_alternate(&items[1..], insts);
    fill(insts, Hole::Split2(i), frag_rest.start);
    let mut holes = frag1.holes;
    holes.extend(frag_rest.holes);
    Frag { start: i, holes }
}

fn compile_ast(ast: &Ast, insts: &mut Vec<Inst>) -> Frag {
    match ast {
        Ast::Char(c) => {
            let i = insts.len();
            insts.push(Inst::Char(*c, usize::MAX));
            Frag {
                start: i,
                holes: vec![Hole::Next(i)],
            }
        }
        Ast::Any => {
            let i = insts.len();
            insts.push(Inst::Any(usize::MAX));
            Frag {
                start: i,
                holes: vec![Hole::Next(i)],
            }
        }
        Ast::Class(ranges, negated) => {
            let i = insts.len();
            insts.push(Inst::Class(ranges.clone(), *negated, usize::MAX));
            Frag {
                start: i,
                holes: vec![Hole::Next(i)],
            }
        }
        Ast::Start => {
            let i = insts.len();
            insts.push(Inst::Start(usize::MAX));
            Frag {
                start: i,
                holes: vec![Hole::Next(i)],
            }
        }
        Ast::End => {
            let i = insts.len();
            insts.push(Inst::End(usize::MAX));
            Frag {
                start: i,
                holes: vec![Hole::Next(i)],
            }
        }
        Ast::Concat(items) => {
            if items.is_empty() {
                let i = insts.len();
                insts.push(Inst::Jmp(usize::MAX));
                return Frag {
                    start: i,
                    holes: vec![Hole::Next(i)],
                };
            }
            let mut iter = items.iter();
            let mut current = compile_ast(iter.next().unwrap(), insts);
            for item in iter {
                let next_frag = compile_ast(item, insts);
                let holes = std::mem::take(&mut current.holes);
                for hole in holes {
                    fill(insts, hole, next_frag.start);
                }
                current.holes = next_frag.holes;
            }
            current
        }
        Ast::Alternate(items) => compile_alternate(items, insts),
        Ast::Star(inner) => {
            let i = insts.len();
            insts.push(Inst::Split(usize::MAX, usize::MAX));
            let frag = compile_ast(inner, insts);
            fill(insts, Hole::Split1(i), frag.start);
            for hole in frag.holes {
                fill(insts, hole, i);
            }
            Frag {
                start: i,
                holes: vec![Hole::Split2(i)],
            }
        }
        Ast::Plus(inner) => {
            let frag = compile_ast(inner, insts);
            let i = insts.len();
            insts.push(Inst::Split(usize::MAX, usize::MAX));
            for hole in frag.holes {
                fill(insts, hole, i);
            }
            fill(insts, Hole::Split1(i), frag.start);
            Frag {
                start: frag.start,
                holes: vec![Hole::Split2(i)],
            }
        }
        Ast::Question(inner) => {
            let i = insts.len();
            insts.push(Inst::Split(usize::MAX, usize::MAX));
            let frag = compile_ast(inner, insts);
            fill(insts, Hole::Split1(i), frag.start);
            let mut holes = frag.holes;
            holes.push(Hole::Split2(i));
            Frag { start: i, holes }
        }
    }
}

fn compile(ast: &Ast) -> Program {
    let mut insts = Vec::new();
    let frag = compile_ast(ast, &mut insts);
    let match_idx = insts.len();
    insts.push(Inst::Match);
    for hole in frag.holes {
        fill(&mut insts, hole, match_idx);
    }
    Program {
        insts,
        start: frag.start,
    }
}

fn class_matches(ranges: &[(char, char)], negated: bool, c: char) -> bool {
    let hit = ranges.iter().any(|&(lo, hi)| c >= lo && c <= hi);
    hit != negated
}

struct ThreadList {
    pcs: Vec<usize>,
    in_list: Vec<bool>,
}

impl ThreadList {
    fn new(n: usize) -> Self {
        ThreadList {
            pcs: Vec::new(),
            in_list: vec![false; n],
        }
    }

    fn clear(&mut self) {
        for flag in self.in_list.iter_mut() {
            *flag = false;
        }
        self.pcs.clear();
    }
}

fn add_thread(list: &mut ThreadList, pc: usize, prog: &Program, pos: usize, n: usize) {
    if list.in_list[pc] {
        return;
    }
    list.in_list[pc] = true;
    match &prog.insts[pc] {
        Inst::Split(a, b) => {
            let (a, b) = (*a, *b);
            add_thread(list, a, prog, pos, n);
            add_thread(list, b, prog, pos, n);
        }
        Inst::Jmp(t) => {
            let t = *t;
            add_thread(list, t, prog, pos, n);
        }
        Inst::Start(t) => {
            let t = *t;
            if pos == 0 {
                add_thread(list, t, prog, pos, n);
            }
        }
        Inst::End(t) => {
            let t = *t;
            if pos == n {
                add_thread(list, t, prog, pos, n);
            }
        }
        _ => list.pcs.push(pc),
    }
}

fn run(prog: &Program, chars: &[char], unanchored: bool) -> bool {
    let n = chars.len();
    let mut clist = ThreadList::new(prog.insts.len());
    let mut nlist = ThreadList::new(prog.insts.len());
    add_thread(&mut clist, prog.start, prog, 0, n);
    let mut pos = 0;
    loop {
        if unanchored && pos > 0 {
            add_thread(&mut clist, prog.start, prog, pos, n);
        }
        let mut matched = false;
        let c = if pos < n { Some(chars[pos]) } else { None };
        for idx in 0..clist.pcs.len() {
            let pc = clist.pcs[idx];
            match &prog.insts[pc] {
                Inst::Match => matched = true,
                Inst::Char(ch, next) => {
                    if let Some(cc) = c {
                        if cc == *ch {
                            add_thread(&mut nlist, *next, prog, pos + 1, n);
                        }
                    }
                }
                Inst::Any(next) => {
                    if c.is_some() {
                        add_thread(&mut nlist, *next, prog, pos + 1, n);
                    }
                }
                Inst::Class(ranges, negated, next) => {
                    if let Some(cc) = c {
                        if class_matches(ranges, *negated, cc) {
                            add_thread(&mut nlist, *next, prog, pos + 1, n);
                        }
                    }
                }
                _ => unreachable!(),
            }
        }
        if matched && (unanchored || pos == n) {
            return true;
        }
        if pos >= n {
            break;
        }
        clist.clear();
        std::mem::swap(&mut clist, &mut nlist);
        pos += 1;
    }
    false
}

pub struct Regex {
    prog: Program,
}

impl Regex {
    pub fn new(pattern: &str) -> Result<Regex, String> {
        let mut parser = Parser::new(pattern);
        let ast = parser.parse()?;
        Ok(Regex {
            prog: compile(&ast),
        })
    }

    pub fn is_match(&self, text: &str) -> bool {
        let chars: Vec<char> = text.chars().collect();
        run(&self.prog, &chars, false)
    }

    pub fn find(&self, text: &str) -> bool {
        let chars: Vec<char> = text.chars().collect();
        run(&self.prog, &chars, true)
    }
}

pub fn grep<'a>(pattern: &str, lines: &[&'a str]) -> Vec<(usize, &'a str)> {
    let re = Regex::new(pattern).expect("invalid pattern");
    lines
        .iter()
        .enumerate()
        .filter_map(|(i, &line)| if re.find(line) { Some((i + 1, line)) } else { None })
        .collect()
}
