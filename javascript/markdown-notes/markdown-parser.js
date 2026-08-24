'use strict';

/**
 * markdown-parser.js — a small Markdown-to-HTML parser written from scratch.
 * No dependencies, no external libraries. Works under plain Node.js
 * (CommonJS) and directly in the browser (as a global).
 *
 * Public API:
 *   parseMarkdown(sourceText) -> htmlString
 *
 * Supported syntax:
 *   - ATX headings:        # H1 ... ###### H6
 *   - Emphasis:             **bold**, __bold__, *italic*, _italic_,
 *                            ***bold italic***, ___bold italic___
 *   - Inline code:          single-backtick code spans
 *   - Fenced code blocks:   triple-backtick or triple-tilde fences, language
 *                           preserved as a "language-xxx" class
 *   - Lists:                unordered (-, *, +) and ordered (1. or 1)),
 *                           with arbitrary nesting via indentation
 *   - Blockquotes:          "> quoted text", with nesting ("> >") and
 *                           multi-paragraph quotes
 *   - Links:                [text](url "optional title")
 *   - Images:                ![alt](url "optional title")
 *   - Horizontal rules:      ---, ***, ___ (3+ repeats, spaces allowed)
 *   - Paragraphs, including hard line breaks (trailing double-space or
 *     backslash)
 *
 * All raw text content is HTML-escaped before any markdown-generated tags
 * are inserted, so literal HTML in note text (e.g. "<script>") can never be
 * interpreted as markup.
 *
 * This is intentionally a pragmatic subset of CommonMark, not a spec-
 * complete implementation — things like reference-style links, tables,
 * setext headings, and CommonMark's exact tight/loose list and emphasis
 * flanking rules are out of scope.
 */

// ---------------------------------------------------------------------------
// HTML escaping
// ---------------------------------------------------------------------------

const HTML_ESCAPE_RE = /[&<>"']/g;
const HTML_ESCAPE_MAP = {
  '&': '&amp;',
  '<': '&lt;',
  '>': '&gt;',
  '"': '&quot;',
  "'": '&#39;',
};

/** Escapes the five HTML-significant characters in `str`. */
function escapeHtml(str) {
  return String(str).replace(HTML_ESCAPE_RE, (ch) => HTML_ESCAPE_MAP[ch]);
}

// ---------------------------------------------------------------------------
// Inline parsing (emphasis, code spans, links, images, line breaks)
// ---------------------------------------------------------------------------
//
// Strategy: extract "structural" inline elements (code spans, images, links)
// from the RAW text first, since they rely on literal characters — a title
// in `[text](url "title")` needs real quote characters, which would
// disappear if we escaped the string first. Each extracted element is
// rendered to safe HTML immediately (escaping its own pieces) and swapped
// for a placeholder token so later passes (bold/italic) can't reach inside
// it. Once structural extraction is done, whatever plain text remains is
// HTML-escaped in one shot, hard/soft line breaks are applied, then
// emphasis is layered on from widest delimiter to narrowest, and finally
// the placeholder tokens are swapped back for their real HTML.
//
// The placeholder is built around a control character (produced via
// fromCharCode rather than a literal escape, so this source file stays
// plain ASCII) that ordinary note text will essentially never contain —
// unlike, say, a plain space-digit-space marker, which really could show
// up in normal prose ("item 2 in the list") and get corrupted.
const STASH_MARK = String.fromCharCode(0);
const STASH_TOKEN_RE = new RegExp(STASH_MARK + '(\\d+)' + STASH_MARK, 'g');

const CODE_SPAN_RE = /`([^`\n]+)`/g;
const IMAGE_RE = /!\[([^\]]*)\]\(\s*([^()\s]+)(?:\s+"([^"]*)")?\s*\)/g;
const LINK_RE = /\[([^\]]*)\]\(\s*([^()\s]+)(?:\s+"([^"]*)")?\s*\)/g;

const HARD_BREAK_RE = /(?: {2,}|\\)\n/g;
const SOFT_BREAK_RE = /\n/g;

const TRIPLE_STAR_RE = /\*\*\*([^\s*](?:[\s\S]*?[^\s*])?)\*\*\*/g;
const TRIPLE_US_RE = /(?<!\w)___([^\s_](?:[\s\S]*?[^\s_])?)___(?!\w)/g;
const DOUBLE_STAR_RE = /\*\*([^\s*](?:[\s\S]*?[^\s*])?)\*\*/g;
const DOUBLE_US_RE = /(?<!\w)__([^\s_](?:[\s\S]*?[^\s_])?)__(?!\w)/g;
const SINGLE_STAR_RE = /\*([^\s*](?:[\s\S]*?[^\s*])?)\*/g;
const SINGLE_US_RE = /(?<!\w)_([^\s_](?:[\s\S]*?[^\s_])?)_(?!\w)/g;

/**
 * Parses inline markdown (emphasis, code, links, images, line breaks)
 * within a single chunk of text and returns safe HTML. Used for paragraph
 * text, heading text, and list item text.
 */
function parseInline(raw) {
  if (raw == null || raw === '') return '';
  let text = String(raw);

  const stash = [];
  function stashHtml(html) {
    const token = STASH_MARK + stash.length + STASH_MARK;
    stash.push(html);
    return token;
  }

  // 1. Inline code spans — literal content, no nested markdown.
  text = text.replace(CODE_SPAN_RE, (_m, code) => stashHtml(`<code>${escapeHtml(code)}</code>`));

  // 2. Images — must run before links since the syntax only differs by "!".
  text = text.replace(IMAGE_RE, (_m, alt, url, title) => {
    const titleAttr = title ? ` title="${escapeHtml(title)}"` : '';
    return stashHtml(`<img src="${escapeHtml(url)}" alt="${escapeHtml(alt)}"${titleAttr}>`);
  });

  // 3. Links.
  text = text.replace(LINK_RE, (_m, label, url, title) => {
    const titleAttr = title ? ` title="${escapeHtml(title)}"` : '';
    return stashHtml(`<a href="${escapeHtml(url)}"${titleAttr}>${escapeHtml(label)}</a>`);
  });

  // 4. Everything else left in `text` is plain prose (plus our placeholder
  // tokens, which contain no HTML-significant characters) — escape once.
  text = escapeHtml(text);

  // 5. Line breaks: two-or-more trailing spaces, or a trailing backslash,
  // become a hard <br>; any other newline (soft wrap) becomes a space.
  text = text.replace(HARD_BREAK_RE, '<br>\n').replace(SOFT_BREAK_RE, ' ');

  // 6. Emphasis, widest delimiter first so "***x***" and "**bold *em* bold**"
  // resolve correctly instead of leaving stray asterisks behind.
  text = text.replace(TRIPLE_STAR_RE, '<strong><em>$1</em></strong>');
  text = text.replace(TRIPLE_US_RE, '<strong><em>$1</em></strong>');
  text = text.replace(DOUBLE_STAR_RE, '<strong>$1</strong>');
  text = text.replace(DOUBLE_US_RE, '<strong>$1</strong>');
  text = text.replace(SINGLE_STAR_RE, '<em>$1</em>');
  text = text.replace(SINGLE_US_RE, '<em>$1</em>');

  // 7. Swap placeholder tokens back for their final HTML.
  if (stash.length) {
    text = text.replace(STASH_TOKEN_RE, (_m, idx) => stash[Number(idx)]);
  }

  return text;
}

// ---------------------------------------------------------------------------
// Block-level helpers
// ---------------------------------------------------------------------------

function isBlank(line) {
  return line.trim() === '';
}

function leadingSpaces(line) {
  const m = /^ */.exec(line);
  return m[0].length;
}

/** Strips up to `n` leading spaces from `line` (never more than it has). */
function dedent(line, n) {
  return line.replace(new RegExp(`^ {0,${n}}`), '');
}

const FENCE_START_RE = /^ {0,3}(`{3,}|~{3,})/;

function isFenceStart(line) {
  return FENCE_START_RE.test(line);
}

const HEADING_RE = /^ {0,3}(#{1,6})(?:\s+(.*))?$/;

/** Returns {level, content} for an ATX heading line, or null. */
function matchHeading(line) {
  const m = HEADING_RE.exec(line);
  if (!m) return null;
  let content = (m[2] || '').trim();
  content = content.replace(/\s+#+$/, '').trim();
  return { level: m[1].length, content };
}

/** True if `line` is a thematic break (---, ***, ___, with optional spaces). */
function isHr(line) {
  const trimmed = line.trim();
  if (!trimmed) return false;
  const compact = trimmed.replace(/[ \t]/g, '');
  if (compact.length < 3) return false;
  if (!/^[-*_]+$/.test(compact)) return false;
  return compact[0].repeat(compact.length) === compact;
}

const BLOCKQUOTE_START_RE = /^ {0,3}>/;

function isBlockquoteStart(line) {
  return BLOCKQUOTE_START_RE.test(line);
}

const UL_ITEM_RE = /^( *)([-*+])( +)(.*)$/;
const OL_ITEM_RE = /^( *)(\d{1,9})([.)])( +)(.*)$/;

/**
 * Returns a descriptor for a list-item line, or null:
 *   { ordered, indent, bullet|delim, start?, contentCol, content }
 * `contentCol` is the column where the item's own text begins, and is used
 * to decide how much leading whitespace to strip from that item's
 * continuation lines before recursively parsing them.
 */
function matchListItem(line) {
  const ul = UL_ITEM_RE.exec(line);
  if (ul) {
    const [, indent, bullet, gap, content] = ul;
    return {
      ordered: false,
      indent: indent.length,
      bullet,
      contentCol: indent.length + bullet.length + gap.length,
      content,
    };
  }
  const ol = OL_ITEM_RE.exec(line);
  if (ol) {
    const [, indent, num, delim, gap, content] = ol;
    return {
      ordered: true,
      indent: indent.length,
      start: parseInt(num, 10),
      delim,
      contentCol: indent.length + num.length + delim.length + gap.length,
      content,
    };
  }
  return null;
}

function isBlockStart(line) {
  return (
    isFenceStart(line) ||
    matchHeading(line) !== null ||
    isHr(line) ||
    isBlockquoteStart(line) ||
    matchListItem(line) !== null
  );
}

// ---------------------------------------------------------------------------
// Block consumers — each takes (lines, i), consumes one or more lines
// starting at i, and returns { html or content, next }.
// ---------------------------------------------------------------------------

function consumeFencedCode(lines, i) {
  const open = FENCE_START_RE.exec(lines[i]);
  const fenceChar = open[1][0];
  const fenceLen = open[1].length;
  const infoString = lines[i].slice(lines[i].indexOf(open[1]) + fenceLen).trim();
  const lang = infoString.split(/\s+/)[0] || '';

  const closeRe = new RegExp(`^ {0,3}\\${fenceChar}{${fenceLen},}\\s*$`);
  const codeLines = [];
  let j = i + 1;
  while (j < lines.length && !closeRe.test(lines[j])) {
    codeLines.push(lines[j]);
    j++;
  }
  const next = j < lines.length ? j + 1 : j; // lenient: unterminated fence runs to EOF

  const classAttr = lang ? ` class="language-${escapeHtml(lang)}"` : '';
  const html = `<pre><code${classAttr}>${escapeHtml(codeLines.join('\n'))}</code></pre>`;
  return { html, next };
}

function consumeBlockquote(lines, i) {
  const raw = [];
  while (i < lines.length) {
    const line = lines[i];
    if (BLOCKQUOTE_START_RE.test(line)) {
      raw.push(line.replace(/^ {0,3}> ?/, ''));
      i++;
    } else if (isBlank(line)) {
      let j = i;
      while (j < lines.length && isBlank(lines[j])) j++;
      if (j < lines.length && BLOCKQUOTE_START_RE.test(lines[j])) {
        for (let b = i; b < j; b++) raw.push('');
        i = j;
      } else {
        break;
      }
    } else {
      break;
    }
  }
  return { content: raw.join('\n'), next: i };
}

/** Unwraps `<p>...</p>` to just its inner text for "tight" single-paragraph list items. */
function unwrapSingleParagraph(html) {
  const m = /^<p>([\s\S]*)<\/p>$/.exec(html.trim());
  return m ? m[1] : html;
}

function consumeList(lines, i) {
  const first = matchListItem(lines[i]);
  const ordered = first.ordered;
  const baseIndent = first.indent;
  const marker = ordered ? first.delim : first.bullet;
  const startNum = ordered ? first.start : null;

  function sameListType(m) {
    if (!m || m.ordered !== ordered || m.indent !== baseIndent) return false;
    return (ordered ? m.delim : m.bullet) === marker;
  }

  const items = [];

  while (i < lines.length) {
    if (isBlank(lines[i])) {
      let j = i;
      while (j < lines.length && isBlank(lines[j])) j++;
      const continues =
        j < lines.length && (sameListType(matchListItem(lines[j])) || leadingSpaces(lines[j]) > baseIndent);
      if (continues) {
        i = j;
        continue;
      }
      break;
    }

    const m = matchListItem(lines[i]);
    if (!sameListType(m)) break;

    const itemLines = [m.content];
    i++;

    while (i < lines.length) {
      if (isBlank(lines[i])) {
        let k = i;
        while (k < lines.length && isBlank(lines[k])) k++;
        if (k < lines.length && leadingSpaces(lines[k]) > baseIndent) {
          for (let b = i; b < k; b++) itemLines.push('');
          i = k;
          continue;
        }
        break;
      }
      if (leadingSpaces(lines[i]) > baseIndent) {
        itemLines.push(dedent(lines[i], m.contentCol));
        i++;
      } else {
        break;
      }
    }

    items.push(parseBlocks(itemLines.join('\n')));
  }

  const tag = ordered ? 'ol' : 'ul';
  const openTag = ordered && startNum !== 1 ? `<ol start="${startNum}">` : `<${tag}>`;
  const itemsHtml = items.map((inner) => `<li>${unwrapSingleParagraph(inner)}</li>`).join('\n');
  return { html: `${openTag}\n${itemsHtml}\n</${tag}>`, next: i };
}

function consumeParagraph(lines, i) {
  const raw = [];
  while (i < lines.length && !isBlank(lines[i]) && !isBlockStart(lines[i])) {
    raw.push(lines[i]);
    i++;
  }
  return { html: parseInline(raw.join('\n')), next: i };
}

// ---------------------------------------------------------------------------
// Block dispatcher
// ---------------------------------------------------------------------------

/**
 * Parses a chunk of markdown text into a sequence of block-level HTML
 * elements, joined by newlines. Recurses into itself for blockquote and
 * list-item contents, so nesting "just works" for any combination of
 * blockquotes, lists, and paragraphs.
 */
function parseBlocks(text) {
  if (!text) return '';
  const lines = text.split('\n');
  const out = [];
  let i = 0;

  while (i < lines.length) {
    if (isBlank(lines[i])) {
      i++;
      continue;
    }

    if (isFenceStart(lines[i])) {
      const res = consumeFencedCode(lines, i);
      out.push(res.html);
      i = res.next;
      continue;
    }

    const heading = matchHeading(lines[i]);
    if (heading) {
      out.push(`<h${heading.level}>${parseInline(heading.content)}</h${heading.level}>`);
      i++;
      continue;
    }

    if (isHr(lines[i])) {
      out.push('<hr>');
      i++;
      continue;
    }

    if (isBlockquoteStart(lines[i])) {
      const res = consumeBlockquote(lines, i);
      out.push(`<blockquote>\n${parseBlocks(res.content)}\n</blockquote>`);
      i = res.next;
      continue;
    }

    if (matchListItem(lines[i])) {
      const res = consumeList(lines, i);
      out.push(res.html);
      i = res.next;
      continue;
    }

    const para = consumeParagraph(lines, i);
    out.push(`<p>${para.html}</p>`);
    i = para.next;
  }

  return out.join('\n');
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * Converts a markdown source string to an HTML string.
 * @param {string} sourceText
 * @returns {string}
 */
function parseMarkdown(sourceText) {
  if (sourceText == null) return '';
  const normalized = String(sourceText).replace(/\r\n?/g, '\n');
  return parseBlocks(normalized);
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { parseMarkdown, escapeHtml };
}
if (typeof window !== 'undefined') {
  window.parseMarkdown = parseMarkdown;
}
