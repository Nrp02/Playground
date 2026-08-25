'use strict';

const assert = require('assert');
const { SocialGraph } = require('./graph.js');
const { PostStore } = require('./postStore.js');
const { OpsCounter } = require('./opsCounter.js');
const { FanOutOnWriteFeedBuilder } = require('./fanOutOnWrite.js');

let passCount = 0;
let failCount = 0;

function test(name, fn) {
  try {
    fn();
    passCount++;
    console.log(`  PASS  ${name}`);
  } catch (err) {
    failCount++;
    console.log(`  FAIL  ${name}`);
    console.log(`        ${err.message}`);
  }
}

function setup() {
  const graph = new SocialGraph();
  const postStore = new PostStore();
  const ops = new OpsCounter();
  const fow = new FanOutOnWriteFeedBuilder(graph, ops);
  function publish(userId, content) {
    const post = postStore.createPost(userId, content);
    fow.onPost(post);
    return post;
  }
  return { graph, postStore, ops, fow, publish };
}

test('post fans out to all current followers', () => {
  const { graph, fow, publish } = setup();
  graph.follow('alice', 'bob');
  graph.follow('carol', 'bob');
  publish('bob', 'hello');
  assert.strictEqual(fow.getFeed('alice').length, 1);
  assert.strictEqual(fow.getFeed('carol').length, 1);
});

test('write op count equals follower count at post time', () => {
  const { graph, ops, publish } = setup();
  graph.follow('alice', 'bob');
  graph.follow('carol', 'bob');
  graph.follow('dave', 'bob');
  publish('bob', 'hello');
  assert.strictEqual(ops.writes, 3);
});

test('a new follow does not retroactively populate the feed with old posts', () => {
  const { graph, fow, publish } = setup();
  publish('bob', 'before alice followed');
  graph.follow('alice', 'bob');
  assert.strictEqual(fow.getFeed('alice').length, 0);
  publish('bob', 'after alice followed');
  assert.strictEqual(fow.getFeed('alice').length, 1);
  assert.strictEqual(fow.getFeed('alice')[0].content, 'after alice followed');
});

test('unfollow stops future posts from being pushed into the feed', () => {
  const { graph, fow, publish } = setup();
  graph.follow('alice', 'bob');
  publish('bob', 'while following');
  graph.unfollow('alice', 'bob');
  publish('bob', 'after unfollow');
  const feed = fow.getFeed('alice');
  assert.strictEqual(feed.length, 1);
  assert.strictEqual(feed[0].content, 'while following');
});

test('feed is ordered newest first', () => {
  const { graph, fow, publish } = setup();
  graph.follow('alice', 'bob');
  publish('bob', 'first');
  publish('bob', 'second');
  publish('bob', 'third');
  const feed = fow.getFeed('alice');
  assert.deepStrictEqual(feed.map((p) => p.content), ['third', 'second', 'first']);
});

test('getFeed respects a limit', () => {
  const { graph, fow, publish } = setup();
  graph.follow('alice', 'bob');
  publish('bob', 'first');
  publish('bob', 'second');
  publish('bob', 'third');
  assert.strictEqual(fow.getFeed('alice', 2).length, 2);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
