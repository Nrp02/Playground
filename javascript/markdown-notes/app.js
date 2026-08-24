'use strict';

/**
 * Vanilla JS Markdown Notes SPA
 * - CRUD on notes (create / rename / edit / delete / duplicate)
 * - Textarea editor + live-updating rendered Markdown preview, side by side
 * - Multi-tag tagging with an "active filters" tag cloud (AND semantics)
 * - Full-text search across title + body
 * - State persisted to localStorage (debounced autosave) via storage.js
 * - Markdown -> HTML rendering via the hand-written parser in
 *   markdown-parser.js
 * - A few extra niceties: undo toast after delete, export/import as .md,
 *   word/character count, light/dark theme toggle, keyboard shortcuts
 *
 * Relies on two globals loaded before this file: `parseMarkdown` (from
 * markdown-parser.js) and `NotesStorage` (from storage.js).
 */

// ---------------------------------------------------------------------------
// Element references
// ---------------------------------------------------------------------------

const newNoteBtn = document.getElementById('newNoteBtn');
const importNoteBtn = document.getElementById('importNoteBtn');
const importFileInput = document.getElementById('importFileInput');
const searchInput = document.getElementById('searchInput');
const tagFilterEl = document.getElementById('tagFilter');
const sortSelect = document.getElementById('sortSelect');
const themeToggleBtn = document.getElementById('themeToggleBtn');
const noteListEl = document.getElementById('noteList');
const noteCountEl = document.getElementById('noteCount');

const emptyStateEl = document.getElementById('emptyState');
const emptyNewNoteBtn = document.getElementById('emptyNewNoteBtn');
const editorShellEl = document.getElementById('editorShell');

const titleInput = document.getElementById('titleInput');
const tagsInput = document.getElementById('tagsInput');
const bodyInput = document.getElementById('bodyInput');
const previewOutput = document.getElementById('previewOutput');
const duplicateBtn = document.getElementById('duplicateBtn');
const exportBtn = document.getElementById('exportBtn');
const deleteBtn = document.getElementById('deleteBtn');
const wordCountEl = document.getElementById('wordCount');
const savedIndicatorEl = document.getElementById('savedIndicator');

const toastContainer = document.getElementById('toastContainer');
const noteListItemTemplate = document.getElementById('noteListItemTemplate');

const helpBtn = document.getElementById('helpBtn');
const helpBackdrop = document.getElementById('helpBackdrop');
const helpCloseBtn = document.getElementById('helpCloseBtn');

const SAVE_DEBOUNCE_MS = 400;
const UNDO_WINDOW_MS = 6000;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

/** @type {{id: string, title: string, body: string, tags: string[], createdAt: number, updatedAt: number}[]} */
let notes = [];
let currentNoteId = null;
let searchTerm = '';
/** @type {Set<string>} tags currently active in the filter; a note must have ALL of them to show. */
let activeTags = new Set();
let sortMode = 'updated'; // 'updated' | 'created' | 'title'

let saveTimeoutId = null;
let pendingUndo = null; // { note, index, timeoutId }

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

function uid() {
  return `note-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}

function getCurrentNote() {
  return notes.find((n) => n.id === currentNoteId) || null;
}

function parseTagsInput(value) {
  return NotesStorage.normalizeTags(value.split(','));
}

function slugify(text) {
  const slug = text
    .toLowerCase()
    .trim()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '');
  return slug || 'untitled-note';
}

function formatRelativeDate(ts) {
  const diffSec = Math.floor((Date.now() - ts) / 1000);
  if (diffSec < 5) return 'Just now';
  if (diffSec < 60) return `${diffSec}s ago`;
  const diffMin = Math.floor(diffSec / 60);
  if (diffMin < 60) return `${diffMin}m ago`;
  const diffHour = Math.floor(diffMin / 60);
  if (diffHour < 24) return `${diffHour}h ago`;
  const diffDay = Math.floor(diffHour / 24);
  if (diffDay < 7) return `${diffDay}d ago`;
  return new Date(ts).toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' });
}

function countWords(text) {
  const trimmed = text.trim();
  return trimmed ? trimmed.split(/\s+/).length : 0;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

function persistNotes() {
  NotesStorage.saveNotes(notes);
}

function scheduleSave() {
  setSavedIndicator('Saving…');
  if (saveTimeoutId) clearTimeout(saveTimeoutId);
  saveTimeoutId = setTimeout(() => {
    saveTimeoutId = null;
    persistNotes();
    setSavedIndicator('Saved');
  }, SAVE_DEBOUNCE_MS);
}

/** Flushes any pending debounced save immediately (e.g. before navigating away). */
function flushSave() {
  if (saveTimeoutId) {
    clearTimeout(saveTimeoutId);
    saveTimeoutId = null;
    persistNotes();
  }
}

function setSavedIndicator(text) {
  savedIndicatorEl.textContent = text;
}

// ---------------------------------------------------------------------------
// Note CRUD
// ---------------------------------------------------------------------------

function createNote(overrides) {
  const now = Date.now();
  const note = Object.assign(
    {
      id: uid(),
      title: 'Untitled note',
      body: '',
      tags: [],
      createdAt: now,
      updatedAt: now,
    },
    overrides || {}
  );
  notes.unshift(note);
  currentNoteId = note.id;
  persistNotes();
  render();
  return note;
}

function selectNote(id) {
  if (currentNoteId === id) return;
  currentNoteId = id;
  render();
}

function deleteNote(id) {
  const index = notes.findIndex((n) => n.id === id);
  if (index === -1) return;

  const note = notes[index];
  const confirmed = window.confirm(`Delete "${note.title || 'Untitled note'}"? You can undo this for a few seconds.`);
  if (!confirmed) return;

  notes.splice(index, 1);

  if (currentNoteId === id) {
    const next = getFilteredSortedNotes()[0];
    currentNoteId = next ? next.id : (notes[0] ? notes[0].id : null);
  }

  persistNotes();
  render();
  showUndoToast(note, index);
}

function restoreNote(note, index) {
  const safeIndex = Math.min(index, notes.length);
  notes.splice(safeIndex, 0, note);
  currentNoteId = note.id;
  persistNotes();
  render();
}

function duplicateNote(id) {
  const original = notes.find((n) => n.id === id);
  if (!original) return;
  const now = Date.now();
  const copy = {
    id: uid(),
    title: `${original.title} (copy)`,
    body: original.body,
    tags: original.tags.slice(),
    createdAt: now,
    updatedAt: now,
  };
  const idx = notes.findIndex((n) => n.id === id);
  notes.splice(idx + 1, 0, copy);
  currentNoteId = copy.id;
  persistNotes();
  render();
}

function exportNote(id) {
  const note = notes.find((n) => n.id === id);
  if (!note) return;
  const filename = `${slugify(note.title)}.md`;
  const blob = new Blob([note.body], { type: 'text/markdown;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  URL.revokeObjectURL(url);
}

function importNoteFromFile(file) {
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => {
    const rawName = file.name.replace(/\.(md|markdown|txt)$/i, '');
    createNote({ title: rawName || 'Imported note', body: String(reader.result || '') });
  };
  reader.onerror = () => {
    window.alert('Could not read that file.');
  };
  reader.readAsText(file);
}

// ---------------------------------------------------------------------------
// Undo toast (mirrors the pattern used by the kanban-board project, adapted
// for single-note delete/restore instead of task delete/restore)
// ---------------------------------------------------------------------------

function showUndoToast(note, index) {
  if (pendingUndo) {
    clearTimeout(pendingUndo.timeoutId);
    toastContainer.innerHTML = '';
  }

  const toast = document.createElement('div');
  toast.className = 'toast';

  const label = note.title.length > 40 ? `${note.title.slice(0, 40)}…` : note.title;
  const message = document.createElement('span');
  message.textContent = `Deleted "${label || 'Untitled note'}"`;
  toast.appendChild(message);

  const undoBtn = document.createElement('button');
  undoBtn.className = 'toast-undo-btn';
  undoBtn.textContent = 'Undo';
  undoBtn.addEventListener('click', () => {
    clearTimeout(pendingUndo.timeoutId);
    restoreNote(note, index);
    dismissToast();
  });
  toast.appendChild(undoBtn);

  toastContainer.innerHTML = '';
  toastContainer.appendChild(toast);

  const timeoutId = setTimeout(dismissToast, UNDO_WINDOW_MS);
  pendingUndo = { note, index, timeoutId };
}

function dismissToast() {
  toastContainer.innerHTML = '';
  pendingUndo = null;
}

// ---------------------------------------------------------------------------
// Filtering / sorting
// ---------------------------------------------------------------------------

function matchesSearch(note) {
  if (!searchTerm) return true;
  const haystack = `${note.title}\n${note.body}`.toLowerCase();
  return haystack.includes(searchTerm);
}

function matchesActiveTags(note) {
  if (activeTags.size === 0) return true;
  return Array.from(activeTags).every((tag) => note.tags.includes(tag));
}

function getFilteredSortedNotes() {
  const filtered = notes.filter((n) => matchesSearch(n) && matchesActiveTags(n));
  return filtered.sort((a, b) => {
    if (sortMode === 'title') return a.title.localeCompare(b.title, undefined, { sensitivity: 'base' });
    if (sortMode === 'created') return b.createdAt - a.createdAt;
    return b.updatedAt - a.updatedAt;
  });
}

function computeAllTags() {
  const counts = new Map();
  notes.forEach((note) => {
    note.tags.forEach((tag) => counts.set(tag, (counts.get(tag) || 0) + 1));
  });
  return Array.from(counts.entries()).sort((a, b) => a[0].localeCompare(b[0], undefined, { sensitivity: 'base' }));
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

function render() {
  renderTagFilter();
  renderSidebarList();
  renderEditorFull();
}

function renderTagFilter() {
  tagFilterEl.innerHTML = '';
  const allTags = computeAllTags();

  if (allTags.length === 0) {
    tagFilterEl.hidden = true;
    return;
  }
  tagFilterEl.hidden = false;

  allTags.forEach(([tag, count]) => {
    const pill = document.createElement('button');
    pill.type = 'button';
    pill.className = `tag-pill${activeTags.has(tag) ? ' active' : ''}`;
    pill.textContent = `${tag} (${count})`;
    pill.title = activeTags.has(tag) ? `Remove "${tag}" from filter` : `Filter by "${tag}"`;
    pill.addEventListener('click', () => {
      if (activeTags.has(tag)) {
        activeTags.delete(tag);
      } else {
        activeTags.add(tag);
      }
      renderTagFilter();
      renderSidebarList();
    });
    tagFilterEl.appendChild(pill);
  });

  if (activeTags.size > 0) {
    const clearBtn = document.createElement('button');
    clearBtn.type = 'button';
    clearBtn.className = 'tag-pill tag-pill-clear';
    clearBtn.textContent = 'Clear filter ×';
    clearBtn.addEventListener('click', () => {
      activeTags.clear();
      renderTagFilter();
      renderSidebarList();
    });
    tagFilterEl.appendChild(clearBtn);
  }
}

function renderSidebarList() {
  const list = getFilteredSortedNotes();
  noteListEl.innerHTML = '';

  if (list.length === 0) {
    const hint = document.createElement('li');
    hint.className = 'empty-hint';
    hint.textContent = notes.length === 0 ? 'No notes yet — create one to get started.' : 'No notes match your filters.';
    noteListEl.appendChild(hint);
  } else {
    list.forEach((note) => {
      const fragment = noteListItemTemplate.content.cloneNode(true);
      const li = fragment.querySelector('.note-item');
      li.dataset.id = note.id;
      if (note.id === currentNoteId) li.classList.add('active');

      fragment.querySelector('.note-item-title').textContent = note.title || 'Untitled note';
      fragment.querySelector('.note-item-date').textContent = formatRelativeDate(note.updatedAt);
      const words = countWords(note.body);
      fragment.querySelector('.note-item-words').textContent = words ? `${words}w` : 'empty';

      const tagsEl = fragment.querySelector('.note-item-tags');
      note.tags.forEach((tag) => {
        const span = document.createElement('span');
        span.className = 'note-item-tag';
        span.textContent = tag;
        tagsEl.appendChild(span);
      });

      li.addEventListener('click', () => selectNote(note.id));
      li.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' || e.key === ' ') {
          e.preventDefault();
          selectNote(note.id);
        }
      });

      noteListEl.appendChild(fragment);
    });
  }

  noteCountEl.textContent = `${list.length} of ${notes.length} note${notes.length === 1 ? '' : 's'}`;
}

function renderEditorFull() {
  const note = getCurrentNote();

  if (!note) {
    emptyStateEl.hidden = false;
    editorShellEl.hidden = true;
    return;
  }

  emptyStateEl.hidden = true;
  editorShellEl.hidden = false;

  titleInput.value = note.title;
  tagsInput.value = note.tags.join(', ');
  bodyInput.value = note.body;

  updatePreview();
  updateWordCount();
  setSavedIndicator('Saved');
}

function updatePreview() {
  const note = getCurrentNote();
  previewOutput.innerHTML = note ? parseMarkdown(note.body) : '';
}

function updateWordCount() {
  const note = getCurrentNote();
  if (!note) {
    wordCountEl.textContent = '';
    return;
  }
  const words = countWords(note.body);
  const chars = note.body.length;
  wordCountEl.textContent = `${words} word${words === 1 ? '' : 's'} · ${chars} character${chars === 1 ? '' : 's'}`;
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

function applyTheme(theme) {
  document.documentElement.setAttribute('data-theme', theme);
  themeToggleBtn.innerHTML = theme === 'dark' ? '&#9788;' : '&#9789;';
  themeToggleBtn.title = theme === 'dark' ? 'Switch to light theme' : 'Switch to dark theme';
}

function toggleTheme() {
  const current = document.documentElement.getAttribute('data-theme') === 'light' ? 'light' : 'dark';
  const next = current === 'dark' ? 'light' : 'dark';
  applyTheme(next);
  NotesStorage.saveTheme(next);
}

// ---------------------------------------------------------------------------
// Help modal (Markdown syntax reference)
// ---------------------------------------------------------------------------

function isHelpOpen() {
  return !helpBackdrop.hidden;
}

function openHelp() {
  helpBackdrop.hidden = false;
}

function closeHelp() {
  helpBackdrop.hidden = true;
}

function wireHelpModal() {
  helpBtn.addEventListener('click', openHelp);
  helpCloseBtn.addEventListener('click', closeHelp);
  helpBackdrop.addEventListener('click', (e) => {
    if (e.target === helpBackdrop) closeHelp(); // click on the dimmed backdrop, not the panel itself
  });
}

// ---------------------------------------------------------------------------
// Event wiring
// ---------------------------------------------------------------------------

function wireEvents() {
  newNoteBtn.addEventListener('click', () => {
    createNote();
    titleInput.focus();
    titleInput.select();
  });

  emptyNewNoteBtn.addEventListener('click', () => {
    createNote();
    titleInput.focus();
    titleInput.select();
  });

  importNoteBtn.addEventListener('click', () => importFileInput.click());
  importFileInput.addEventListener('change', () => {
    const file = importFileInput.files && importFileInput.files[0];
    importNoteFromFile(file);
    importFileInput.value = '';
  });

  searchInput.addEventListener('input', () => {
    searchTerm = searchInput.value.trim().toLowerCase();
    renderSidebarList();
  });

  sortSelect.addEventListener('change', () => {
    sortMode = sortSelect.value;
    renderSidebarList();
  });

  themeToggleBtn.addEventListener('click', toggleTheme);

  // Title: mutate state directly and do targeted re-renders, rather than a
  // full renderEditorFull(), so the input keeps focus/cursor while typing.
  titleInput.addEventListener('input', () => {
    const note = getCurrentNote();
    if (!note) return;
    note.title = titleInput.value;
    note.updatedAt = Date.now();
    scheduleSave();
    renderSidebarList();
  });

  tagsInput.addEventListener('input', () => {
    const note = getCurrentNote();
    if (!note) return;
    note.tags = parseTagsInput(tagsInput.value);
    note.updatedAt = Date.now();
    scheduleSave();
    renderTagFilter();
    renderSidebarList();
  });

  bodyInput.addEventListener('input', () => {
    const note = getCurrentNote();
    if (!note) return;
    note.body = bodyInput.value;
    note.updatedAt = Date.now();
    scheduleSave();
    updatePreview();
    updateWordCount();
    renderSidebarList();
  });

  duplicateBtn.addEventListener('click', () => {
    if (currentNoteId) duplicateNote(currentNoteId);
  });

  exportBtn.addEventListener('click', () => {
    if (currentNoteId) exportNote(currentNoteId);
  });

  deleteBtn.addEventListener('click', () => {
    if (currentNoteId) deleteNote(currentNoteId);
  });

  window.addEventListener('beforeunload', flushSave);

  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && isHelpOpen()) {
      closeHelp();
      return;
    }

    const activeTag = document.activeElement && document.activeElement.tagName;
    const isTyping = activeTag === 'INPUT' || activeTag === 'TEXTAREA';

    if (e.key === '/' && !isTyping) {
      e.preventDefault();
      searchInput.focus();
      return;
    }

    if (e.key === 'Escape' && document.activeElement === searchInput) {
      searchInput.value = '';
      searchTerm = '';
      searchInput.blur();
      renderSidebarList();
    }
  });
}

// ---------------------------------------------------------------------------
// Welcome note (first-run content) + init
// ---------------------------------------------------------------------------

const WELCOME_BODY = [
  '# Welcome to Markdown Notes',
  '',
  'This app is a small, dependency-free notes SPA with a **hand-written Markdown parser** underneath — no libraries, just plain JavaScript.',
  '',
  'Type in this pane and watch the *preview* update live. Here is a quick tour of what is supported:',
  '',
  '## Text styling',
  '',
  'You can write **bold**, *italic*, ***bold italic***, and `inline code`. Unclosed markers like **this are left alone instead of breaking.',
  '',
  '## Lists',
  '',
  '- Unordered lists work',
  '  - and so do nested ones',
  '  - as deep as you like',
  '- Ordered lists too:',
  '  1. First step',
  '  2. Second step',
  '',
  '## Quotes and code',
  '',
  '> Blockquotes are supported, including\n> multiple lines.',
  '',
  '```js',
  'function greet(name) {',
  '  return `Hello, ${name}!`;',
  '}',
  '```',
  '',
  '## Links and structure',
  '',
  'Here is a [link to Anthropic](https://www.anthropic.com), a horizontal rule below, and this whole document is safe from injected HTML — try typing a literal `<script>` tag and it will render as text, not run.',
  '',
  '---',
  '',
  '**Tips:**',
  '- Use the tag box above the editor to organize notes; click tags in the sidebar to filter.',
  '- Press `/` to jump to search from anywhere.',
  '- Your notes save automatically to this browser\'s local storage.',
].join('\n');

function createWelcomeNote() {
  const now = Date.now();
  return {
    id: uid(),
    title: 'Welcome to Markdown Notes',
    body: WELCOME_BODY,
    tags: ['welcome', 'guide'],
    createdAt: now,
    updatedAt: now,
  };
}

function init() {
  notes = NotesStorage.loadNotes();

  if (notes.length === 0) {
    notes = [createWelcomeNote()];
    persistNotes();
  }

  const mostRecentlyUpdated = notes.slice().sort((a, b) => b.updatedAt - a.updatedAt)[0];
  currentNoteId = mostRecentlyUpdated ? mostRecentlyUpdated.id : null;

  applyTheme(NotesStorage.loadTheme());
  wireEvents();
  wireHelpModal();
  render();
}

init();
