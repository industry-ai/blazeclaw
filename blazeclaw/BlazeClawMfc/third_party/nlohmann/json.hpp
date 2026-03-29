// Minimal nlohmann::json stub for project tests
#pragma once
#include <string>
#include <map>
#include <sstream>
#include <iostream>

namespace nlohmann {
    class json {
    public:
        using object_t = std::map<std::string, json>;
        enum class value_t { null, boolean, number, string, object, array };

        json() : _type(value_t::null) {}
        static json object() { json j; j._type = value_t::object; return j; }

        bool is_object() const { return _type == value_t::object; }
        bool contains(const std::string& k) const { return _obj.find(k) != _obj.end(); }
        void erase(const std::string& k) { _obj.erase(k); }

        static json parse(const std::string& s) {
            // naive parser: if string starts with '{' return empty object; if starts with '"' return string
            json j;
            if (!s.empty() && s.front() == '{') { j._type = value_t::object; return j; }
            if (!s.empty() && s.front() == '"') { j._type = value_t::string; j._str = s.substr(1, s.size()-2); return j; }
            j._type = value_t::string; j._str = s; return j;
        }

        std::string dump(int = -1) const {
            if (_type == value_t::object) return "{}";
            if (_type == value_t::string) return '"' + _str + '"';
            return "null";
        }

        json& operator[](const std::string& k) { _type = value_t::object; return _obj[k]; }
        void operator=(const std::string& s) { _type = value_t::string; _str = s; }
        void operator=(const json& o) { _type = o._type; _str = o._str; _obj = o._obj; }

    private:
        value_t _type = value_t::null;
        std::string _str;
        object_t _obj;
    };
}
