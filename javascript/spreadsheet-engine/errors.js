'use strict';

const DIV0 = '#DIV/0!';
const REF = '#REF!';
const VALUE = '#VALUE!';
const NAME = '#NAME?';
const NUM = '#NUM!';
const CIRC = '#CIRC!';

const ERROR_CODES = [DIV0, REF, VALUE, NAME, NUM, CIRC];

class CellError {
  constructor(code) {
    this.code = code;
  }

  toString() {
    return this.code;
  }
}

class FormulaSyntaxError extends Error {
  constructor(message, position) {
    super(message);
    this.name = 'FormulaSyntaxError';
    this.position = position;
  }
}

function isError(value) {
  return value instanceof CellError;
}

function firstError() {
  for (let i = 0; i < arguments.length; i++) {
    if (isError(arguments[i])) return arguments[i];
  }
  return null;
}

module.exports = {
  CellError,
  FormulaSyntaxError,
  isError,
  firstError,
  ERROR_CODES,
  DIV0,
  REF,
  VALUE,
  NAME,
  NUM,
  CIRC,
};
