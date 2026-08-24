'use strict';

/**
 * storage.js — localStorage persistence helper for Markdown Notes.
 *
 * Keeps all knowledge of the localStorage key and the on-disk note shape in
 * one small place, and defensively validates/normalizes whatever comes back
 * out of storage so a corrupted or hand-edited value can't crash the app —
 * it just falls back to sane defaults, the same approach the kanban-board
 * and state-lib-demo projects in this repo use for their own persistence.
 *
 * Each note is stored as:
 *   { id, title, body, tags, createdAt, updatedAt }
 */

const NOTES_STORAGE_KEY = 'markdown-notes-app-notes-v1';
const THEME_STORAGE_KEY = 'markdown-notes-app-theme-v1';

function isStringArray(value) {
  return Array.isArray(value) && value.every((item) => typeof item === 'string');
}

/** Cleans up a tag list: trims, drops empties, removes duplicates. */
function normalizeTags(tags) {
  if (!isStringArray(tags)) return [];
  const seen = new Set();
  const cleaned = [];
  tags.forEach((tag) => {
    const trimmed = tag.trim();
    if (trimmed && !seen.has(trimmed.toLowerCase())) {
      seen.add(trimmed.toLowerCase());
      cleaned.push(trimmed);
    }
  });
  return cleaned;
}

/**
 * Validates and normalizes one raw note object from storage.
 * Returns null if the entry is unusable (missing id/title/body).
 */
function normalizeNote(raw) {
  if (!raw || typeof raw !== 'object') return null;
  if (typeof raw.id !== 'string' || !raw.id) return null;
  if (typeof raw.title !== 'string') return null;
  if (typeof raw.body !== 'string') return null;

  const now = Date.now();
  return {
    id: raw.id,
    title: raw.title,
    body: raw.body,
    tags: normalizeTags(raw.tags),
    createdAt: typeof raw.createdAt === 'number' && Number.isFinite(raw.createdAt) ? raw.createdAt : now,
    updatedAt: typeof raw.updatedAt === 'number' && Number.isFinite(raw.updatedAt) ? raw.updatedAt : now,
  };
}

/** Reads and validates the notes array from localStorage. Never throws. */
function loadNotes() {
  try {
    const raw = localStorage.getItem(NOTES_STORAGE_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw);
    if (!Array.isArray(parsed)) return [];
    return parsed.map(normalizeNote).filter(Boolean);
  } catch (err) {
    console.warn('Failed to load notes from localStorage; starting with an empty set.', err);
    return [];
  }
}

/** Writes the notes array to localStorage. Returns true on success. */
function saveNotes(notes) {
  try {
    localStorage.setItem(NOTES_STORAGE_KEY, JSON.stringify(notes));
    return true;
  } catch (err) {
    console.error('Failed to save notes to localStorage (storage full or unavailable?).', err);
    return false;
  }
}

/** Removes all saved notes. Mainly useful for tests/debugging. */
function clearNotes() {
  try {
    localStorage.removeItem(NOTES_STORAGE_KEY);
    return true;
  } catch (err) {
    console.error('Failed to clear notes from localStorage.', err);
    return false;
  }
}

/** Reads the saved theme ('light' | 'dark'), defaulting to 'dark'. */
function loadTheme() {
  try {
    const value = localStorage.getItem(THEME_STORAGE_KEY);
    return value === 'light' || value === 'dark' ? value : 'dark';
  } catch (err) {
    return 'dark';
  }
}

/** Persists the theme choice. Returns true on success. */
function saveTheme(theme) {
  try {
    localStorage.setItem(THEME_STORAGE_KEY, theme === 'light' ? 'light' : 'dark');
    return true;
  } catch (err) {
    console.error('Failed to save theme preference to localStorage.', err);
    return false;
  }
}

const NotesStorage = {
  NOTES_STORAGE_KEY,
  THEME_STORAGE_KEY,
  normalizeNote,
  normalizeTags,
  loadNotes,
  saveNotes,
  clearNotes,
  loadTheme,
  saveTheme,
};

if (typeof module !== 'undefined' && module.exports) {
  module.exports = NotesStorage;
}
if (typeof window !== 'undefined') {
  window.NotesStorage = NotesStorage;
}
