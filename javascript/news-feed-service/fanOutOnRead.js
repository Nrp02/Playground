'use strict';

class FanOutOnReadFeedBuilder {
  constructor(graph, postStore, opsCounter) {
    this.graph = graph;
    this.postStore = postStore;
    this.opsCounter = opsCounter;
  }

  onPost() {}

  getFeed(userId, limit = Infinity) {
    const followees = this.graph.getFollowees(userId);
    let merged = [];
    for (const followeeId of followees) {
      this.opsCounter.readMerges += 1;
      merged = merged.concat(this.postStore.getPostsByUser(followeeId));
    }
    merged.sort((a, b) => b.timestamp - a.timestamp);
    return merged.slice(0, limit);
  }
}

module.exports = { FanOutOnReadFeedBuilder };
