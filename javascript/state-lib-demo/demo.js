'use strict';

/**
 * Demo app: a todo list whose entire UI state lives in a store.js store.
 * Every user interaction dispatches a plain action; a single subscribe()
 * callback re-renders the DOM from whatever getState() returns.
 */

// ---------------------------------------------------------------------------
// Action creators
// ---------------------------------------------------------------------------

const addTodo = (text) => ({ type: 'todos/add', payload: { text } });
const toggleTodo = (id) => ({ type: 'todos/toggle', payload: { id } });
const editTodo = (id, text) => ({ type: 'todos/edit', payload: { id, text } });
const removeTodo = (id) => ({ type: 'todos/remove', payload: { id } });
const toggleAll = (completed) => ({ type: 'todos/toggleAll', payload: { completed } });
const clearCompleted = () => ({ type: 'todos/clearCompleted' });
const setFilter = (filter) => ({ type: 'todos/setFilter', payload: { filter } });

// ---------------------------------------------------------------------------
// Reducer
// ---------------------------------------------------------------------------

let nextId = 1;
const makeId = () => nextId++;

const initialState = { todos: [], filter: 'all' };

function todosReducer(state = initialState, action) {
  switch (action.type) {
    case 'todos/add': {
      const text = action.payload.text.trim();
      if (!text) return state;
      return {
        ...state,
        todos: [...state.todos, { id: makeId(), text, completed: false }],
      };
    }

    case 'todos/toggle':
      return {
        ...state,
        todos: state.todos.map((todo) =>
          todo.id === action.payload.id ? { ...todo, completed: !todo.completed } : todo
        ),
      };

    case 'todos/edit': {
      const text = action.payload.text.trim();
      if (!text) return state;
      return {
        ...state,
        todos: state.todos.map((todo) =>
          todo.id === action.payload.id ? { ...todo, text } : todo
        ),
      };
    }

    case 'todos/remove':
      return { ...state, todos: state.todos.filter((todo) => todo.id !== action.payload.id) };

    case 'todos/toggleAll':
      return {
        ...state,
        todos: state.todos.map((todo) => ({ ...todo, completed: action.payload.completed })),
      };

    case 'todos/clearCompleted':
      return { ...state, todos: state.todos.filter((todo) => !todo.completed) };

    case 'todos/setFilter':
      return { ...state, filter: action.payload.filter };

    default:
      return state;
  }
}

const store = createStore(todosReducer);

// ---------------------------------------------------------------------------
// DOM references
// ---------------------------------------------------------------------------

const addForm = document.getElementById('addForm');
const newTodoInput = document.getElementById('newTodoInput');
const toggleAllInput = document.getElementById('toggleAllInput');
const todoListEl = document.getElementById('todoList');
const itemsLeftEl = document.getElementById('itemsLeft');
const filtersEl = document.getElementById('filters');
const clearCompletedBtn = document.getElementById('clearCompletedBtn');
const stateDumpEl = document.getElementById('stateDump');

// Ephemeral, render-only UI state that does NOT belong in the store
// (nothing else in the app needs to know which row is mid-edit).
let editingId = null;

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

function getVisibleTodos(state) {
  switch (state.filter) {
    case 'active':
      return state.todos.filter((t) => !t.completed);
    case 'completed':
      return state.todos.filter((t) => t.completed);
    default:
      return state.todos;
  }
}

function createTodoItem(todo) {
  const li = document.createElement('li');
  li.className = 'todo-item' + (todo.completed ? ' completed' : '');
  li.dataset.id = String(todo.id);

  const checkbox = document.createElement('input');
  checkbox.type = 'checkbox';
  checkbox.className = 'todo-checkbox';
  checkbox.checked = todo.completed;
  checkbox.addEventListener('change', () => store.dispatch(toggleTodo(todo.id)));
  li.appendChild(checkbox);

  if (editingId === todo.id) {
    const editInput = document.createElement('input');
    editInput.type = 'text';
    editInput.className = 'todo-edit-input';
    editInput.value = todo.text;
    editInput.maxLength = 200;

    const commit = () => {
      store.dispatch(editTodo(todo.id, editInput.value));
      editingId = null;
      render();
    };
    const cancel = () => {
      editingId = null;
      render();
    };

    editInput.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        commit();
      } else if (e.key === 'Escape') {
        e.preventDefault();
        cancel();
      }
    });
    editInput.addEventListener('blur', commit);

    li.appendChild(editInput);
    // Focus after insertion; render() runs synchronously so the node exists.
    queueMicrotask(() => {
      editInput.focus();
      editInput.select();
    });
  } else {
    const text = document.createElement('span');
    text.className = 'todo-text';
    text.textContent = todo.text;
    text.title = 'Double-click to edit';
    text.addEventListener('dblclick', () => {
      editingId = todo.id;
      render();
    });
    li.appendChild(text);
  }

  const deleteBtn = document.createElement('button');
  deleteBtn.type = 'button';
  deleteBtn.className = 'todo-delete';
  deleteBtn.innerHTML = '&times;';
  deleteBtn.title = 'Delete';
  deleteBtn.addEventListener('click', () => store.dispatch(removeTodo(todo.id)));
  li.appendChild(deleteBtn);

  return li;
}

function render() {
  const state = store.getState();
  const visibleTodos = getVisibleTodos(state);

  // Todo list
  todoListEl.innerHTML = '';
  if (visibleTodos.length === 0) {
    const empty = document.createElement('li');
    empty.className = 'empty-state';
    empty.textContent =
      state.todos.length === 0 ? 'Nothing here yet — add your first task above.' : 'No todos match this filter.';
    todoListEl.appendChild(empty);
  } else {
    visibleTodos.forEach((todo) => todoListEl.appendChild(createTodoItem(todo)));
  }

  // Footer counts
  const activeCount = state.todos.filter((t) => !t.completed).length;
  const completedCount = state.todos.length - activeCount;
  itemsLeftEl.textContent = `${activeCount} item${activeCount === 1 ? '' : 's'} left`;

  // Filter buttons
  filtersEl.querySelectorAll('.filter-btn').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.filter === state.filter);
  });

  // Clear-completed visibility
  clearCompletedBtn.disabled = completedCount === 0;

  // Toggle-all checkbox reflects whether every todo is complete
  toggleAllInput.checked = state.todos.length > 0 && activeCount === 0;

  // Raw state dump so the store's reactivity is visible, not just implied
  stateDumpEl.textContent = JSON.stringify(state, null, 2);
}

// ---------------------------------------------------------------------------
// Event wiring
// ---------------------------------------------------------------------------

addForm.addEventListener('submit', (e) => {
  e.preventDefault();
  store.dispatch(addTodo(newTodoInput.value));
  newTodoInput.value = '';
  newTodoInput.focus();
});

toggleAllInput.addEventListener('change', () => {
  store.dispatch(toggleAll(toggleAllInput.checked));
});

filtersEl.addEventListener('click', (e) => {
  const btn = e.target.closest('.filter-btn');
  if (!btn) return;
  store.dispatch(setFilter(btn.dataset.filter));
});

clearCompletedBtn.addEventListener('click', () => {
  store.dispatch(clearCompleted());
});

// Re-render on every state change. This is the one place the DOM reacts
// to the store — everything above only ever dispatches actions.
store.subscribe(render);

// Seed a couple of example todos so the list isn't empty on first load.
store.dispatch(addTodo('Explore the store.js source'));
store.dispatch(addTodo('Try editing a todo (double-click it)'));
store.dispatch(addTodo('Toggle a few items, then filter by Active/Completed'));
store.dispatch(toggleTodo(2));

render();

// ---------------------------------------------------------------------------
// Second widget: a counter with undo/redo, backed by its own store instance.
//
// This exists to show store.js is a general-purpose, reusable library — not
// something wired to the todo list specifically. The reducer keeps its own
// `past` / `future` stacks, so "undo" and "redo" are just more actions;
// the store itself has no idea undo/redo is happening.
// ---------------------------------------------------------------------------

const counterInitialState = { past: [], present: 0, future: [] };

function counterReducer(state = counterInitialState, action) {
  switch (action.type) {
    case 'counter/increment':
    case 'counter/decrement': {
      const delta = action.type === 'counter/increment' ? 1 : -1;
      return { past: [...state.past, state.present], present: state.present + delta, future: [] };
    }

    case 'counter/reset':
      if (state.present === 0) return state;
      return { past: [...state.past, state.present], present: 0, future: [] };

    case 'counter/undo': {
      if (state.past.length === 0) return state;
      const previous = state.past[state.past.length - 1];
      return {
        past: state.past.slice(0, -1),
        present: previous,
        future: [state.present, ...state.future],
      };
    }

    case 'counter/redo': {
      if (state.future.length === 0) return state;
      const [next, ...restFuture] = state.future;
      return { past: [...state.past, state.present], present: next, future: restFuture };
    }

    default:
      return state;
  }
}

const counterStore = createStore(counterReducer);

const counterValueEl = document.getElementById('counterValue');
const counterHistoryEl = document.getElementById('counterHistory');
const counterIncBtn = document.getElementById('counterInc');
const counterDecBtn = document.getElementById('counterDec');
const counterResetBtn = document.getElementById('counterReset');
const counterUndoBtn = document.getElementById('counterUndo');
const counterRedoBtn = document.getElementById('counterRedo');

function renderCounter() {
  const state = counterStore.getState();
  counterValueEl.textContent = String(state.present);
  counterUndoBtn.disabled = state.past.length === 0;
  counterRedoBtn.disabled = state.future.length === 0;
  counterHistoryEl.textContent = `past: [${state.past.join(', ')}]   future: [${state.future.join(', ')}]`;
}

counterIncBtn.addEventListener('click', () => counterStore.dispatch({ type: 'counter/increment' }));
counterDecBtn.addEventListener('click', () => counterStore.dispatch({ type: 'counter/decrement' }));
counterResetBtn.addEventListener('click', () => counterStore.dispatch({ type: 'counter/reset' }));
counterUndoBtn.addEventListener('click', () => counterStore.dispatch({ type: 'counter/undo' }));
counterRedoBtn.addEventListener('click', () => counterStore.dispatch({ type: 'counter/redo' }));

counterStore.subscribe(renderCounter);
renderCounter();
