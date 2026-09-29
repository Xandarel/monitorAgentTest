#include "json_util.hpp"

#include "queue.hpp"

#include <string>

namespace {

void append_json_string(std::string& out, std::string_view text) {
    out.push_back('"');
    for (unsigned char ch : text) {
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(hex[ch >> 4]);
                out.push_back(hex[ch & 0x0F]);
            } else {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    out.push_back('"');
}

void append_record_json(std::string& out, const ActivityRecord& record, std::string_view indent) {
    out += indent;
    out += "{\n";
    out += indent;
    out += "  \"time\": ";
    append_json_string(out, record.time);
    out += ",\n";
    out += indent;
    out += "  \"process_name\": ";
    append_json_string(out, record.process_name);
    out += ",\n";
    out += indent;
    out += "  \"window_title\": ";
    append_json_string(out, record.window_title);
    out += ",\n";
    out += indent;
    out += "  \"user_active\": ";
    out += record.user_active ? "true" : "false";
    out += "\n";
    out += indent;
    out += "}";
}

void append_records(std::string& out, const std::vector<ActivityRecord>& records, std::string_view indent) {
    if (records.empty()) {
        out += "[]";
        return;
    }
    out += "[\n";
    for (std::size_t i = 0; i < records.size(); ++i) {
        append_record_json(out, records[i], indent);
        if (i + 1 != records.size()) {
            out += ",";
        }
        out += "\n";
    }
    if (indent.size() >= 2) {
        out.append(indent.data(), indent.size() - 2);
    }
    out += "]";
}

class Parser {
public:
    explicit Parser(std::string_view input) : in_(input) {}

    std::vector<ActivityRecord> parse_array() {
        skip_ws();
        expect('[');
        std::vector<ActivityRecord> records;
        skip_ws();
        if (consume(']')) {
            finish();
            return records;
        }
        while (true) {
            records.push_back(parse_record());
            skip_ws();
            if (consume(']')) {
                break;
            }
            expect(',');
            skip_ws();
        }
        finish();
        return records;
    }

private:
    std::string_view in_;
    std::size_t i_ = 0;

    void finish() {
        skip_ws();
        if (i_ != in_.size()) {
            throw JsonParseError("JSON: лишние данные после массива на позиции " + std::to_string(i_));
        }
    }

    void skip_ws() {
        while (i_ < in_.size()) {
            const char ch = in_[i_];
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                break;
            }
            ++i_;
        }
    }

    bool consume(char expected) {
        if (i_ < in_.size() && in_[i_] == expected) {
            ++i_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) {
            throw JsonParseError("JSON: ожидался символ '" + std::string(1, expected) +
                                 "' на позиции " + std::to_string(i_));
        }
    }

    void append_codepoint(std::string& out, unsigned code) const {
        if (code <= 0x7F) {
            out.push_back(static_cast<char>(code));
            return;
        }
        if (code <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            return;
        }
        if (code <= 0xFFFF) {
            if (code >= 0xD800 && code <= 0xDFFF) {
                throw JsonParseError("JSON: суррогатная пара UTF-16 не поддерживается");
            }
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            return;
        }
        throw JsonParseError("JSON: код символа вне BMP");
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (i_ < in_.size()) {
            const char ch = in_[i_++];
            if (ch == '"') {
                return out;
            }
            if (static_cast<unsigned char>(ch) < 0x20) {
                throw JsonParseError("JSON: неэкранированный управляющий символ в строке");
            }
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }
            if (i_ >= in_.size()) {
                throw JsonParseError("JSON: оборванная escape-последовательность");
            }
            const char escaped = in_[i_++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                out.push_back(escaped);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                if (i_ + 4 > in_.size()) {
                    throw JsonParseError("JSON: неполный код \\u");
                }
                unsigned code = 0;
                for (int n = 0; n < 4; ++n) {
                    const char hex = in_[i_++];
                    code <<= 4;
                    if (hex >= '0' && hex <= '9') {
                        code += static_cast<unsigned>(hex - '0');
                    } else if (hex >= 'a' && hex <= 'f') {
                        code += static_cast<unsigned>(hex - 'a' + 10);
                    } else if (hex >= 'A' && hex <= 'F') {
                        code += static_cast<unsigned>(hex - 'A' + 10);
                    } else {
                        throw JsonParseError("JSON: некорректный код \\u");
                    }
                }
                append_codepoint(out, code);
                break;
            }
            default:
                throw JsonParseError("JSON: неизвестная escape-последовательность");
            }
        }
        throw JsonParseError("JSON: незакрытая строка");
    }

    bool parse_bool() {
        if (in_.substr(i_, 4) == "true") {
            i_ += 4;
            return true;
        }
        if (in_.substr(i_, 5) == "false") {
            i_ += 5;
            return false;
        }
        throw JsonParseError("JSON: ожидалось true или false на позиции " + std::to_string(i_));
    }

    ActivityRecord parse_record() {
        expect('{');
        ActivityRecord record;
        bool saw_time = false;
        bool saw_process = false;
        bool saw_title = false;
        bool saw_active = false;
        skip_ws();
        if (consume('}')) {
            throw JsonParseError("JSON: пустая запись активности");
        }
        while (true) {
            const std::string key = parse_string();
            skip_ws();
            expect(':');
            skip_ws();
            if (key == "time") {
                record.time = parse_string();
                saw_time = true;
            } else if (key == "process_name") {
                record.process_name = parse_string();
                saw_process = true;
            } else if (key == "window_title") {
                record.window_title = parse_string();
                saw_title = true;
            } else if (key == "user_active") {
                record.user_active = parse_bool();
                saw_active = true;
            } else {
                throw JsonParseError("JSON: неизвестное поле \"" + key + "\"");
            }
            skip_ws();
            if (consume('}')) {
                break;
            }
            expect(',');
            skip_ws();
        }
        if (!saw_time || !saw_process || !saw_title || !saw_active) {
            throw JsonParseError("JSON: в записи активности не хватает полей");
        }
        return record;
    }
};

}  // namespace

std::string make_storage_json(const std::vector<ActivityRecord>& records) {
    std::string out;
    append_records(out, records, "  ");
    out += "\n";
    return out;
}

std::string make_batch_json(std::string_view agent_id,
                            std::time_t timestamp,
                            const std::vector<ActivityRecord>& records) {
    std::string out = "{\n  \"agent_id\": ";
    append_json_string(out, agent_id);
    out += ",\n  \"timestamp\": ";
    out += std::to_string(static_cast<long long>(timestamp));
    out += ",\n  \"payload\": ";
    append_records(out, records, "    ");
    out += "\n}\n";
    return out;
}

std::vector<ActivityRecord> parse_storage_json(std::string_view json) {
    return Parser(json).parse_array();
}

std::string run_self_test() {
    std::string errors;

    auto expect = [&](bool ok, const char* name) {
        if (!ok) {
            errors += " - ";
            errors += name;
            errors += "\n";
        }
    };

    try {
        expect(parse_storage_json("[]").empty(), "пустой массив");
        expect(parse_storage_json("  [ ]\n").empty(), "пустой массив с пробелами");

        ActivityRecord record;
        record.time = "2026-09-15 13:55:00";
        record.process_name = "note\"pad.exe";
        record.window_title = std::string("Строка\\с\nпереносом\tи \"ИНСАЙДЕР\"") + '\x01';
        record.user_active = false;

        ActivityRecord second = record;
        second.process_name = "chrome.exe";
        second.window_title = "ТЗ.txt — Блокнот";
        second.user_active = true;

        const auto stored = make_storage_json({record, second});
        const auto parsed = parse_storage_json(stored);
        expect(parsed.size() == 2, "размер после чтения");
        if (parsed.size() == 2) {
            expect(parsed[0].time == record.time, "time");
            expect(parsed[0].process_name == record.process_name, "process_name");
            expect(parsed[0].window_title == record.window_title, "window_title");
            expect(parsed[0].user_active == false, "user_active false");
            expect(parsed[1].process_name == "chrome.exe", "вторая запись");
            expect(parsed[1].user_active == true, "user_active true");
        }

        const auto batch = make_batch_json("DESKTOP-MIDDLE-C", 1792147320, {record, second});
        expect(batch.find("\"agent_id\": \"DESKTOP-MIDDLE-C\"") != std::string::npos, "agent_id");
        expect(batch.find("\"timestamp\": 1792147320") != std::string::npos, "timestamp");
        expect(batch.find("\"user_active\": false") != std::string::npos, "false в пакете");
        expect(batch.find("\"user_active\": true") != std::string::npos, "true в пакете");
        expect(batch.find("\\u0001") != std::string::npos, "экранирование управляющего символа");

        std::vector<ActivityRecord> many(105, record);
        const auto dropped = trim_queue(many, 100);
        expect(dropped == 5 && many.size() == 100, "лимит очереди 100");

        bool threw = false;
        try {
            parse_storage_json("{");
        } catch (const JsonParseError&) {
            threw = true;
        }
        expect(threw, "битый JSON должен отвергаться");
    } catch (const std::exception& ex) {
        errors += " - исключение: ";
        errors += ex.what();
        errors += "\n";
    }

    if (!errors.empty()) {
        return std::string("self-test не пройден:\n") + errors;
    }
    return {};
}
