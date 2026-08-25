'use strict';

class SocialGraph {
  constructor() {
    this.followers = new Map();
    this.followees = new Map();
  }

  addUser(userId) {
    if (!this.followers.has(userId)) this.followers.set(userId, new Set());
    if (!this.followees.has(userId)) this.followees.set(userId, new Set());
  }

  follow(followerId, followeeId) {
    if (followerId === followeeId) {
      throw new RangeError('a user cannot follow themselves');
    }
    this.addUser(followerId);
    this.addUser(followeeId);
    this.followers.get(followeeId).add(followerId);
    this.followees.get(followerId).add(followeeId);
  }

  unfollow(followerId, followeeId) {
    this.addUser(followerId);
    this.addUser(followeeId);
    this.followers.get(followeeId).delete(followerId);
    this.followees.get(followerId).delete(followeeId);
  }

  getFollowers(userId) {
    this.addUser(userId);
    return Array.from(this.followers.get(userId));
  }

  getFollowees(userId) {
    this.addUser(userId);
    return Array.from(this.followees.get(userId));
  }

  followerCount(userId) {
    this.addUser(userId);
    return this.followers.get(userId).size;
  }
}

module.exports = { SocialGraph };
