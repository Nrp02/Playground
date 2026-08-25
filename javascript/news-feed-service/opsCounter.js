'use strict';

class OpsCounter {
  constructor() {
    this.writes = 0;
    this.readMerges = 0;
  }

  reset() {
    this.writes = 0;
    this.readMerges = 0;
  }
}

module.exports = { OpsCounter };
