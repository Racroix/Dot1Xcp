#include "pch.h"
#include "ProviderJson.h"

void SecureClearString(std::string& s)
{
    if (!s.empty()) {
        SecureZeroMemory(&s[0], s.size());
    }
    s.clear();
}

void SecureClearMsg(Msg& m)
{
    SecureClearString(m.type);
    SecureClearString(m.sam);
    SecureClearString(m.password);
    SecureClearString(m.reason);
}

static std::string Trim(const std::string& s)
{
    size_t i = 0, j = s.size();
    while (i < j && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    while (j > i && (s[j - 1] == ' ' || s[j - 1] == '\t' || s[j - 1] == '\r' || s[j - 1] == '\n')) --j;
    return s.substr(i, j - i);
}

static int HexValue(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void AppendUtf8(std::string& out, unsigned int cp)
{
    if (cp <= 0x7F) {
        out.push_back((char)cp);
    } else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

static bool ParseHex4(const std::string& s, size_t pos, unsigned int& codepoint)
{
    if (pos + 4 > s.size()) return false;
    codepoint = 0;
    for (size_t i = 0; i < 4; ++i) {
        int v = HexValue((unsigned char)s[pos + i]);
        if (v < 0) return false;
        codepoint = (codepoint << 4) | (unsigned int)v;
    }
    return true;
}

static bool UnescapeJsonString(const std::string& in, std::string& out)
{
    out.clear();
    out.reserve(in.size());

    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }

        if (i + 1 >= in.size()) return false;
        char e = in[++i];
        switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
            unsigned int cp1 = 0;
            if (!ParseHex4(in, i + 1, cp1)) return false;
            i += 4;

            if (cp1 >= 0xD800 && cp1 <= 0xDBFF) {
                if (i + 6 < in.size() && in[i + 1] == '\\' && in[i + 2] == 'u') {
                    unsigned int cp2 = 0;
                    if (!ParseHex4(in, i + 3, cp2)) return false;
                    if (cp2 >= 0xDC00 && cp2 <= 0xDFFF) {
                        i += 6;
                        unsigned int cp = 0x10000 + (((cp1 - 0xD800) << 10) | (cp2 - 0xDC00));
                        AppendUtf8(out, cp);
                        break;
                    }
                }
                return false;
            }
            if (cp1 >= 0xDC00 && cp1 <= 0xDFFF) return false;

            AppendUtf8(out, cp1);
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

static void SkipWs(const std::string& s, size_t& i)
{
    while (i < s.size() &&
           (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
        ++i;
    }
}

static bool ParseJsonQuotedToken(const std::string& s, size_t& i, std::string& rawToken)
{
    rawToken.clear();
    if (i >= s.size() || s[i] != '"') return false;
    ++i;

    size_t start = i;
    bool escaped = false;
    while (i < s.size()) {
        char c = s[i];
        if (escaped) {
            escaped = false;
            ++i;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            ++i;
            continue;
        }
        if (c == '"') {
            break;
        }
        ++i;
    }
    if (i >= s.size() || s[i] != '"') return false;
    rawToken = s.substr(start, i - start);
    ++i;
    return true;
}

static bool ParseFlatBrokerMessage(const std::string& obj, Msg& out)
{
    size_t i = 0;
    SkipWs(obj, i);
    if (i >= obj.size() || obj[i] != '{') return false;
    ++i;

    bool hasType = false;
    while (true) {
        SkipWs(obj, i);
        if (i >= obj.size()) return false;
        if (obj[i] == '}') {
            ++i;
            break;
        }

        std::string keyRaw;
        if (!ParseJsonQuotedToken(obj, i, keyRaw)) return false;
        std::string key;
        if (!UnescapeJsonString(keyRaw, key)) {
            SecureClearString(keyRaw);
            return false;
        }
        SecureClearString(keyRaw);

        SkipWs(obj, i);
        if (i >= obj.size() || obj[i] != ':') {
            SecureClearString(key);
            return false;
        }
        ++i;
        SkipWs(obj, i);

        std::string valueRaw;
        if (!ParseJsonQuotedToken(obj, i, valueRaw)) {
            SecureClearString(key);
            return false;
        }
        std::string value;
        bool okValue = UnescapeJsonString(valueRaw, value);
        SecureClearString(valueRaw);
        if (!okValue) {
            SecureClearString(key);
            return false;
        }

        if (key == "type") {
            SecureClearString(out.type);
            out.type.swap(value);
            hasType = true;
        } else if (key == "sam") {
            SecureClearString(out.sam);
            out.sam.swap(value);
        } else if (key == "password") {
            SecureClearString(out.password);
            out.password.swap(value);
        } else if (key == "reason") {
            SecureClearString(out.reason);
            out.reason.swap(value);
        }
        SecureClearString(value);
        SecureClearString(key);

        SkipWs(obj, i);
        if (i >= obj.size()) return false;
        if (obj[i] == ',') {
            ++i;
            continue;
        }
        if (obj[i] == '}') {
            ++i;
            break;
        }
        return false;
    }

    SkipWs(obj, i);
    if (i != obj.size()) return false;
    return hasType;
}

bool ParseJsonLine(const std::string& line, Msg& out)
{
    SecureClearMsg(out);
    std::string s = Trim(line);
    if (s.empty()) {
        SecureClearString(s);
        return false;
    }

    bool ok = ParseFlatBrokerMessage(s, out);
    SecureClearString(s);
    if (!ok || out.type.empty()) {
        SecureClearMsg(out);
        return false;
    }
    return true;
}
