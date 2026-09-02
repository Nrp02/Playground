'use strict';

const { CellError, isError, DIV0, VALUE, NAME, NUM } = require('./errors.js');
const { isRange, toNumber, toText, toBoolean } = require('./values.js');

const registry = new Map();

function define(name, spec) {
  registry.set(name, spec);
}

function isDefined(name) {
  return registry.has(String(name).toUpperCase());
}

function functionNames() {
  return Array.from(registry.keys()).sort();
}

function flatten(args) {
  const out = [];
  for (const arg of args) {
    if (isRange(arg)) {
      for (const value of arg.values) out.push(value);
    } else {
      out.push(arg);
    }
  }
  return out;
}

function collectNumbers(args) {
  const numbers = [];
  for (const value of flatten(args)) {
    if (isError(value)) return { error: value, numbers };
    if (value === null || value === undefined) continue;
    if (typeof value === 'string') continue;
    const num = toNumber(value);
    if (isError(num)) return { error: num, numbers };
    numbers.push(num);
  }
  return { error: null, numbers };
}

function collectBooleans(args) {
  const booleans = [];
  for (const value of flatten(args)) {
    if (isError(value)) return { error: value, booleans };
    if (value === null || value === undefined) continue;
    const flag = toBoolean(value);
    if (isError(flag)) return { error: flag, booleans };
    booleans.push(flag);
  }
  return { error: null, booleans };
}

function scalar(value) {
  if (isRange(value)) {
    if (value.values.length === 1) return value.values[0];
    return new CellError(VALUE);
  }
  return value;
}

function roundHalfAwayFromZero(value, digits) {
  const factor = Math.pow(10, digits);
  const scaled = Number((value * factor).toPrecision(15));
  const rounded = scaled >= 0 ? Math.round(scaled) : -Math.round(-scaled);
  return rounded / factor;
}

define('SUM', {
  minArgs: 1,
  maxArgs: Infinity,
  apply(args) {
    const collected = collectNumbers(args);
    if (collected.error) return collected.error;
    return collected.numbers.reduce((acc, n) => acc + n, 0);
  },
});

define('AVERAGE', {
  minArgs: 1,
  maxArgs: Infinity,
  apply(args) {
    const collected = collectNumbers(args);
    if (collected.error) return collected.error;
    if (collected.numbers.length === 0) return new CellError(DIV0);
    return collected.numbers.reduce((acc, n) => acc + n, 0) / collected.numbers.length;
  },
});

define('MIN', {
  minArgs: 1,
  maxArgs: Infinity,
  apply(args) {
    const collected = collectNumbers(args);
    if (collected.error) return collected.error;
    if (collected.numbers.length === 0) return 0;
    return collected.numbers.reduce((acc, n) => (n < acc ? n : acc));
  },
});

define('MAX', {
  minArgs: 1,
  maxArgs: Infinity,
  apply(args) {
    const collected = collectNumbers(args);
    if (collected.error) return collected.error;
    if (collected.numbers.length === 0) return 0;
    return collected.numbers.reduce((acc, n) => (n > acc ? n : acc));
  },
});

define('COUNT', {
  minArgs: 1,
  maxArgs: Infinity,
  apply(args) {
    let count = 0;
    for (const value of flatten(args)) {
      if (isError(value)) return value;
      if (typeof value === 'number') count++;
    }
    return count;
  },
});

define('COUNTA', {
  minArgs: 1,
  maxArgs: Infinity,
  apply(args) {
    let count = 0;
    for (const value of flatten(args)) {
      if (isError(value)) return value;
      if (value !== null && value !== undefined) count++;
    }
    return count;
  },
});

define('IF', {
  minArgs: 2,
  maxArgs: 3,
  lazy: true,
  apply(nodes, ctx, evaluateNode) {
    const condition = toBoolean(scalar(evaluateNode(nodes[0], ctx)));
    if (isError(condition)) return condition;
    if (condition) return scalar(evaluateNode(nodes[1], ctx));
    if (nodes.length < 3) return false;
    return scalar(evaluateNode(nodes[2], ctx));
  },
});

define('AND', {
  minArgs: 1,
  maxArgs: Infinity,
  apply(args) {
    const collected = collectBooleans(args);
    if (collected.error) return collected.error;
    if (collected.booleans.length === 0) return new CellError(VALUE);
    return collected.booleans.every((flag) => flag);
  },
});

define('OR', {
  minArgs: 1,
  maxArgs: Infinity,
  apply(args) {
    const collected = collectBooleans(args);
    if (collected.error) return collected.error;
    if (collected.booleans.length === 0) return new CellError(VALUE);
    return collected.booleans.some((flag) => flag);
  },
});

define('NOT', {
  minArgs: 1,
  maxArgs: 1,
  apply(args) {
    const flag = toBoolean(scalar(args[0]));
    if (isError(flag)) return flag;
    return !flag;
  },
});

define('ABS', {
  minArgs: 1,
  maxArgs: 1,
  apply(args) {
    const num = toNumber(scalar(args[0]));
    if (isError(num)) return num;
    return Math.abs(num);
  },
});

define('ROUND', {
  minArgs: 1,
  maxArgs: 2,
  apply(args) {
    const num = toNumber(scalar(args[0]));
    if (isError(num)) return num;
    const digits = args.length > 1 ? toNumber(scalar(args[1])) : 0;
    if (isError(digits)) return digits;
    return roundHalfAwayFromZero(num, Math.trunc(digits));
  },
});

define('SQRT', {
  minArgs: 1,
  maxArgs: 1,
  apply(args) {
    const num = toNumber(scalar(args[0]));
    if (isError(num)) return num;
    if (num < 0) return new CellError(NUM);
    return Math.sqrt(num);
  },
});

define('LEN', {
  minArgs: 1,
  maxArgs: 1,
  apply(args) {
    const text = toText(scalar(args[0]));
    if (isError(text)) return text;
    return text.length;
  },
});

define('CONCAT', {
  minArgs: 1,
  maxArgs: Infinity,
  apply(args) {
    let out = '';
    for (const value of flatten(args)) {
      if (isError(value)) return value;
      const text = toText(value);
      if (isError(text)) return text;
      out += text;
    }
    return out;
  },
});

function callFunction(name, argNodes, ctx, evaluateNode) {
  const spec = registry.get(name);
  if (!spec) return new CellError(NAME);
  if (argNodes.length < spec.minArgs || argNodes.length > spec.maxArgs) {
    return new CellError(VALUE);
  }
  if (spec.lazy) {
    return spec.apply(argNodes, ctx, evaluateNode);
  }
  const args = [];
  for (const node of argNodes) {
    const value = evaluateNode(node, ctx);
    if (isError(value)) return value;
    args.push(value);
  }
  return spec.apply(args, ctx);
}

module.exports = {
  define,
  isDefined,
  functionNames,
  callFunction,
  flatten,
  collectNumbers,
  registry,
};
