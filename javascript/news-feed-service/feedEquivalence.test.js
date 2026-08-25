'use strict';

const assert = require('assert');
const { SocialGraph } = require('./graph.js');
const { PostStore } = require('./postStore.js');
const { OpsCounter } = require('./opsCounter.js');
const { FanOutOnWriteFeedBuilder } = require('./fanOutOnWrite.js');
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

function buildScenario() {
  const graph = new SocialGraph();
  const postStore = new PostStore();
  const fow = new FanOutOnWriteFeedBuilder(graph, new OpsCounter());
  const forB = new FanOutOnReadFeedBuilder(graph, postStore, new OpsCounter());

  function publish(userId, content) {
    const post = postStore.createPost(userId, content);
    fow.onPost(post);
    forB.onPost(post);
    return post;
  }

  graph.follow('alice', 'bob');
  graph.follow('alice', 'carol');
  graph.follow('dave', 'carol');
  graph.follow('dave', 'bob');

  publish('bob', 'bob post 1');
  publish('carol', 'carol post 1');
  publish('bob', 'bob post 2');
  publish('carol', 'carol post 2');
  publish('bob', 'bob post 3');

  return { graph, postStore, fow, forB };
}

test('both strategies produce identical feed content and order for alice', () => {
  const { fow, forB } = buildScenario();
  const fowFeed = fow.getFeed('alice');
  const forFeed = forB.getFeed('alice');
  assert.deepStrictEqual(
    fowFeed.map((p) => ({ userId: p.userId, content: p.content, timestamp: p.timestamp })),
    forFeed.map((p) => ({ userId: p.userId, content: p.content, timestamp: p.timestamp })),
  );
});

test('both strategies produce identical feed content and order for dave', () => {
  const { fow, forB } = buildScenario();
  const fowFeed = fow.getFeed('dave');
  const forFeed = forB.getFeed('dave');
  assert.deepStrictEqual(
    fowFeed.map((p) => p.content),
    forFeed.map((p) => p.content),
  );
  assert.deepStrictEqual(
    fowFeed.map((p) => p.content),
    ['bob post 3', 'carol post 2', 'bob post 2', 'carol post 1', 'bob post 1'],
  );
});

test('both strategies agree there is nothing for a user with no followees', () => {
  const { fow, forB } = buildScenario();
  assert.deepStrictEqual(fow.getFeed('bob'), []);
  assert.deepStrictEqual(forB.getFeed('bob'), []);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
