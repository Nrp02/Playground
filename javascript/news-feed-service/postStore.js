'use strict';

class PostStore {
  constructor() {
    this.postsByUser = new Map();
    this.nextId = 1;
    this.clock = 0;
  }

  createPost(userId, content) {
    if (!this.postsByUser.has(userId)) this.postsByUser.set(userId, []);
    this.clock += 1;
    const post = {
      id: this.nextId++,
      userId,
      content,
      timestamp: this.clock,
    };
    this.postsByUser.get(userId).unshift(post);
    return post;
  }

  getPostsByUser(userId) {
    return this.postsByUser.get(userId) || [];
  }
}

module.exports = { PostStore };
