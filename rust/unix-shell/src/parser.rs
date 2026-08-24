use crate::lexer::{Token, WordPart};

#[derive(Debug, Clone)]
pub struct OutRedirect {
    pub fd: i32,
    pub append: bool,
    pub target: Vec<WordPart>,
}

#[derive(Debug, Clone, Default)]
pub struct SimpleCommand {
    pub words: Vec<Vec<WordPart>>,
    pub out_redirects: Vec<OutRedirect>,
    pub in_redirect: Option<Vec<WordPart>>,
}

#[derive(Debug, Clone)]
pub struct Pipeline {
    pub commands: Vec<SimpleCommand>,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Connector {
    And,
    Or,
}

#[derive(Debug, Clone)]
pub struct AndOrList {
    pub first: Pipeline,
    pub rest: Vec<(Connector, Pipeline)>,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Terminator {
    Seq,
    Background,
}

#[derive(Debug, Clone)]
pub struct ShellJob {
    pub list: AndOrList,
    pub terminator: Terminator,
}

pub type Script = Vec<ShellJob>;

struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    fn peek(&self) -> Option<&Token> {
        self.tokens.get(self.pos)
    }

    fn advance(&mut self) -> Option<Token> {
        let t = self.tokens.get(self.pos).cloned();
        self.pos += 1;
        t
    }

    fn parse_script(&mut self) -> Result<Script, String> {
        let mut jobs = Vec::new();
        loop {
            while matches!(self.peek(), Some(Token::Semi)) {
                self.pos += 1;
            }
            if self.peek().is_none() {
                break;
            }
            let list = self.parse_and_or_list()?;
            let terminator = match self.peek() {
                Some(Token::Amp) => {
                    self.pos += 1;
                    Terminator::Background
                }
                Some(Token::Semi) => {
                    self.pos += 1;
                    Terminator::Seq
                }
                _ => Terminator::Seq,
            };
            jobs.push(ShellJob { list, terminator });
        }
        Ok(jobs)
    }

    fn parse_and_or_list(&mut self) -> Result<AndOrList, String> {
        let first = self.parse_pipeline()?;
        let mut rest = Vec::new();
        loop {
            let connector = match self.peek() {
                Some(Token::And) => Connector::And,
                Some(Token::Or) => Connector::Or,
                _ => break,
            };
            self.pos += 1;
            let next = self.parse_pipeline()?;
            rest.push((connector, next));
        }
        Ok(AndOrList { first, rest })
    }

    fn parse_pipeline(&mut self) -> Result<Pipeline, String> {
        let mut commands = vec![self.parse_simple_command()?];
        while matches!(self.peek(), Some(Token::Pipe)) {
            self.pos += 1;
            commands.push(self.parse_simple_command()?);
        }
        Ok(Pipeline { commands })
    }

    fn parse_simple_command(&mut self) -> Result<SimpleCommand, String> {
        let mut cmd = SimpleCommand::default();
        loop {
            match self.peek() {
                Some(Token::Word(_)) => {
                    if let Some(Token::Word(w)) = self.advance() {
                        cmd.words.push(w);
                    }
                }
                Some(Token::Redirect { fd, append }) => {
                    let fd = *fd;
                    let append = *append;
                    self.pos += 1;
                    let target = self.expect_word("expected filename after redirect")?;
                    cmd.out_redirects.push(OutRedirect { fd, append, target });
                }
                Some(Token::RedirectIn) => {
                    self.pos += 1;
                    let target = self.expect_word("expected filename after '<'")?;
                    cmd.in_redirect = Some(target);
                }
                _ => break,
            }
        }
        if cmd.words.is_empty() && cmd.out_redirects.is_empty() && cmd.in_redirect.is_none() {
            return Err("syntax error: expected a command".to_string());
        }
        Ok(cmd)
    }

    fn expect_word(&mut self, err: &str) -> Result<Vec<WordPart>, String> {
        match self.advance() {
            Some(Token::Word(w)) => Ok(w),
            _ => Err(err.to_string()),
        }
    }
}

pub fn parse(tokens: Vec<Token>) -> Result<Script, String> {
    let mut p = Parser { tokens, pos: 0 };
    p.parse_script()
}
