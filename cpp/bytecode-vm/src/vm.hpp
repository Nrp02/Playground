#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class OpCode : uint8_t {
    Constant, Nil, True, False, Pop,
    GetLocal, SetLocal, GetGlobal, DefineGlobal, SetGlobal,
    Equal, Greater, Less,
    Add, Sub, Mul, Div,
    Not, Negate,
    Print,
    Jump, JumpIfFalse, Loop,
    Call, Return,
};

struct ObjFunction;

enum class ValueType { Nil, Bool, Number, String, Function };

struct Value {
    ValueType type = ValueType::Nil;
    bool boolean = false;
    double number = 0;
    std::shared_ptr<std::string> str;
    std::shared_ptr<ObjFunction> function;

    static Value nil();
    static Value boolean_(bool b);
    static Value number_(double n);
    static Value string_(std::string s);
    static Value function_(std::shared_ptr<ObjFunction> f);

    bool isFalsey() const;
    std::string toString() const;
};

bool valuesEqual(const Value& a, const Value& b);

struct Chunk {
    std::vector<uint8_t> code;
    std::vector<int> lines;
    std::vector<Value> constants;

    size_t writeOp(OpCode op, int line);
    size_t writeByte(uint8_t byte, int line);
    size_t addConstant(const Value& value);
};

struct ObjFunction {
    std::string name;
    int arity = 0;
    Chunk chunk;
};

class VM {
public:
    void run(std::shared_ptr<ObjFunction> script);

private:
    struct CallFrame {
        std::shared_ptr<ObjFunction> function;
        size_t ip = 0;
        size_t slotsBase = 0;
    };

    std::vector<Value> stack_;
    std::vector<CallFrame> frames_;
    std::unordered_map<std::string, Value> globals_;

    uint8_t readByte(CallFrame& frame);
    uint16_t readShort(CallFrame& frame);
    Value readConstant(CallFrame& frame);
    [[noreturn]] void runtimeError(const std::string& message);
    void execute();
};
