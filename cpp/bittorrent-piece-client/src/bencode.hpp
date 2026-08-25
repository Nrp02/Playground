#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace bencode {

class BValue;
using BList = std::vector<BValue>;
using BDict = std::map<std::string, BValue>;

class BValue {
public:
    enum class Type { Integer, String, List, Dict };

    BValue() : type_(Type::Integer), integer_(0) {}
    static BValue makeInt(int64_t value) {
        BValue v;
        v.type_ = Type::Integer;
        v.integer_ = value;
        return v;
    }
    static BValue makeString(std::string value) {
        BValue v;
        v.type_ = Type::String;
        v.string_ = std::move(value);
        return v;
    }
    static BValue makeList(BList value) {
        BValue v;
        v.type_ = Type::List;
        v.list_ = std::move(value);
        return v;
    }
    static BValue makeDict(BDict value) {
        BValue v;
        v.type_ = Type::Dict;
        v.dict_ = std::move(value);
        return v;
    }

    Type type() const { return type_; }
    int64_t asInt() const {
        if (type_ != Type::Integer) throw std::runtime_error("not an integer");
        return integer_;
    }
    const std::string& asString() const {
        if (type_ != Type::String) throw std::runtime_error("not a string");
        return string_;
    }
    const BList& asList() const {
        if (type_ != Type::List) throw std::runtime_error("not a list");
        return list_;
    }
    const BDict& asDict() const {
        if (type_ != Type::Dict) throw std::runtime_error("not a dict");
        return dict_;
    }

    std::string encode() const {
        std::string out;
        encodeInto(out);
        return out;
    }

private:
    void encodeInto(std::string& out) const {
        switch (type_) {
            case Type::Integer:
                out += 'i';
                out += std::to_string(integer_);
                out += 'e';
                break;
            case Type::String:
                out += std::to_string(string_.size());
                out += ':';
                out += string_;
                break;
            case Type::List:
                out += 'l';
                for (const BValue& item : list_) {
                    item.encodeInto(out);
                }
                out += 'e';
                break;
            case Type::Dict:
                out += 'd';
                for (const auto& [key, value] : dict_) {
                    BValue::makeString(key).encodeInto(out);
                    value.encodeInto(out);
                }
                out += 'e';
                break;
        }
    }

    Type type_;
    int64_t integer_ = 0;
    std::string string_;
    BList list_;
    BDict dict_;
};

class Decoder {
public:
    explicit Decoder(const std::string& data) : data_(data), pos_(0) {}

    BValue decode() {
        BValue value = decodeValue();
        if (pos_ != data_.size()) {
            throw std::runtime_error("trailing data after bencode value");
        }
        return value;
    }

private:
    BValue decodeValue() {
        if (pos_ >= data_.size()) throw std::runtime_error("unexpected end of bencode data");
        char c = data_[pos_];
        if (c == 'i') return decodeInt();
        if (c == 'l') return decodeList();
        if (c == 'd') return decodeDict();
        if (c >= '0' && c <= '9') return decodeString();
        throw std::runtime_error("invalid bencode tag");
    }

    BValue decodeInt() {
        expect('i');
        std::size_t end = data_.find('e', pos_);
        if (end == std::string::npos) throw std::runtime_error("unterminated integer");
        int64_t value = std::stoll(data_.substr(pos_, end - pos_));
        pos_ = end + 1;
        return BValue::makeInt(value);
    }

    BValue decodeString() {
        std::size_t colon = data_.find(':', pos_);
        if (colon == std::string::npos) throw std::runtime_error("unterminated string length");
        std::size_t len = static_cast<std::size_t>(std::stoull(data_.substr(pos_, colon - pos_)));
        pos_ = colon + 1;
        if (pos_ + len > data_.size()) throw std::runtime_error("string length out of bounds");
        std::string value = data_.substr(pos_, len);
        pos_ += len;
        return BValue::makeString(value);
    }

    BValue decodeList() {
        expect('l');
        BList items;
        while (pos_ < data_.size() && data_[pos_] != 'e') {
            items.push_back(decodeValue());
        }
        expect('e');
        return BValue::makeList(std::move(items));
    }

    BValue decodeDict() {
        expect('d');
        BDict items;
        while (pos_ < data_.size() && data_[pos_] != 'e') {
            BValue key = decodeString();
            BValue value = decodeValue();
            items.emplace(key.asString(), std::move(value));
        }
        expect('e');
        return BValue::makeDict(std::move(items));
    }

    void expect(char c) {
        if (pos_ >= data_.size() || data_[pos_] != c) {
            throw std::runtime_error("bencode parse error: expected token");
        }
        ++pos_;
    }

    const std::string& data_;
    std::size_t pos_;
};

inline BValue decode(const std::string& data) {
    Decoder decoder(data);
    return decoder.decode();
}

}
