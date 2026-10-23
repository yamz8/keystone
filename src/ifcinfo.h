#pragma once

#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QVector>

#include "step.h"

// What an IFC file says about its elements: class, name, type, storey,
// materials, property sets, and quantities. Geometry comes from IfcConvert;
// everything else is read here, straight from the STEP text, so it works for
// IFC2x3, IFC4, and IFC4x3 alike.
class IfcInfo {
    Q_DECLARE_TR_FUNCTIONS(IfcInfo)

public:
    struct Row {
        QString label;
        QString value;
    };
    struct Section {
        QString title;
        QVector<Row> rows;
    };
    struct Details {
        bool found = false;
        QString ifcClass;
        QString name;
        // Class, type, storey, material, area, volume, GlobalId: what the
        // panel shows at the top, before the full property sets.
        QVector<Row> summary;
        QVector<Section> sections;
    };
    // Measured from the element's mesh, for when the file carries no
    // quantities; negative means unknown.
    struct Measured {
        double area = -1;
        double volume = -1;
    };

    bool load(const QString &path, QString *error = nullptr);
    bool parse(QByteArray data, QString *error = nullptr);

    QString schema() const { return m_file.schema(); }
    bool contains(const QString &globalId) const { return m_file.idForGlobalId(globalId) != 0; }
    QString className(const QString &globalId) const;
    QString storeyName(const QString &globalId) const;
    // Formats with the default QLocale and the installed translator, so call
    // it again after a language switch.
    Details describe(const QString &globalId) const;
    Details describe(const QString &globalId, const Measured &measured) const;

    // "IFCWALLSTANDARDCASE" as the schema spells it, "IfcWallStandardCase".
    static QString prettyClassName(const QByteArray &upper);
    static QString formatNumber(double value);

private:
    struct Units {
        QHash<QString, QString> symbols; // "LENGTHUNIT" -> "mm"
    };

    void index();
    QString name(qint64 id) const;
    QString materialText(qint64 id, int depth = 0) const;
    QString valueText(const Step::Value &value, const QString &unitHint = {}) const;
    QString unitFor(const QString &measureType) const;
    qint64 storeyOf(qint64 id) const;
    QVector<Section> propertySets(qint64 definition) const;

    Step::File m_file;
    QHash<qint64, QVector<qint64>> m_definitions; // object -> property sets and quantities
    QHash<qint64, qint64> m_typeOf;
    QHash<qint64, qint64> m_materialOf;
    QHash<qint64, qint64> m_containedIn;
    QHash<qint64, qint64> m_parentOf;
    Units m_units;
};
