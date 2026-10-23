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

    // An element with a shape, as the model tree, the filters, and the
    // command line see it.
    struct Product {
        QString globalId;
        QString ifcClass;
        QString name;
        qint64 storey = 0;
    };
    struct Storey {
        qint64 id = 0;
        QString name;
        // In the project's length unit, for ordering; absent in some files.
        double elevation = 0;
        bool hasElevation = false;
    };
    // What X-ray mode shows an element under; walls and slabs whose file
    // doesn't say whether they carry load count as both structure and
    // architecture.
    enum Discipline { Structure = 1, Services = 2, Architecture = 4 };

    bool load(const QString &path, QString *error = nullptr);
    bool parse(QByteArray data, QString *error = nullptr);

    QString schema() const { return m_file.schema(); }
    bool contains(const QString &globalId) const { return m_file.idForGlobalId(globalId) != 0; }
    QString className(const QString &globalId) const;
    QString storeyName(const QString &globalId) const;
    QString nameOf(const QString &globalId) const;
    qint64 storeyIdOf(const QString &globalId) const;
    QVector<Product> products() const;
    // Every storey, lowest first where the file gives elevations.
    QVector<Storey> storeys() const;
    int disciplines(const QString &globalId) const;
    // By class alone; loadBearing is 1 or 0 from the element's common
    // property set, or -1 when it doesn't say.
    static int disciplinesOfClass(const QString &ifcClass, int loadBearing = -1);
    // The element's Pset_*Common LoadBearing, its type's if it has none: 1,
    // 0, or -1 when neither says.
    int loadBearing(qint64 id) const;
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
    QString unitSymbol(qint64 unit) const;
    // A quantity's value with its unit, or empty when it has no number.
    QString quantityText(qint64 quantity) const;
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
