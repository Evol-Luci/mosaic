#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mosaic::foundation::serialization
{

class Value;

using Array = std::vector<Value>;

// Insertion-ordered string->Value map. Equality is order-independent (same
// keys mapping to equal values) so cross-format round-trips that reorder table
// keys still compare equal, while iteration stays stable for readable output.
class Table
{
public:
    Value& operator[](const std::string& key);
    const Value* find(const std::string& key) const;

    std::vector<std::pair<std::string, Value>>::const_iterator begin() const { return entries_.begin(); }
    std::vector<std::pair<std::string, Value>>::const_iterator end() const { return entries_.end(); }
    std::size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

    bool operator==(const Table& other) const;

private:
    std::vector<std::pair<std::string, Value>> entries_;
};

class Value
{
public:
    using Storage = std::variant<std::monostate, bool, std::int64_t, std::uint64_t,
                                 double, std::string, Array, Table>;

    Value() = default; // Null
    Value(bool b) : data_(b) {}
    Value(std::int64_t i) : data_(i) {}
    Value(std::uint64_t u) : data_(u) {}
    Value(double d) : data_(d) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(const char* s) : data_(std::string(s)) {}
    Value(Array a) : data_(std::move(a)) {}
    Value(Table t) : data_(std::move(t)) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(data_); }
    template <class T> bool is() const { return std::holds_alternative<T>(data_); }
    template <class T> const T& as() const { return std::get<T>(data_); }
    template <class T> T& as() { return std::get<T>(data_); }

    const Storage& storage() const { return data_; }
    bool operator==(const Value& other) const { return data_ == other.data_; }

private:
    Storage data_;
};

inline Value& Table::operator[](const std::string& key)
{
    for (auto& entry : entries_)
    {
        if (entry.first == key)
        {
            return entry.second;
        }
    }
    entries_.emplace_back(key, Value{});
    return entries_.back().second;
}

inline const Value* Table::find(const std::string& key) const
{
    for (const auto& entry : entries_)
    {
        if (entry.first == key)
        {
            return &entry.second;
        }
    }
    return nullptr;
}

inline bool Table::operator==(const Table& other) const
{
    if (entries_.size() != other.entries_.size())
    {
        return false;
    }
    for (const auto& entry : entries_)
    {
        const Value* o = other.find(entry.first);
        if (o == nullptr || !(*o == entry.second))
        {
            return false;
        }
    }
    return true;
}

} // namespace mosaic::foundation::serialization
