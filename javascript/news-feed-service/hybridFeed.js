'use strict';

class HybridFeedBuilder {
  constructor(graph, postStore, opsCounter, celebrityThreshold) {
    this.graph = graph;
    this.postStore = postStore;
    this.opsCounter = opsCounter;
    this.celebrityThreshold = celebrityThreshold;
    this.feeds = new Map();
  }

  isCelebrity(userId) {
    return this.graph.followerCount(userId) > this.celebrityThreshold;
  }

  onPost(post) {
    if (this.isCelebrity(post.userId)) return;
    const followers = this.graph.getFollowers(post.userId);
    for (const followerId of followers) {
      if (!this.feeds.has(followerId)) this.feeds.set(followerId, []);
      this.feeds.get(followerId).unshift(post);
      this.opsCounter.writes += 1;
    }
  }

  getFeed(userId, limit = Infinity) {
    const precomputed = this.feeds.get(userId) || [];
    const followees = this.graph.getFollowees(userId);
    let celebrityPosts = [];
    for (const followeeId of followees) {
      if (!this.isCelebrity(followeeId)) continue;
      this.opsCounter.readMerges += 1;
      celebrityPosts = celebrityPosts.concat(this.postStore.getPostsByUser(followeeId));
    }
    const merged = precomputed.concat(celebrityPosts);
    const seen = new Set();
    const deduped = [];
    for (const post of merged) {
      if (seen.has(post.id)) continue;
      seen.add(post.id);
      deduped.push(post);
    }
    deduped.sort((a, b) => b.timestamp - a.timestamp);
    return deduped.slice(0, limit);
  }
}

module.exports = { HybridFeedBuilder };
