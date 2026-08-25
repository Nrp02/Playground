'use strict';

const assert = require('assert');
const { SocialGraph } = require('./graph.js');

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

test('follow adds a follower relationship both directions', () => {
  const graph = new SocialGraph();
  graph.follow('alice', 'bob');
  assert.deepStrictEqual(graph.getFollowers('bob'), ['alice']);
  assert.deepStrictEqual(graph.getFollowees('alice'), ['bob']);
});

test('unfollow removes a follower relationship both directions', () => {
  const graph = new SocialGraph();
  graph.follow('alice', 'bob');
  graph.unfollow('alice', 'bob');
  assert.deepStrictEqual(graph.getFollowers('bob'), []);
  assert.deepStrictEqual(graph.getFollowees('alice'), []);
});

test('followerCount reflects current followers', () => {
  const graph = new SocialGraph();
  graph.follow('alice', 'celeb');
  graph.follow('bob', 'celeb');
  assert.strictEqual(graph.followerCount('celeb'), 2);
  graph.unfollow('alice', 'celeb');
  assert.strictEqual(graph.followerCount('celeb'), 1);
});

test('a user cannot follow themselves', () => {
  const graph = new SocialGraph();
  assert.throws(() => graph.follow('alice', 'alice'), RangeError);
});

test('unknown user has empty followers and followees', () => {
  const graph = new SocialGraph();
  assert.deepStrictEqual(graph.getFollowers('nobody'), []);
  assert.deepStrictEqual(graph.getFollowees('nobody'), []);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
