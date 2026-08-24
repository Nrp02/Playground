#include "vm.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

Value Value::nil() { return Value{}; }

Value Value::boolean_(bool b) {
    Value v;
    v.type = ValueType::Bool;
    v.boolean = b;
    return v;
}

Value Value::number_(double n) {
    Value v;
    v.type = ValueType::Number;
    v.number = n;
    return v;
}

Value Value::string_(std::string s) {
    Value v;
    v.type = ValueType::String;
    v.str = std::make_shared<std::string>(std::move(s));
    return v;
}

Value Value::function_(std::shared_ptr<ObjFunction> f) {
    Value v;
    v.type = ValueType::Function;
    v.function = std::move(f);
    return v;
}

bool Value::isFalsey() const {
    if (type == ValueType::Nil) return true;
    if (type == ValueType::Bool) return !boolean;
    return false;
}

std::string Value::toString() const {
    switch (type) {
        case ValueType::Nil: return "nil";
        case ValueType::Bool: return boolean ? "true" : "false";
        case ValueType::Number: {
            std::ostringstream oss;
            if (number == static_cast<long long>(number)) {
                oss << static_cast<long long>(number);
            } else {
                oss << number;
            }
            return oss.str();
        }
        case ValueType::String: return *str;
        case ValueType::Function: return "<fn " + function->name + ">";
    }
    return "";
}

bool valuesEqual(const Value& a, const Value& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case ValueType::Nil: return true;
        case ValueType::Bool: return a.boolean == b.boolean;
        case ValueType::Number: return a.number == b.number;
        case ValueType::String: return *a.str == *b.str;
        case ValueType::Function: return a.function == b.function;
    }
    return false;
}

size_t Chunk::writeOp(OpCode op, int line) { return writeByte(static_cast<uint8_t>(op), line); }

size_t Chunk::writeByte(uint8_t byte, int line) {
    code.push_back(byte);
    lines.push_back(line);
    return code.size() - 1;
}

size_t Chunk::addConstant(const Value& value) {
    constants.push_back(value);
    return constants.size() - 1;
}

uint8_t VM::readByte(CallFrame& frame) { return frame.function->chunk.code[frame.ip++]; }

uint16_t VM::readShort(CallFrame& frame) {
    uint8_t hi = readByte(frame);
    uint8_t lo = readByte(frame);
    return static_cast<uint16_t>((hi << 8) | lo);
}

Value VM::readConstant(CallFrame& frame) { return frame.function->chunk.constants[readByte(frame)]; }

void VM::runtimeError(const std::string& message) {
    int line = frames_.empty() ? 0 : frames_.back().function->chunk.lines[frames_.back().ip - 1];
    throw std::runtime_error("Runtime error at line " + std::to_string(line) + ": " + message);
}

void VM::run(std::shared_ptr<ObjFunction> script) {
    stack_.push_back(Value::function_(script));
    CallFrame frame;
    frame.function = script;
    frame.slotsBase = 0;
    frames_.push_back(frame);
    execute();
}

void VM::execute() {
    for (;;) {
        CallFrame& frame = frames_.back();
        OpCode instruction = static_cast<OpCode>(readByte(frame));
        switch (instruction) {
            case OpCode::Constant: stack_.push_back(readConstant(frame)); break;
            case OpCode::Nil: stack_.push_back(Value::nil()); break;
            case OpCode::True: stack_.push_back(Value::boolean_(true)); break;
            case OpCode::False: stack_.push_back(Value::boolean_(false)); break;
            case OpCode::Pop: stack_.pop_back(); break;
            case OpCode::GetLocal: {
                uint8_t slot = readByte(frame);
                stack_.push_back(stack_[frame.slotsBase + slot]);
                break;
            }
            case OpCode::SetLocal: {
                uint8_t slot = readByte(frame);
                stack_[frame.slotsBase + slot] = stack_.back();
                break;
            }
            case OpCode::GetGlobal: {
                Value name = readConstant(frame);
                auto it = globals_.find(*name.str);
                if (it == globals_.end()) runtimeError("Undefined variable '" + *name.str + "'.");
                stack_.push_back(it->second);
                break;
            }
            case OpCode::DefineGlobal: {
                Value name = readConstant(frame);
                globals_[*name.str] = stack_.back();
                stack_.pop_back();
                break;
            }
            case OpCode::SetGlobal: {
                Value name = readConstant(frame);
                if (globals_.find(*name.str) == globals_.end()) runtimeError("Undefined variable '" + *name.str + "'.");
                globals_[*name.str] = stack_.back();
                break;
            }
            case OpCode::Equal: {
                Value b = stack_.back(); stack_.pop_back();
                Value a = stack_.back(); stack_.pop_back();
                stack_.push_back(Value::boolean_(valuesEqual(a, b)));
                break;
            }
            case OpCode::Greater:
            case OpCode::Less: {
                Value b = stack_.back(); stack_.pop_back();
                Value a = stack_.back(); stack_.pop_back();
                if (a.type != ValueType::Number || b.type != ValueType::Number) runtimeError("Operands must be numbers.");
                bool result = instruction == OpCode::Greater ? a.number > b.number : a.number < b.number;
                stack_.push_back(Value::boolean_(result));
                break;
            }
            case OpCode::Add: {
                Value b = stack_.back(); stack_.pop_back();
                Value a = stack_.back(); stack_.pop_back();
                if (a.type == ValueType::Number && b.type == ValueType::Number) {
                    stack_.push_back(Value::number_(a.number + b.number));
                } else if (a.type == ValueType::String && b.type == ValueType::String) {
                    stack_.push_back(Value::string_(*a.str + *b.str));
                } else {
                    runtimeError("Operands must be two numbers or two strings.");
                }
                break;
            }
            case OpCode::Sub:
            case OpCode::Mul:
            case OpCode::Div: {
                Value b = stack_.back(); stack_.pop_back();
                Value a = stack_.back(); stack_.pop_back();
                if (a.type != ValueType::Number || b.type != ValueType::Number) runtimeError("Operands must be numbers.");
                double result = 0;
                if (instruction == OpCode::Sub) result = a.number - b.number;
                else if (instruction == OpCode::Mul) result = a.number * b.number;
                else {
                    if (b.number == 0) runtimeError("Division by zero.");
                    result = a.number / b.number;
                }
                stack_.push_back(Value::number_(result));
                break;
            }
            case OpCode::Not: {
                Value a = stack_.back(); stack_.pop_back();
                stack_.push_back(Value::boolean_(a.isFalsey()));
                break;
            }
            case OpCode::Negate: {
                Value a = stack_.back(); stack_.pop_back();
                if (a.type != ValueType::Number) runtimeError("Operand must be a number.");
                stack_.push_back(Value::number_(-a.number));
                break;
            }
            case OpCode::Print: {
                std::cout << stack_.back().toString() << "\n";
                stack_.pop_back();
                break;
            }
            case OpCode::Jump: {
                uint16_t offset = readShort(frame);
                frame.ip += offset;
                break;
            }
            case OpCode::JumpIfFalse: {
                uint16_t offset = readShort(frame);
                if (stack_.back().isFalsey()) frame.ip += offset;
                break;
            }
            case OpCode::Loop: {
                uint16_t offset = readShort(frame);
                frame.ip -= offset;
                break;
            }
            case OpCode::Call: {
                uint8_t argCount = readByte(frame);
                Value callee = stack_[stack_.size() - 1 - argCount];
                if (callee.type != ValueType::Function) runtimeError("Can only call functions.");
                if (callee.function->arity != argCount) {
                    runtimeError("Expected " + std::to_string(callee.function->arity) + " arguments but got " + std::to_string(argCount) + ".");
                }
                CallFrame newFrame;
                newFrame.function = callee.function;
                newFrame.slotsBase = stack_.size() - argCount - 1;
                frames_.push_back(newFrame);
                break;
            }
            case OpCode::Return: {
                Value result = stack_.back(); stack_.pop_back();
                size_t base = frame.slotsBase;
                frames_.pop_back();
                stack_.resize(base);
                stack_.push_back(result);
                if (frames_.empty()) return;
                break;
            }
        }
    }
}
