'use strict';

const State = Object.freeze({
  IDLE: 'idle',
  ELECTION: 'election',
  WAITING_COORDINATOR: 'waiting_coordinator',
});

class BullyNode {
  constructor(id, peerIds, options = {}) {
    this.id = id;
    this.peerIds = peerIds.slice().sort((a, b) => a - b);
    this.higherPeerIds = this.peerIds.filter((peerId) => peerId > id);
    this.alive = true;
    this.leaderId = null;
    this.state = State.IDLE;
    this.electionDeadline = 0;
    this.coordinatorDeadline = 0;
    this.lastHeartbeatReceived = 0;
    this.lastHeartbeatSent = 0;
    this.electionTimeout = options.electionTimeout || 20;
    this.coordinatorTimeout = options.coordinatorTimeout || 25;
    this.heartbeatInterval = options.heartbeatInterval || 5;
    this.heartbeatTimeout = options.heartbeatTimeout || 15;
    this.log = [];
  }

  record(now, text) {
    this.log.push(`[t${now}] node ${this.id}: ${text}`);
  }

  crash() {
    this.alive = false;
  }

  revive(now) {
    this.alive = true;
    this.leaderId = null;
    this.state = State.IDLE;
    this.lastHeartbeatReceived = now;
    this.record(now, 'rejoined the cluster');
  }

  startElection(now, bus) {
    if (!this.alive) return;
    this.state = State.ELECTION;
    this.electionDeadline = now + this.electionTimeout;
    this.record(now, 'starting an election');
    if (this.higherPeerIds.length === 0) {
      this.becomeLeader(now, bus);
      return;
    }
    for (const peerId of this.higherPeerIds) {
      bus.send(this.id, peerId, { type: 'ELECTION', from: this.id }, now);
    }
  }

  becomeLeader(now, bus) {
    this.leaderId = this.id;
    this.state = State.IDLE;
    this.lastHeartbeatSent = now;
    this.record(now, 'became the coordinator');
    for (const peerId of this.peerIds) {
      bus.send(this.id, peerId, { type: 'COORDINATOR', from: this.id }, now);
    }
  }

  handleMessage(now, bus, message) {
    if (!this.alive) return;
    if (message.type === 'ELECTION') {
      bus.send(this.id, message.from, { type: 'OK', from: this.id }, now);
      this.record(now, `answered OK to node ${message.from}`);
      if (this.state !== State.ELECTION && this.state !== State.WAITING_COORDINATOR) {
        this.startElection(now, bus);
      }
      return;
    }
    if (message.type === 'OK') {
      if (this.state === State.ELECTION) {
        this.state = State.WAITING_COORDINATOR;
        this.coordinatorDeadline = now + this.coordinatorTimeout;
        this.record(now, `received OK from node ${message.from}`);
      }
      return;
    }
    if (message.type === 'COORDINATOR') {
      this.leaderId = message.from;
      this.state = State.IDLE;
      this.lastHeartbeatReceived = now;
      this.record(now, `accepted node ${message.from} as coordinator`);
      return;
    }
    if (message.type === 'HEARTBEAT') {
      if (message.from === this.leaderId) {
        this.lastHeartbeatReceived = now;
      }
    }
  }

  tick(now, bus) {
    if (!this.alive) return;
    if (this.state === State.ELECTION && now >= this.electionDeadline) {
      this.record(now, 'election timed out with no answer, becoming coordinator');
      this.becomeLeader(now, bus);
      return;
    }
    if (this.state === State.WAITING_COORDINATOR && now >= this.coordinatorDeadline) {
      this.record(now, 'coordinator announcement timed out, restarting election');
      this.startElection(now, bus);
      return;
    }
    if (this.state !== State.IDLE) return;
    if (this.leaderId === this.id) {
      if (now - this.lastHeartbeatSent >= this.heartbeatInterval) {
        this.lastHeartbeatSent = now;
        for (const peerId of this.peerIds) {
          bus.send(this.id, peerId, { type: 'HEARTBEAT', from: this.id }, now);
        }
      }
      return;
    }
    if (now - this.lastHeartbeatReceived > this.heartbeatTimeout) {
      this.record(now, 'suspects the current leader is down');
      this.startElection(now, bus);
    }
  }
}

module.exports = { BullyNode, State };
