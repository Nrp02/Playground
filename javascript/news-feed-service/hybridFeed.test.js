'use strict';

const assert = require('assert');
const { SocialGraph } = require('./graph.js');
const { PostStore } = require('./postStore.js');
const { OpsCounter } = require('./opsCounter.js');
const { HybridFeedBuilder } = require('./hybridFeed.js');

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

function setup(threshold) {
  const graph = new SocialGraph();
  const postStore = new PostStore();
  const ops = new OpsCounter();
  const hybrid = new HybridFeedBuilder(graph, postStore, ops, threshold);
  function publish(userId, content) {
    const post = postStore.createPost(userId, content);
    hybrid.onPost(post);
    return post;
  }
  return { graph, postStore, ops, hybrid, publish };
}

test('a celebrity post causes no write ops', () => {
  const { graph, ops, publish } = setup(2);
  graph.follow('a', 'celeb');
  graph.follow('b', 'celeb');
  graph.follow('c', 'celeb');
  publish('celeb', 'big announcement');
  assert.strictEqual(ops.writes, 0);
});

test('a regular user post fans out normally below the threshold', () => {
  const { graph, ops, publish } = setup(2);
  graph.follow('a', 'bob');
  publish('bob', 'hi');
  assert.strictEqual(ops.writes, 1);
});

test('a follower still sees the celebrity post via on-demand read', () => {
  const { graph, hybrid, publish } = setup(2);
  graph.follow('a', 'celeb');
  graph.follow('b', 'celeb');
  graph.follow('c', 'celeb');
  publish('celeb', 'big announcement');
  const feed = hybrid.getFeed('a');
  assert.strictEqual(feed.length, 1);
  assert.strictEqual(feed[0].content, 'big announcement');
});

test('reading a celebrity followee increments read-merge ops', () => {
  const { graph, ops, hybrid, publish } = setup(2);
  graph.follow('a', 'celeb');
  graph.follow('b', 'celeb');
  graph.follow('c', 'celeb');
  publish('celeb', 'big announcement');
  ops.reset();
  hybrid.getFeed('a');
  assert.strictEqual(ops.readMerges, 1);
});

test('regular and celebrity posts merge into one recency-ordered feed', () => {
  const { graph, hybrid, publish } = setup(2);
  graph.follow('a', 'celeb');
  graph.follow('a', 'bob');
  graph.follow('b', 'celeb');
  graph.follow('c', 'celeb');
  publish('bob', 'bob post');
  publish('celeb', 'celeb post');
  const feed = hybrid.getFeed('a');
  assert.deepStrictEqual(feed.map((p) => p.content), ['celeb post', 'bob post']);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
