'use strict';

const assert = require('assert');
const { SocialGraph } = require('./graph.js');
const { PostStore } = require('./postStore.js');
const { OpsCounter } = require('./opsCounter.js');
const { FanOutOnReadFeedBuilder } = require('./fanOutOnRead.js');

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
  const forB = new FanOutOnReadFeedBuilder(graph, postStore, ops);
  function publish(userId, content) {
    const post = postStore.createPost(userId, content);
    forB.onPost(post);
    return post;
  }
  return { graph, postStore, ops, forB, publish };
}

test('post causes zero write ops at post time', () => {
  const { graph, ops, publish } = setup();
  graph.follow('alice', 'bob');
  graph.follow('carol', 'bob');
  publish('bob', 'hello');
  assert.strictEqual(ops.writes, 0);
});

test('getFeed merges posts from all current followees', () => {
  const { graph, forB, publish } = setup();
  graph.follow('alice', 'bob');
  graph.follow('alice', 'carol');
  publish('bob', 'from bob');
  publish('carol', 'from carol');
  const feed = forB.getFeed('alice');
  assert.strictEqual(feed.length, 2);
});

test('read merge op count equals number of followees processed', () => {
  const { graph, ops, forB } = setup();
  graph.follow('alice', 'bob');
  graph.follow('alice', 'carol');
  graph.follow('alice', 'dave');
  forB.getFeed('alice');
  assert.strictEqual(ops.readMerges, 3);
});

test('feed is ordered newest first', () => {
  const { graph, forB, publish } = setup();
  graph.follow('alice', 'bob');
  graph.follow('alice', 'carol');
  publish('bob', 'first');
  publish('carol', 'second');
  publish('bob', 'third');
  const feed = forB.getFeed('alice');
  assert.deepStrictEqual(feed.map((p) => p.content), ['third', 'second', 'first']);
});

test('getFeed respects a limit', () => {
  const { graph, forB, publish } = setup();
  graph.follow('alice', 'bob');
  publish('bob', 'first');
  publish('bob', 'second');
  publish('bob', 'third');
  assert.strictEqual(forB.getFeed('alice', 2).length, 2);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
