#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

// A reader for ISO 10303-21 (STEP) files, the text format IFC is written in.
// It indexes every entity instance up front and parses an instance's
// attributes only when asked, so a large model costs one pass over the bytes
// and properties are read for the elements the user actually looks at.
namespace Step {

struct Value {
    enum Kind { Null, Derived, Integer, Real, String, Enum, Ref, List, Typed, Binary };

    Kind kind = Null;
    double number = 0;
    qint64 ref = 0;
    // The text of a String, Enum, or Binary, and the type name of a Typed value
    // such as IFCLABEL('Wall'), whose one wrapped value is in items.
    QString text;
    QVector<Value> items;

    bool isNull() const { return kind == Null || kind == Derived; }
    bool isNumber() const { return kind == Integer || kind == Real; }
    // The value inside IFCLABEL('Wall') or the value itself.
    const Value &unwrapped() const { return kind == Typed && !items.isEmpty() ? items.first().unwrapped() : *this; }
    QString string() const { const Value &v = unwrapped(); return v.kind == String || v.kind == Enum ? v.text : QString(); }
    // Every reference in a list attribute, or the one reference.
    QVector<qint64> refs() const;
};

using Arguments = QVector<Value>;

class File {
public:
    bool load(const QString &path, QString *error = nullptr);
    bool parse(QByteArray data, QString *error = nullptr);

    QString schema() const { return m_schema; }
    int size() const { return m_records.size(); }
    bool contains(qint64 id) const { return m_records.contains(id); }
    // The entity type in upper case, as the file writes it ("IFCWALL").
    QByteArray type(qint64 id) const { return m_records.value(id).type; }
    Arguments arguments(qint64 id) const;
    QVector<qint64> instancesOf(const QByteArray &type) const { return m_byType.value(type); }
    // Every IfcRoot instance by its 22-character GlobalId.
    qint64 idForGlobalId(const QString &globalId) const { return m_byGlobalId.value(globalId, 0); }

private:
    struct Record {
        QByteArray type;
        qsizetype start = 0;
        qsizetype length = 0;
    };

    QByteArray m_data;
    QString m_schema;
    QHash<qint64, Record> m_records;
    QHash<QByteArray, QVector<qint64>> m_byType;
    QHash<QString, qint64> m_byGlobalId;
};

// Decodes a STEP string's escapes: doubled quotes and backslashes, X2 and X4
// runs for Unicode, X for one ISO 8859-1 byte, and S for the upper half of a
// code page.
QString decodeString(const QByteArray &raw);

Arguments parseArguments(const char *begin, const char *end);

}
