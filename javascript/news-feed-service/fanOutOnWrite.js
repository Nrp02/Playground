'use strict';

class FanOutOnWriteFeedBuilder {
  constructor(graph, opsCounter) {
    this.graph = graph;
    this.opsCounter = opsCounter;
    this.feeds = new Map();
  }

  onPost(post) {
    const followers = this.graph.getFollowers(post.userId);
    for (const followerId of followers) {
      if (!this.feeds.has(followerId)) this.feeds.set(followerId, []);
      this.feeds.get(followerId).unshift(post);
      this.opsCounter.writes += 1;
    }
  }

  getFeed(userId, limit = Infinity) {
    const feed = this.feeds.get(userId) || [];
    return feed.slice(0, limit);
  }
}

module.exports = { FanOutOnWriteFeedBuilder };
