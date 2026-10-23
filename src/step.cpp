#include "step.h"

#include <QFile>
#include <QRegularExpression>
#include <QUtf8StringView>

namespace Step {

QVector<qint64> Value::refs() const {
    QVector<qint64> result;
    if (kind == Ref)
        result.append(ref);
    else if (kind == List)
        for (const Value &item : items)
            if (item.kind == Ref)
                result.append(item.ref);
    return result;
}

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Whitespace and /* comments */ may sit between any two tokens.
const char *skip(const char *p, const char *end) {
    while (p < end) {
        if (isSpace(*p)) {
            ++p;
        } else if (*p == '/' && p + 1 < end && p[1] == '*') {
            p += 2;
            while (p + 1 < end && !(p[0] == '*' && p[1] == '/'))
                ++p;
            p = p + 1 < end ? p + 2 : end;
        } else {
            break;
        }
    }
    return p;
}

// Past the closing quote of a string that opens at p, where '' is a quote
// inside the string.
const char *skipString(const char *p, const char *end) {
    for (++p; p < end; ++p) {
        if (*p == '\'') {
            if (p + 1 < end && p[1] == '\'')
                ++p;
            else
                return p + 1;
        }
    }
    return end;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

Value parseValue(const char *&p, const char *end);

QVector<Value> parseList(const char *&p, const char *end) {
    // p is just past the opening parenthesis.
    QVector<Value> items;
    p = skip(p, end);
    if (p < end && *p == ')') {
        ++p;
        return items;
    }
    while (p < end) {
        items.append(parseValue(p, end));
        p = skip(p, end);
        if (p < end && *p == ',') {
            ++p;
            continue;
        }
        if (p < end && *p == ')')
            ++p;
        break;
    }
    return items;
}

Value parseValue(const char *&p, const char *end) {
    Value value;
    p = skip(p, end);
    if (p >= end)
        return value;
    const char c = *p;
    if (c == '$') {
        ++p;
    } else if (c == '*') {
        value.kind = Value::Derived;
        ++p;
    } else if (c == '#') {
        const char *start = ++p;
        while (p < end && isDigit(*p))
            ++p;
        value.kind = Value::Ref;
        value.ref = QByteArray(start, p - start).toLongLong();
    } else if (c == '\'') {
        const char *start = p;
        p = skipString(p, end);
        value.kind = Value::String;
        value.text = decodeString(QByteArray(start + 1, qMax<qsizetype>(0, p - start - 2)));
    } else if (c == '"') {
        const char *start = ++p;
        while (p < end && *p != '"')
            ++p;
        value.kind = Value::Binary;
        value.text = QString::fromLatin1(start, p - start);
        if (p < end)
            ++p;
    } else if (c == '.') {
        const char *start = ++p;
        while (p < end && *p != '.')
            ++p;
        value.kind = Value::Enum;
        value.text = QString::fromLatin1(start, p - start);
        if (p < end)
            ++p;
    } else if (c == '(') {
        ++p;
        value.kind = Value::List;
        value.items = parseList(p, end);
    } else if (isUpper(c)) {
        const char *start = p;
        while (p < end && (isUpper(*p) || isDigit(*p) || *p == '_'))
            ++p;
        value.kind = Value::Typed;
        value.text = QString::fromLatin1(start, p - start);
        p = skip(p, end);
        if (p < end && *p == '(') {
            ++p;
            value.items = parseList(p, end);
        }
    } else {
        const char *start = p;
        bool real = false;
        while (p < end && (isDigit(*p) || *p == '-' || *p == '+' || *p == '.' || *p == 'E' || *p == 'e')) {
            real = real || *p == '.' || *p == 'E' || *p == 'e';
            ++p;
        }
        if (p == start) {
            // Not a value this reader knows; step over it so parsing goes on.
            ++p;
            return value;
        }
        value.kind = real ? Value::Real : Value::Integer;
        value.number = QByteArray(start, p - start).toDouble();
    }
    return value;
}

}

Arguments parseArguments(const char *begin, const char *end) {
    const char *p = begin;
    Arguments arguments;
    p = skip(p, end);
    while (p < end) {
        arguments.append(parseValue(p, end));
        p = skip(p, end);
        if (p < end && *p == ',')
            ++p;
        else
            break;
    }
    return arguments;
}

QString decodeString(const QByteArray &raw) {
    QString out;
    out.reserve(raw.size());
    const char *p = raw.constData(), *end = p + raw.size();
    while (p < end) {
        if (*p == '\'' && p + 1 < end && p[1] == '\'') {
            out += QLatin1Char('\'');
            p += 2;
        } else if (*p == '\\' && p + 1 < end && p[1] == '\\') {
            out += QLatin1Char('\\');
            p += 2;
        } else if (*p == '\\' && end - p >= 4 && p[1] == 'X' && p[3] == '\\' && (p[2] == '2' || p[2] == '4')) {
            // \X2\00E9\X0\ is UTF-16 in groups of four hex digits, \X4\ is
            // UTF-32 in groups of eight, both ended by \X0\.
            const int width = p[2] == '2' ? 4 : 8;
            p += 4;
            while (end - p >= width) {
                if (p[0] == '\\')
                    break;
                uint code = 0;
                bool ok = true;
                for (int i = 0; i < width; ++i) {
                    const int digit = hexValue(p[i]);
                    ok = ok && digit >= 0;
                    code = code * 16 + uint(qMax(0, digit));
                }
                if (!ok)
                    break;
                if (width == 4)
                    out += QChar(char16_t(code));
                else
                    out += QString::fromUcs4(reinterpret_cast<const char32_t *>(&code), 1);
                p += width;
            }
            if (end - p >= 4 && p[0] == '\\' && p[1] == 'X' && p[2] == '0' && p[3] == '\\')
                p += 4;
        } else if (*p == '\\' && end - p >= 5 && p[1] == 'X' && p[2] == '\\'
                   && hexValue(p[3]) >= 0 && hexValue(p[4]) >= 0) {
            out += QChar(char16_t(hexValue(p[3]) * 16 + hexValue(p[4])));
            p += 5;
        } else if (*p == '\\' && end - p >= 4 && p[1] == 'S' && p[2] == '\\') {
            out += QChar(char16_t(uchar(p[3]) + 128));
            p += 4;
        } else if (*p == '\\' && end - p >= 4 && p[1] == 'P' && p[3] == '\\') {
            // Code page switches; this reader treats every page as Latin-1.
            p += 4;
        } else {
            // IFC files are ASCII by the standard, but exporters write raw
            // UTF-8 or raw ISO 8859-1 too; a run that isn't valid UTF-8 is
            // read as Latin-1, so "Wärme" never turns into replacement marks.
            const char *start = p;
            while (p < end && *p != '\\' && *p != '\'')
                ++p;
            if (p == start) {
                out += QLatin1Char(*p++);
            } else {
                const QByteArrayView run(start, p - start);
                out += QUtf8StringView(run).isValidUtf8() ? QString::fromUtf8(run) : QString::fromLatin1(run);
            }
        }
    }
    return out;
}

bool File::load(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return parse(file.readAll(), error);
}

bool File::parse(QByteArray data, QString *error) {
    m_data = std::move(data);
    m_records.clear();
    m_byType.clear();
    m_byGlobalId.clear();
    m_schema.clear();

    const qsizetype dataStart = m_data.indexOf("DATA;");
    if (!m_data.startsWith("ISO-10303-21;") && dataStart < 0) {
        if (error)
            *error = QStringLiteral("not a STEP file");
        return false;
    }
    static const QRegularExpression schema(R"re(FILE_SCHEMA\s*\(\s*\(\s*'([^']*)')re");
    const auto match = schema.match(QString::fromLatin1(m_data.left(dataStart < 0 ? 4096 : dataStart)));
    if (match.hasMatch())
        m_schema = match.captured(1).toUpper();

    const char *base = m_data.constData();
    const char *p = base + (dataStart < 0 ? 0 : dataStart + 5), *end = base + m_data.size();
    m_records.reserve(int(m_data.size() / 80));
    while (p < end) {
        p = skip(p, end);
        if (p >= end)
            break;
        if (*p != '#') {
            // ENDSEC; and anything else between instances.
            if (end - p >= 6 && qstrncmp(p, "ENDSEC", 6) == 0)
                break;
            while (p < end && *p != ';')
                p = *p == '\'' ? skipString(p, end) : p + 1;
            ++p;
            continue;
        }
        const char *idStart = ++p;
        while (p < end && isDigit(*p))
            ++p;
        const qint64 id = QByteArray(idStart, p - idStart).toLongLong();
        p = skip(p, end);
        if (p < end && *p == '=')
            ++p;
        p = skip(p, end);
        const char *typeStart = p;
        while (p < end && (isUpper(*p) || isDigit(*p) || *p == '_'))
            ++p;
        Record record;
        record.type = QByteArray(typeStart, p - typeStart);
        p = skip(p, end);
        if (record.type.isEmpty() || p >= end || *p != '(') {
            // A complex instance, #1=(A() B()); — not used by the IFC
            // entities this app reads, so it is stepped over.
            while (p < end && *p != ';')
                p = *p == '\'' ? skipString(p, end) : p + 1;
            ++p;
            continue;
        }
        const char *argsStart = ++p;
        int depth = 1;
        while (p < end && depth > 0) {
            if (*p == '\'') {
                p = skipString(p, end);
                continue;
            }
            if (*p == '(')
                ++depth;
            else if (*p == ')')
                --depth;
            ++p;
        }
        record.start = argsStart - base;
        record.length = qMax<qsizetype>(0, (p - 1) - argsStart);
        while (p < end && *p != ';')
            ++p;
        ++p;

        // IfcRoot instances open with their GlobalId, a 22-character string.
        if (record.length > 24 && argsStart[0] == '\'' && argsStart[23] == '\'')
            m_byGlobalId.insert(QString::fromLatin1(argsStart + 1, 22), id);
        m_byType[record.type].append(id);
        m_records.insert(id, record);
    }
    if (m_records.isEmpty()) {
        if (error)
            *error = QStringLiteral("no entity instances");
        return false;
    }
    return true;
}

Arguments File::arguments(qint64 id) const {
    const auto it = m_records.constFind(id);
    if (it == m_records.constEnd())
        return {};
    const char *begin = m_data.constData() + it->start;
    return parseArguments(begin, begin + it->length);
}

}
