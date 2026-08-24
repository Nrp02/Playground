'use strict';

/**
 * Canvas Breakout
 * Vanilla JS + Canvas 2D API. No dependencies.
 *
 * Structure:
 *  - Paddle / Ball / Brick classes hold their own geometry + draw logic
 *  - Game class owns the loop, input, collision detection and state machine
 */

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

const CONFIG = {
  width: 800,
  height: 560,
  paddle: {
    width: 110,
    height: 14,
    speed: 620, // px/sec for keyboard control
    yOffset: 30, // distance from bottom edge
  },
  ball: {
    radius: 8,
    baseSpeed: 320, // px/sec
    speedIncreasePerLevel: 40,
    maxSpeed: 620,
  },
  bricks: {
    cols: 10,
    rowHeight: 24,
    padding: 6,
    top: 60,
    sideMargin: 30,
  },
  maxLevel: 3,
};

const HIGH_SCORES_KEY = 'canvas-breakout-highscores-v1';
const MUTE_KEY = 'canvas-breakout-muted-v1';
const MAX_HIGH_SCORES = 5;

const ROW_STYLES = [
  { color: '#f87171', points: 50 },
  { color: '#fb923c', points: 40 },
  { color: '#fbbf24', points: 30 },
  { color: '#34d399', points: 20 },
  { color: '#38bdf8', points: 10 },
];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function loadHighScores() {
  try {
    const raw = localStorage.getItem(HIGH_SCORES_KEY);
    const list = raw ? JSON.parse(raw) : [];
    if (!Array.isArray(list)) return [];
    return list
      .filter((n) => Number.isFinite(n))
      .sort((a, b) => b - a)
      .slice(0, MAX_HIGH_SCORES);
  } catch (err) {
    return [];
  }
}

function saveHighScores(list) {
  try {
    localStorage.setItem(HIGH_SCORES_KEY, JSON.stringify(list));
  } catch (err) {
    /* localStorage unavailable — ignore */
  }
}

/** Adds `score` to the leaderboard, persists it, and returns the new top-5 list. */
function recordScore(scores, score) {
  const updated = [...scores, score].sort((a, b) => b - a).slice(0, MAX_HIGH_SCORES);
  saveHighScores(updated);
  return updated;
}

function loadMuted() {
  try {
    return localStorage.getItem(MUTE_KEY) === 'true';
  } catch (err) {
    return false;
  }
}

function saveMuted(muted) {
  try {
    localStorage.setItem(MUTE_KEY, String(muted));
  } catch (err) {
    /* localStorage unavailable — ignore */
  }
}

// ---------------------------------------------------------------------------
// Sound (WebAudio, synthesized — no audio files/dependencies)
// ---------------------------------------------------------------------------

const SoundFX = {
  ctx: null,
  muted: false,

  ensureContext() {
    if (!this.ctx) {
      const AudioContextClass = window.AudioContext || window.webkitAudioContext;
      if (!AudioContextClass) return null;
      this.ctx = new AudioContextClass();
    }
    if (this.ctx.state === 'suspended') {
      this.ctx.resume();
    }
    return this.ctx;
  },

  tone(freq, duration, { type = 'sine', volume = 0.15, delay = 0, glideTo = null } = {}) {
    if (this.muted) return;
    const ctx = this.ensureContext();
    if (!ctx) return;

    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.type = type;
    osc.frequency.setValueAtTime(freq, ctx.currentTime + delay);
    if (glideTo !== null) {
      osc.frequency.linearRampToValueAtTime(glideTo, ctx.currentTime + delay + duration);
    }

    // Small attack + exponential decay so tones click less and sound less harsh.
    gain.gain.setValueAtTime(0, ctx.currentTime + delay);
    gain.gain.linearRampToValueAtTime(volume, ctx.currentTime + delay + 0.01);
    gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + delay + duration);

    osc.connect(gain);
    gain.connect(ctx.destination);
    osc.start(ctx.currentTime + delay);
    osc.stop(ctx.currentTime + delay + duration + 0.02);
  },

  launch() {
    this.tone(440, 0.07, { type: 'sine', volume: 0.1 });
  },
  paddleHit() {
    this.tone(220, 0.08, { type: 'square', volume: 0.12 });
  },
  wallBounce() {
    this.tone(160, 0.06, { type: 'sine', volume: 0.07 });
  },
  brickBreak(points) {
    // Higher-value bricks ring a little higher pitched.
    this.tone(300 + points * 4, 0.1, { type: 'triangle', volume: 0.14 });
  },
  loseLife() {
    this.tone(220, 0.35, { type: 'sawtooth', volume: 0.15, glideTo: 80 });
  },
  levelComplete() {
    [523.25, 659.25, 783.99].forEach((freq, i) =>
      this.tone(freq, 0.15, { type: 'square', volume: 0.13, delay: i * 0.12 })
    );
  },
  gameOver() {
    this.tone(160, 0.6, { type: 'sawtooth', volume: 0.15, glideTo: 40 });
  },
  win() {
    [523.25, 659.25, 783.99, 1046.5].forEach((freq, i) =>
      this.tone(freq, 0.2, { type: 'square', volume: 0.14, delay: i * 0.14 })
    );
  },
};

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------

class Paddle {
  constructor() {
    this.width = CONFIG.paddle.width;
    this.height = CONFIG.paddle.height;
    this.x = (CONFIG.width - this.width) / 2;
    this.y = CONFIG.height - CONFIG.paddle.yOffset - this.height;
  }

  reset() {
    this.x = (CONFIG.width - this.width) / 2;
  }

  moveTo(centerX) {
    this.x = clamp(centerX - this.width / 2, 0, CONFIG.width - this.width);
  }

  moveBy(dx) {
    this.x = clamp(this.x + dx, 0, CONFIG.width - this.width);
  }

  get centerX() {
    return this.x + this.width / 2;
  }

  draw(ctx) {
    const gradient = ctx.createLinearGradient(this.x, 0, this.x + this.width, 0);
    gradient.addColorStop(0, '#38bdf8');
    gradient.addColorStop(1, '#f472b6');
    ctx.fillStyle = gradient;
    ctx.beginPath();
    ctx.roundRect(this.x, this.y, this.width, this.height, 7);
    ctx.fill();
  }
}

class Ball {
  constructor() {
    this.radius = CONFIG.ball.radius;
    this.reset();
  }

  reset(paddle) {
    this.speed = CONFIG.ball.baseSpeed;
    this.stuck = true;
    this.paddleRef = paddle || this.paddleRef;
    this.vx = 0;
    this.vy = 0;
    if (this.paddleRef) {
      this.x = this.paddleRef.centerX;
      this.y = this.paddleRef.y - this.radius - 1;
    } else {
      this.x = CONFIG.width / 2;
      this.y = CONFIG.height - 60;
    }
  }

  launch() {
    if (!this.stuck) return;
    this.stuck = false;
    const angle = (Math.random() * 0.5 + 0.25) * Math.PI; // between 45 and 135 degrees
    this.vx = this.speed * Math.cos(angle) * (Math.random() < 0.5 ? -1 : 1);
    this.vy = -Math.abs(this.speed * Math.sin(angle));
  }

  stickToPaddle() {
    if (!this.stuck || !this.paddleRef) return;
    this.x = this.paddleRef.centerX;
    this.y = this.paddleRef.y - this.radius - 1;
  }

  draw(ctx) {
    ctx.fillStyle = '#f8fafc';
    ctx.beginPath();
    ctx.arc(this.x, this.y, this.radius, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = 'rgba(56, 189, 248, 0.6)';
    ctx.lineWidth = 2;
    ctx.stroke();
  }
}

class Brick {
  constructor(x, y, width, height, color, points) {
    this.x = x;
    this.y = y;
    this.width = width;
    this.height = height;
    this.color = color;
    this.points = points;
    this.alive = true;
  }

  draw(ctx) {
    if (!this.alive) return;
    ctx.fillStyle = this.color;
    ctx.beginPath();
    ctx.roundRect(this.x, this.y, this.width, this.height, 3);
    ctx.fill();
    ctx.strokeStyle = 'rgba(0,0,0,0.25)';
    ctx.lineWidth = 1;
    ctx.stroke();
  }
}

/** A single fading debris square used for the brick-break burst effect. */
class Particle {
  constructor(x, y, color) {
    this.x = x;
    this.y = y;
    const angle = Math.random() * Math.PI * 2;
    const speed = 60 + Math.random() * 140;
    this.vx = Math.cos(angle) * speed;
    this.vy = Math.sin(angle) * speed;
    this.color = color;
    this.size = 2 + Math.random() * 2.5;
    this.life = 1; // 1 = fully visible, 0 = dead
    this.decay = 1.2 + Math.random() * 0.8; // life lost per second
  }

  update(dt) {
    this.x += this.vx * dt;
    this.y += this.vy * dt;
    this.vy += 220 * dt; // gentle gravity so debris arcs downward
    this.life -= this.decay * dt;
  }

  get alive() {
    return this.life > 0;
  }

  draw(ctx) {
    ctx.globalAlpha = Math.max(this.life, 0);
    ctx.fillStyle = this.color;
    ctx.fillRect(this.x - this.size / 2, this.y - this.size / 2, this.size, this.size);
    ctx.globalAlpha = 1;
  }
}

function spawnBrickParticles(particles, brick) {
  const cx = brick.x + brick.width / 2;
  const cy = brick.y + brick.height / 2;
  for (let i = 0; i < 10; i++) {
    particles.push(new Particle(cx, cy, brick.color));
  }
}

// ---------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------

class Game {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');

    this.scoreEl = document.getElementById('score');
    this.livesEl = document.getElementById('lives');
    this.levelEl = document.getElementById('level');
    this.highScoreEl = document.getElementById('highScore');
    this.overlay = document.getElementById('overlay');
    this.overlayTitle = document.getElementById('overlayTitle');
    this.overlayMessage = document.getElementById('overlayMessage');
    this.startBtn = document.getElementById('startBtn');
    this.muteBtn = document.getElementById('muteBtn');

    this.paddle = new Paddle();
    this.ball = new Ball();
    this.ball.reset(this.paddle);
    this.bricks = [];
    this.particles = [];

    this.score = 0;
    this.lives = 3;
    this.level = 1;
    this.highScores = loadHighScores();
    this.highScore = this.highScores[0] || 0;
    this.status = 'idle'; // idle | playing | paused | won | lost | level-complete
    this.keys = { left: false, right: false };
    this.mouseX = null;

    this.muted = loadMuted();
    SoundFX.muted = this.muted;

    this._lastTime = null;
    this._boundLoop = this.loop.bind(this);

    this.highScoreEl.textContent = String(this.highScore);
    this.updateMuteButton();

    this.bindInput();
    this.buildLevel(this.level);
    this.draw(); // paint initial frame behind the overlay
  }

  // -- Setup ----------------------------------------------------------------

  bindInput() {
    window.addEventListener('keydown', (e) => {
      if (e.code === 'ArrowLeft') this.keys.left = true;
      if (e.code === 'ArrowRight') this.keys.right = true;
      if (e.code === 'Space') {
        e.preventDefault();
        if (this.status === 'playing' && this.ball.stuck) {
          this.ball.launch();
          SoundFX.launch();
        }
      }
      if (e.code === 'KeyP') {
        this.togglePause();
      }
      if (e.code === 'KeyM') {
        this.toggleMute();
      }
    });

    window.addEventListener('keyup', (e) => {
      if (e.code === 'ArrowLeft') this.keys.left = false;
      if (e.code === 'ArrowRight') this.keys.right = false;
    });

    this.canvas.addEventListener('mousemove', (e) => {
      const rect = this.canvas.getBoundingClientRect();
      const scaleX = CONFIG.width / rect.width;
      this.mouseX = (e.clientX - rect.left) * scaleX;
    });

    this.canvas.addEventListener('click', () => {
      SoundFX.ensureContext();
      if (this.status === 'playing' && this.ball.stuck) {
        this.ball.launch();
        SoundFX.launch();
      }
    });

    this.startBtn.addEventListener('click', () => {
      SoundFX.ensureContext();
      this.handleOverlayAction();
    });

    this.muteBtn.addEventListener('click', () => this.toggleMute());
  }

  toggleMute() {
    this.muted = !this.muted;
    SoundFX.muted = this.muted;
    saveMuted(this.muted);
    this.updateMuteButton();
  }

  updateMuteButton() {
    this.muteBtn.classList.toggle('muted', this.muted);
    this.muteBtn.innerHTML = this.muted ? '&#128263;' : '&#128266;';
    this.muteBtn.title = this.muted ? 'Sound off (click to unmute)' : 'Sound on (click to mute)';
  }

  handleOverlayAction() {
    if (this.status === 'idle' || this.status === 'lost' || this.status === 'won') {
      this.startNewGame();
    } else if (this.status === 'level-complete') {
      this.startNextLevel();
    }
  }

  // -- Level / bricks ---------------------------------------------------------

  buildLevel(level) {
    this.bricks = [];
    const rows = Math.min(ROW_STYLES.length, 3 + level); // more rows each level
    const { cols, rowHeight, padding, top, sideMargin } = CONFIG.bricks;
    const brickWidth = (CONFIG.width - sideMargin * 2 - padding * (cols - 1)) / cols;

    for (let row = 0; row < rows; row++) {
      const style = ROW_STYLES[row % ROW_STYLES.length];
      for (let col = 0; col < cols; col++) {
        // Sparse pattern on later rows keeps things interesting instead of a solid block.
        if (level >= 3 && row === rows - 1 && col % 4 === 0) continue;

        const x = sideMargin + col * (brickWidth + padding);
        const y = top + row * (rowHeight + padding);
        this.bricks.push(new Brick(x, y, brickWidth, rowHeight, style.color, style.points));
      }
    }
  }

  get bricksRemaining() {
    return this.bricks.filter((b) => b.alive).length;
  }

  // -- State transitions ------------------------------------------------------

  startNewGame() {
    this.score = 0;
    this.lives = 3;
    this.level = 1;
    this.particles = [];
    this.buildLevel(this.level);
    this.paddle.reset();
    this.ball.reset(this.paddle);
    this.updateHud();
    this.setStatus('playing');
    this._lastTime = null;
    requestAnimationFrame(this._boundLoop);
  }

  startNextLevel() {
    this.level += 1;
    const newSpeed = Math.min(
      CONFIG.ball.baseSpeed + (this.level - 1) * CONFIG.ball.speedIncreasePerLevel,
      CONFIG.ball.maxSpeed
    );
    this.particles = [];
    this.buildLevel(this.level);
    this.paddle.reset();
    this.ball.reset(this.paddle);
    this.ball.speed = newSpeed;
    this.updateHud();
    this.setStatus('playing');
    this._lastTime = null;
    requestAnimationFrame(this._boundLoop);
  }

  loseLife() {
    this.lives -= 1;
    this.updateHud();
    if (this.lives <= 0) {
      this.gameOver();
    } else {
      SoundFX.loseLife();
      this.ball.reset(this.paddle);
      this.paddle.reset();
    }
  }

  gameOver() {
    this.highScores = recordScore(this.highScores, this.score);
    this.highScore = this.highScores[0] || 0;
    this.highScoreEl.textContent = String(this.highScore);
    SoundFX.gameOver();
    this.setStatus('lost');
  }

  levelComplete() {
    if (this.level >= CONFIG.maxLevel) {
      this.highScores = recordScore(this.highScores, this.score);
      this.highScore = this.highScores[0] || 0;
      this.highScoreEl.textContent = String(this.highScore);
      SoundFX.win();
      this.setStatus('won');
    } else {
      SoundFX.levelComplete();
      this.setStatus('level-complete');
    }
  }

  /** Small ranked list of the best scores across sessions (localStorage-backed). */
  renderHighScoresHtml() {
    if (this.highScores.length === 0) return '';
    const items = this.highScores
      .map((s) => `<li>${s}${s === this.score ? ' <em>&larr; this run</em>' : ''}</li>`)
      .join('');
    return `<ol class="high-score-list">${items}</ol>`;
  }

  togglePause() {
    if (this.status === 'playing') {
      this.setStatus('paused');
    } else if (this.status === 'paused') {
      this.setStatus('playing');
      this._lastTime = null;
      requestAnimationFrame(this._boundLoop);
    }
  }

  setStatus(status) {
    this.status = status;
    const overlays = {
      idle: null,
      playing: null,
      paused: { title: 'Paused', message: 'Press P to resume.', btn: null },
      lost: {
        title: 'Game Over',
        message: `Final score: <strong>${this.score}</strong>${this.renderHighScoresHtml()}`,
        btn: 'Play Again',
      },
      won: {
        title: 'You Win!',
        message: `You cleared all ${CONFIG.maxLevel} levels.<br>Final score: <strong>${this.score}</strong>${this.renderHighScoresHtml()}`,
        btn: 'Play Again',
      },
      'level-complete': {
        title: `Level ${this.level} Complete!`,
        message: `Score so far: <strong>${this.score}</strong><br>Get ready for level ${this.level + 1} — the ball speeds up.`,
        btn: 'Next Level',
      },
    };

    const cfg = overlays[status];
    if (status === 'playing') {
      this.overlay.classList.add('hidden');
      return;
    }
    if (!cfg) return;
    this.overlay.classList.remove('hidden');
    this.overlayTitle.textContent = cfg.title;
    this.overlayMessage.innerHTML = cfg.message;
    this.startBtn.style.display = cfg.btn ? 'inline-block' : 'none';
    this.startBtn.textContent = cfg.btn || '';
  }

  updateHud() {
    this.scoreEl.textContent = String(this.score);
    this.livesEl.textContent = String(this.lives);
    this.levelEl.textContent = String(this.level);
  }

  // -- Update / collision -------------------------------------------------

  update(dt) {
    // Debris particles keep animating regardless of ball state.
    this.particles.forEach((p) => p.update(dt));
    if (this.particles.length > 0) {
      this.particles = this.particles.filter((p) => p.alive);
    }

    // Paddle movement (keyboard)
    if (this.keys.left) this.paddle.moveBy(-CONFIG.paddle.speed * dt);
    if (this.keys.right) this.paddle.moveBy(CONFIG.paddle.speed * dt);

    // Paddle movement (mouse) — only applied when the mouse has moved onto the canvas
    if (this.mouseX !== null && !this.keys.left && !this.keys.right) {
      this.paddle.moveTo(this.mouseX);
    }

    if (this.ball.stuck) {
      this.ball.stickToPaddle();
      return;
    }

    const ball = this.ball;
    const prevX = ball.x;

    ball.x += ball.vx * dt;
    ball.y += ball.vy * dt;

    // Wall collisions
    if (ball.x - ball.radius <= 0) {
      ball.x = ball.radius;
      ball.vx = Math.abs(ball.vx);
      SoundFX.wallBounce();
    } else if (ball.x + ball.radius >= CONFIG.width) {
      ball.x = CONFIG.width - ball.radius;
      ball.vx = -Math.abs(ball.vx);
      SoundFX.wallBounce();
    }
    if (ball.y - ball.radius <= 0) {
      ball.y = ball.radius;
      ball.vy = Math.abs(ball.vy);
      SoundFX.wallBounce();
    }

    // Bottom edge — lose a life
    if (ball.y - ball.radius > CONFIG.height) {
      this.loseLife();
      return;
    }

    // Paddle collision (only when moving downward)
    if (ball.vy > 0 && this.circleRectOverlap(ball, this.paddle)) {
      ball.y = this.paddle.y - ball.radius;
      ball.vy = -Math.abs(ball.vy);
      // Hit position across paddle (-1 left edge .. 1 right edge) steers the ball.
      const hitPos = (ball.x - this.paddle.centerX) / (this.paddle.width / 2);
      const maxAngle = (Math.PI * 5) / 12; // 75 degrees
      const angle = clamp(hitPos, -1, 1) * maxAngle;
      const speed = Math.hypot(ball.vx, ball.vy);
      ball.vx = speed * Math.sin(angle);
      ball.vy = -Math.abs(speed * Math.cos(angle));
      SoundFX.paddleHit();
    }

    // Brick collisions — at most one brick resolved per frame to keep physics stable
    for (const brick of this.bricks) {
      if (!brick.alive) continue;
      if (this.circleRectOverlap(ball, brick)) {
        brick.alive = false;
        this.score += brick.points;
        this.updateHud();
        SoundFX.brickBreak(brick.points);
        spawnBrickParticles(this.particles, brick);

        const hitFromSide =
          prevX + ball.radius <= brick.x || prevX - ball.radius >= brick.x + brick.width;
        if (hitFromSide) {
          ball.vx = -ball.vx;
        } else {
          ball.vy = -ball.vy;
        }
        break;
      }
    }

    if (this.bricksRemaining === 0) {
      this.levelComplete();
    }
  }

  circleRectOverlap(circle, rect) {
    const closestX = clamp(circle.x, rect.x, rect.x + rect.width);
    const closestY = clamp(circle.y, rect.y, rect.y + rect.height);
    const dx = circle.x - closestX;
    const dy = circle.y - closestY;
    return dx * dx + dy * dy < circle.radius * circle.radius;
  }

  // -- Render ---------------------------------------------------------------

  draw() {
    const ctx = this.ctx;
    ctx.clearRect(0, 0, CONFIG.width, CONFIG.height);

    // subtle background grid
    ctx.strokeStyle = 'rgba(255,255,255,0.03)';
    ctx.lineWidth = 1;
    for (let x = 0; x < CONFIG.width; x += 40) {
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, CONFIG.height);
      ctx.stroke();
    }

    this.bricks.forEach((b) => b.draw(ctx));
    this.particles.forEach((p) => p.draw(ctx));
    this.paddle.draw(ctx);
    this.ball.draw(ctx);

    if (this.ball.stuck && this.status === 'playing') {
      ctx.fillStyle = 'rgba(230,233,245,0.7)';
      ctx.font = '14px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('Press SPACE or click to launch', CONFIG.width / 2, this.paddle.y - 20);
    }
  }

  // -- Loop -------------------------------------------------------------------

  loop(timestamp) {
    if (this.status !== 'playing') return;

    if (this._lastTime === null) this._lastTime = timestamp;
    let dt = (timestamp - this._lastTime) / 1000;
    this._lastTime = timestamp;
    dt = Math.min(dt, 1 / 30); // clamp to avoid huge jumps after tab switches

    this.update(dt);
    this.draw();

    requestAnimationFrame(this._boundLoop);
  }
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

const canvas = document.getElementById('gameCanvas');
const game = new Game(canvas);

// Exposed for manual testing / debugging from the browser console.
window.__breakoutGame = game;
