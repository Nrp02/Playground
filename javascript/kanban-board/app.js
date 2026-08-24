'use strict';

/**
 * Vanilla JS Kanban Board
 * - HTML5 Drag and Drop API for moving cards between columns
 * - CRUD on tasks (add / edit / delete) plus a cyclable priority field
 * - Live text search across all columns
 * - Undo toast after deleting a task
 * - State persisted to localStorage
 */

const STORAGE_KEY = 'kanban-board-state-v1';
const STATUSES = ['todo', 'in-progress', 'done'];
const PRIORITIES = ['low', 'medium', 'high'];
const PRIORITY_LABELS = { low: 'Low', medium: 'Med', high: 'High' };

const board = document.getElementById('board');
const taskCountEl = document.getElementById('taskCount');
const clearBoardBtn = document.getElementById('clearBoardBtn');
const searchInput = document.getElementById('searchInput');
const toastContainer = document.getElementById('toastContainer');
const cardTemplate = document.getElementById('taskCardTemplate');

/** @type {{tasks: {id: string, text: string, status: string, priority: string, createdAt: number}[]}} */
let state = loadState();

let draggedTaskId = null;
let searchTerm = '';
let pendingUndo = null; // { task, index, timeoutId }

function uid() {
  return `t-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}

function defaultState() {
  return {
    tasks: [
      { id: uid(), text: 'Welcome! Drag this card to another column.', status: 'todo', priority: 'medium', createdAt: Date.now() },
      { id: uid(), text: 'Click the priority badge to cycle Low / Med / High.', status: 'todo', priority: 'low', createdAt: Date.now() },
      { id: uid(), text: 'Click the pencil icon to edit a task.', status: 'in-progress', priority: 'high', createdAt: Date.now() },
      { id: uid(), text: 'Refresh the page — your board is saved.', status: 'done', priority: 'medium', createdAt: Date.now() },
    ],
  };
}

function normalizeTask(t) {
  if (!t || typeof t.id !== 'string' || typeof t.text !== 'string' || !STATUSES.includes(t.status)) {
    return null;
  }
  return {
    id: t.id,
    text: t.text,
    status: t.status,
    priority: PRIORITIES.includes(t.priority) ? t.priority : 'medium',
    createdAt: typeof t.createdAt === 'number' ? t.createdAt : Date.now(),
  };
}

function loadState() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return defaultState();
    const parsed = JSON.parse(raw);
    if (!parsed || !Array.isArray(parsed.tasks)) return defaultState();
    return { tasks: parsed.tasks.map(normalizeTask).filter(Boolean) };
  } catch (err) {
    console.warn('Failed to load kanban state, starting fresh.', err);
    return defaultState();
  }
}

function saveState() {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
  } catch (err) {
    console.error('Failed to save kanban state to localStorage.', err);
  }
}

function addTask(text, status, priority) {
  const trimmed = text.trim();
  if (!trimmed) return;
  state.tasks.push({
    id: uid(),
    text: trimmed,
    status,
    priority: PRIORITIES.includes(priority) ? priority : 'medium',
    createdAt: Date.now(),
  });
  saveState();
  render();
}

function deleteTask(id) {
  const index = state.tasks.findIndex((t) => t.id === id);
  if (index === -1) return;
  const [removed] = state.tasks.splice(index, 1);
  saveState();
  render();
  showUndoToast(removed, index);
}

function restoreTask(task, index) {
  const safeIndex = Math.min(index, state.tasks.length);
  state.tasks.splice(safeIndex, 0, task);
  saveState();
  render();
}

function editTask(id, newText) {
  const trimmed = newText.trim();
  const task = state.tasks.find((t) => t.id === id);
  if (!task) return;
  if (trimmed) {
    task.text = trimmed;
  }
  saveState();
  render();
}

function cyclePriority(id) {
  const task = state.tasks.find((t) => t.id === id);
  if (!task) return;
  const currentIndex = PRIORITIES.indexOf(task.priority);
  task.priority = PRIORITIES[(currentIndex + 1) % PRIORITIES.length];
  saveState();
  render();
}

function moveTask(id, newStatus, beforeId) {
  const task = state.tasks.find((t) => t.id === id);
  if (!task) return;

  // Remove task from its current position, then reinsert so column order
  // reflects manual drag position (not just status changes).
  const withoutTask = state.tasks.filter((t) => t.id !== id);
  task.status = newStatus;

  if (beforeId) {
    const idx = withoutTask.findIndex((t) => t.id === beforeId);
    if (idx === -1) {
      withoutTask.push(task);
    } else {
      withoutTask.splice(idx, 0, task);
    }
  } else {
    withoutTask.push(task);
  }

  state.tasks = withoutTask;
  saveState();
  render();
}

function clearBoard() {
  if (state.tasks.length === 0) return;
  const confirmed = window.confirm('Remove all tasks from the board? This cannot be undone.');
  if (!confirmed) return;
  state.tasks = [];
  saveState();
  render();
}

// -- Undo toast --------------------------------------------------------------

function showUndoToast(task, index) {
  if (pendingUndo) {
    clearTimeout(pendingUndo.timeoutId);
    toastContainer.innerHTML = '';
  }

  const toast = document.createElement('div');
  toast.className = 'toast';

  const message = document.createElement('span');
  message.textContent = `Deleted "${task.text.length > 40 ? task.text.slice(0, 40) + '…' : task.text}"`;
  toast.appendChild(message);

  const undoBtn = document.createElement('button');
  undoBtn.className = 'toast-undo-btn';
  undoBtn.textContent = 'Undo';
  undoBtn.addEventListener('click', () => {
    clearTimeout(pendingUndo.timeoutId);
    restoreTask(task, index);
    dismissToast();
  });
  toast.appendChild(undoBtn);

  toastContainer.innerHTML = '';
  toastContainer.appendChild(toast);

  const timeoutId = setTimeout(dismissToast, 5000);
  pendingUndo = { task, index, timeoutId };
}

function dismissToast() {
  toastContainer.innerHTML = '';
  pendingUndo = null;
}

// -- Filtering ----------------------------------------------------------------

function matchesSearch(task) {
  if (!searchTerm) return true;
  return task.text.toLowerCase().includes(searchTerm);
}

function tasksForStatus(status) {
  return state.tasks.filter((t) => t.status === status);
}

// -- Rendering ------------------------------------------------------------

function createTaskCard(task) {
  const fragment = cardTemplate.content.cloneNode(true);
  const card = fragment.querySelector('.task-card');
  const textEl = fragment.querySelector('.task-text');
  const editInput = fragment.querySelector('.task-edit-input');
  const editBtn = fragment.querySelector('.edit-btn');
  const deleteBtn = fragment.querySelector('.delete-btn');
  const priorityBadge = fragment.querySelector('.priority-badge');

  card.dataset.id = task.id;
  textEl.textContent = task.text;
  editInput.value = task.text;

  priorityBadge.textContent = PRIORITY_LABELS[task.priority];
  priorityBadge.classList.add(`priority-${task.priority}`);
  priorityBadge.addEventListener('click', () => cyclePriority(task.id));

  card.addEventListener('dragstart', (e) => {
    draggedTaskId = task.id;
    card.classList.add('dragging');
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/plain', task.id);
  });

  card.addEventListener('dragend', () => {
    card.classList.remove('dragging');
    draggedTaskId = null;
    document.querySelectorAll('.column').forEach((c) => c.classList.remove('drag-over'));
  });

  function enterEditMode() {
    textEl.style.display = 'none';
    editInput.style.display = 'block';
    editInput.value = task.text;
    editInput.focus();
    editInput.select();
  }

  function exitEditMode(commit) {
    if (commit) {
      editTask(task.id, editInput.value);
      return; // render() will rebuild everything
    }
    textEl.style.display = '';
    editInput.style.display = 'none';
  }

  editBtn.addEventListener('click', enterEditMode);
  textEl.addEventListener('dblclick', enterEditMode);

  editInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      exitEditMode(true);
    } else if (e.key === 'Escape') {
      e.preventDefault();
      exitEditMode(false);
    }
  });

  editInput.addEventListener('blur', () => {
    // Commit on blur so users don't lose edits by clicking away.
    if (editInput.style.display !== 'none') {
      exitEditMode(true);
    }
  });

  deleteBtn.addEventListener('click', () => {
    card.classList.add('dragging');
    deleteTask(task.id);
  });

  return fragment;
}

function getDragAfterElement(container, mouseY) {
  const cards = [...container.querySelectorAll('.task-card:not(.dragging)')];

  let closest = { offset: Number.NEGATIVE_INFINITY, element: null };
  for (const child of cards) {
    const box = child.getBoundingClientRect();
    const offset = mouseY - box.top - box.height / 2;
    if (offset < 0 && offset > closest.offset) {
      closest = { offset, element: child };
    }
  }
  return closest.element;
}

function setupColumnDropZone(listEl) {
  const status = listEl.dataset.status;

  listEl.addEventListener('dragover', (e) => {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
    listEl.closest('.column').classList.add('drag-over');

    const afterElement = getDragAfterElement(listEl, e.clientY);
    const draggingCard = listEl.querySelector('.task-card.dragging') ||
      document.querySelector('.task-card.dragging');
    if (!draggingCard) return;

    if (afterElement == null) {
      listEl.appendChild(draggingCard);
    } else {
      listEl.insertBefore(draggingCard, afterElement);
    }
  });

  listEl.addEventListener('dragleave', (e) => {
    if (!listEl.contains(e.relatedTarget)) {
      listEl.closest('.column').classList.remove('drag-over');
    }
  });

  listEl.addEventListener('drop', (e) => {
    e.preventDefault();
    listEl.closest('.column').classList.remove('drag-over');

    const id = draggedTaskId || e.dataTransfer.getData('text/plain');
    if (!id) return;

    // Determine which card (if any) the dropped card now sits before,
    // based on the live DOM order set up during dragover.
    const cardEls = [...listEl.querySelectorAll('.task-card')];
    const droppedIndex = cardEls.findIndex((el) => el.dataset.id === id);
    const beforeEl = droppedIndex >= 0 ? cardEls[droppedIndex + 1] : null;
    const beforeId = beforeEl ? beforeEl.dataset.id : null;

    moveTask(id, status, beforeId);
  });
}

function renderColumn(status) {
  const listEl = document.getElementById(`list-${status}`);
  const countEl = document.getElementById(`count-${status}`);
  const allTasks = tasksForStatus(status);
  const visibleTasks = allTasks.filter(matchesSearch);

  listEl.innerHTML = '';

  if (visibleTasks.length === 0) {
    const hint = document.createElement('div');
    hint.className = 'empty-hint';
    hint.textContent = searchTerm
      ? 'No tasks match your search.'
      : 'No tasks — drag one here or add a new one.';
    listEl.appendChild(hint);
  } else {
    visibleTasks.forEach((task) => {
      listEl.appendChild(createTaskCard(task));
    });
  }

  countEl.textContent = searchTerm
    ? `${visibleTasks.length}/${allTasks.length}`
    : String(allTasks.length);
}

function render() {
  STATUSES.forEach(renderColumn);
  taskCountEl.textContent = `${state.tasks.length} task${state.tasks.length === 1 ? '' : 's'}`;
}

function init() {
  STATUSES.forEach((status) => {
    const listEl = document.getElementById(`list-${status}`);
    setupColumnDropZone(listEl);
  });

  document.querySelectorAll('.add-task-form').forEach((form) => {
    form.addEventListener('submit', (e) => {
      e.preventDefault();
      const input = form.querySelector('.add-task-input');
      const prioritySelect = form.querySelector('.priority-select');
      addTask(input.value, form.dataset.status, prioritySelect.value);
      input.value = '';
      input.focus();
    });
  });

  searchInput.addEventListener('input', () => {
    searchTerm = searchInput.value.trim().toLowerCase();
    render();
  });

  clearBoardBtn.addEventListener('click', clearBoard);

  render();
}

init();
