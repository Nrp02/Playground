'use strict';

const { SocialGraph } = require('./graph.js');
const { PostStore } = require('./postStore.js');
const { OpsCounter } = require('./opsCounter.js');
const { FanOutOnWriteFeedBuilder } = require('./fanOutOnWrite.js');
const { FanOutOnReadFeedBuilder } = require('./fanOutOnRead.js');
const { HybridFeedBuilder } = require('./hybridFeed.js');

function formatFeed(feed) {
  return feed.map((post) => `[${post.timestamp}] ${post.userId}: ${post.content}`);
}

const graph = new SocialGraph();
const postStore = new PostStore();
const writeOps = new OpsCounter();
const readOps = new OpsCounter();

const fow = new FanOutOnWriteFeedBuilder(graph, writeOps);
const forB = new FanOutOnReadFeedBuilder(graph, postStore, readOps);

function publish(userId, content) {
  const post = postStore.createPost(userId, content);
  fow.onPost(post);
  forB.onPost(post);
  return post;
}

const followers = ['alice', 'bob', 'carol', 'dave', 'erin'];
for (const follower of followers) {
  graph.follow(follower, 'celeb');
}
graph.follow('alice', 'bob');
graph.follow('carol', 'dave');

publish('celeb', 'hello from celeb, first post');
publish('bob', 'bob checking in');
publish('dave', 'dave says hi');
publish('celeb', 'celeb second post');

console.log('=== Fan-out-on-write feed for alice ===');
console.log(formatFeed(fow.getFeed('alice')).join('\n'));

console.log('\n=== Fan-out-on-read feed for alice ===');
console.log(formatFeed(forB.getFeed('alice')).join('\n'));

console.log(`\nFeeds identical: ${JSON.stringify(fow.getFeed('alice')) === JSON.stringify(forB.getFeed('alice'))}`);

console.log('\n=== Op counts after this sequence ===');
console.log(`fan-out-on-write: ${writeOps.writes} write-ops, ${writeOps.readMerges} read-merge-ops`);
console.log(`fan-out-on-read : ${readOps.writes} write-ops, ${readOps.readMerges} read-merge-ops (per getFeed call above)`);

console.log('\n=== Hybrid strategy (celebrity threshold = 3) ===');
const hybridOps = new OpsCounter();
const hybridGraph = new SocialGraph();
const hybridPostStore = new PostStore();
for (const follower of followers) {
  hybridGraph.follow(follower, 'celeb');
}
hybridGraph.follow('alice', 'bob');
const hybrid = new HybridFeedBuilder(hybridGraph, hybridPostStore, hybridOps, 3);

function hybridPublish(userId, content) {
  const post = hybridPostStore.createPost(userId, content);
  hybrid.onPost(post);
  return post;
}

hybridPublish('celeb', 'celeb post seen by many, no fan-out write');
hybridPublish('bob', 'bob post, small follower count, fans out normally');

console.log('alice feed via hybrid:');
console.log(formatFeed(hybrid.getFeed('alice')).join('\n'));
console.log(`hybrid write-ops so far: ${hybridOps.writes} (celeb post caused no writes, bob's post caused 1)`);
console.log(`hybrid read-merge-ops so far: ${hybridOps.readMerges} (from fetching celeb's posts on demand at read time)`);
