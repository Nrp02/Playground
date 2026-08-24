'use strict';

const scheduler = new TaskScheduler();

const form = document.getElementById('addForm');
const nameInput = document.getElementById('taskName');
const priorityInput = document.getElementById('taskPriority');
const queueList = document.getElementById('queueList');
const runNextBtn = document.getElementById('runNextBtn');
const runAllBtn = document.getElementById('runAllBtn');
const logPanel = document.getElementById('logPanel');

function render() {
  const items = scheduler.snapshot();

  queueList.innerHTML = '';
  if (items.length === 0) {
    const empty = document.createElement('li');
    empty.className = 'empty-state';
    empty.textContent = 'Queue is empty — add a task above.';
    queueList.appendChild(empty);
  } else {
    for (const task of items) {
      const li = document.createElement('li');
      li.className = 'queue-item';

      const badge = document.createElement('span');
      badge.className = 'priority-badge';
      badge.textContent = task.priority;

      const label = document.createElement('span');
      label.textContent = task.name;
      label.style.flex = '1';

      li.appendChild(badge);
      li.appendChild(label);
      queueList.appendChild(li);
    }
  }

  const log = scheduler.executionLog();
  logPanel.textContent = log.length === 0 ? '(nothing run yet)' : log.map((name, i) => `${i + 1}. ${name}`).join('\n');

  runNextBtn.disabled = scheduler.isEmpty();
  runAllBtn.disabled = scheduler.isEmpty();
}

form.addEventListener('submit', (e) => {
  e.preventDefault();
  const name = nameInput.value.trim();
  const priority = Number(priorityInput.value);
  if (!name || !Number.isFinite(priority)) {
    return;
  }
  scheduler.schedule(name, priority);
  nameInput.value = '';
  priorityInput.value = '5';
  nameInput.focus();
  render();
});

runNextBtn.addEventListener('click', () => {
  scheduler.runNext();
  render();
});

runAllBtn.addEventListener('click', () => {
  scheduler.runAll();
  render();
});

['grab coffee:8', 'fix prod outage:1', 'reply to email:6', 'deploy hotfix:2', 'write docs:9'].forEach((entry) => {
  const [name, priority] = entry.split(':');
  scheduler.schedule(name, Number(priority));
});

render();
