#include "ifcinfo.h"

#include <QFile>
#include <QLocale>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {

const QHash<QByteArray, QString> &classNames() {
    // Every entity name of IFC2x3, IFC4, and IFC4x3 ADD2, in the schemas'
    // own spelling, compiled in from ifcnames.txt.
    static const QHash<QByteArray, QString> names = [] {
        QHash<QByteArray, QString> result;
        QFile file(":/ifcnames.txt");
        if (file.open(QIODevice::ReadOnly))
            for (const QByteArray &line : file.readAll().split('\n'))
                if (!line.trimmed().isEmpty())
                    result.insert(line.trimmed().toUpper(), QString::fromLatin1(line.trimmed()));
        return result;
    }();
    return names;
}

QString siSymbol(const QString &prefix, const QString &name) {
    static const QHash<QString, QString> prefixes = {
        {"EXA", "E"}, {"PETA", "P"}, {"TERA", "T"}, {"GIGA", "G"}, {"MEGA", "M"}, {"KILO", "k"},
        {"HECTO", "h"}, {"DECA", "da"}, {"DECI", "d"}, {"CENTI", "c"}, {"MILLI", "m"},
        {"MICRO", "µ"}, {"NANO", "n"}, {"PICO", "p"}, {"FEMTO", "f"}, {"ATTO", "a"},
    };
    static const QHash<QString, QString> names = {
        {"METRE", "m"}, {"SQUARE_METRE", "m²"}, {"CUBIC_METRE", "m³"}, {"GRAM", "g"},
        {"SECOND", "s"}, {"RADIAN", "rad"}, {"STERADIAN", "sr"}, {"KELVIN", "K"},
        {"DEGREE_CELSIUS", "°C"}, {"WATT", "W"}, {"PASCAL", "Pa"}, {"NEWTON", "N"},
        {"JOULE", "J"}, {"HERTZ", "Hz"}, {"AMPERE", "A"}, {"VOLT", "V"}, {"OHM", "Ω"},
        {"LUMEN", "lm"}, {"LUX", "lx"}, {"CANDELA", "cd"}, {"MOLE", "mol"},
        {"COULOMB", "C"}, {"FARAD", "F"}, {"HENRY", "H"}, {"TESLA", "T"}, {"WEBER", "Wb"},
        {"SIEMENS", "S"}, {"BECQUEREL", "Bq"}, {"GRAY", "Gy"}, {"SIEVERT", "Sv"},
    };
    return prefixes.value(prefix) + names.value(name, name.toLower());
}

QString conversionSymbol(const QString &name) {
    static const QHash<QString, QString> symbols = {
        {"foot", "ft"}, {"feet", "ft"}, {"inch", "in"}, {"yard", "yd"}, {"mile", "mi"},
        {"square foot", "ft²"}, {"square feet", "ft²"}, {"square inch", "in²"}, {"square yard", "yd²"},
        {"cubic foot", "ft³"}, {"cubic feet", "ft³"}, {"cubic inch", "in³"}, {"cubic yard", "yd³"},
        {"degree", "°"}, {"pound", "lb"}, {"gallon", "gal"}, {"acre", "ac"},
    };
    return symbols.value(name.toLower().trimmed(), name);
}

// The unit type a measure takes when the property names no unit of its own.
QString unitTypeOfMeasure(const QString &measure) {
    static const QHash<QString, QString> types = {
        {"IFCLENGTHMEASURE", "LENGTHUNIT"}, {"IFCPOSITIVELENGTHMEASURE", "LENGTHUNIT"},
        {"IFCNONNEGATIVELENGTHMEASURE", "LENGTHUNIT"}, {"IFCAREAMEASURE", "AREAUNIT"},
        {"IFCVOLUMEMEASURE", "VOLUMEUNIT"}, {"IFCMASSMEASURE", "MASSUNIT"},
        {"IFCPLANEANGLEMEASURE", "PLANEANGLEUNIT"}, {"IFCPOSITIVEPLANEANGLEMEASURE", "PLANEANGLEUNIT"},
        {"IFCTIMEMEASURE", "TIMEUNIT"}, {"IFCTHERMODYNAMICTEMPERATUREMEASURE", "THERMODYNAMICTEMPERATUREUNIT"},
    };
    return types.value(measure);
}

// SI symbols for derived measures, which files rarely assign a unit to.
QString derivedSymbol(const QString &measure) {
    static const QHash<QString, QString> symbols = {
        {"IFCTHERMALTRANSMITTANCEMEASURE", "W/(m²·K)"}, {"IFCTHERMALCONDUCTIVITYMEASURE", "W/(m·K)"},
        {"IFCPOWERMEASURE", "W"}, {"IFCPRESSUREMEASURE", "Pa"}, {"IFCVOLUMETRICFLOWRATEMEASURE", "m³/s"},
        {"IFCMASSDENSITYMEASURE", "kg/m³"}, {"IFCLINEARVELOCITYMEASURE", "m/s"},
        {"IFCELECTRICCURRENTMEASURE", "A"}, {"IFCELECTRICVOLTAGEMEASURE", "V"},
        {"IFCFREQUENCYMEASURE", "Hz"}, {"IFCSOUNDPOWERLEVELMEASURE", "dB"},
        {"IFCSOUNDPRESSURELEVELMEASURE", "dB"}, {"IFCILLUMINANCEMEASURE", "lx"},
        {"IFCLUMINOUSFLUXMEASURE", "lm"},
    };
    return symbols.value(measure);
}

bool isSpatial(const QByteArray &type) {
    return type == "IFCSITE" || type == "IFCBUILDING" || type == "IFCBUILDINGSTOREY" || type == "IFCSPACE"
        || type == "IFCFACILITY" || type == "IFCFACILITYPART" || type == "IFCBRIDGE" || type == "IFCROAD"
        || type == "IFCRAILWAY" || type == "IFCSPATIALZONE";
}

QString attribute(const Step::Arguments &arguments, int index) {
    return index < arguments.size() ? arguments.at(index).string() : QString();
}

qint64 refAt(const Step::Arguments &arguments, int index) {
    return index < arguments.size() && arguments.at(index).kind == Step::Value::Ref ? arguments.at(index).ref : 0;
}

QVector<qint64> refsAt(const Step::Arguments &arguments, int index) {
    return index < arguments.size() ? arguments.at(index).refs() : QVector<qint64>();
}

}

bool IfcInfo::load(const QString &path, QString *error) {
    if (!m_file.load(path, error))
        return false;
    index();
    return true;
}

bool IfcInfo::parse(QByteArray data, QString *error) {
    if (!m_file.parse(std::move(data), error))
        return false;
    index();
    return true;
}

void IfcInfo::index() {
    m_definitions.clear();
    m_typeOf.clear();
    m_materialOf.clear();
    m_containedIn.clear();
    m_parentOf.clear();
    m_units = {};

    // The relationship attributes sit at the same positions in IFC2x3, IFC4,
    // and IFC4x3: RelatedObjects at 4, the relating side at 5 (reversed for
    // aggregation and nesting).
    for (const qint64 rel : m_file.instancesOf("IFCRELDEFINESBYPROPERTIES")) {
        const auto args = m_file.arguments(rel);
        const auto definitions = refsAt(args, 5);
        for (const qint64 object : refsAt(args, 4))
            m_definitions[object] += definitions;
    }
    for (const qint64 rel : m_file.instancesOf("IFCRELDEFINESBYTYPE")) {
        const auto args = m_file.arguments(rel);
        for (const qint64 object : refsAt(args, 4))
            m_typeOf.insert(object, refAt(args, 5));
    }
    for (const qint64 rel : m_file.instancesOf("IFCRELASSOCIATESMATERIAL")) {
        const auto args = m_file.arguments(rel);
        for (const qint64 object : refsAt(args, 4))
            m_materialOf.insert(object, refAt(args, 5));
    }
    for (const qint64 rel : m_file.instancesOf("IFCRELCONTAINEDINSPATIALSTRUCTURE")) {
        const auto args = m_file.arguments(rel);
        for (const qint64 element : refsAt(args, 4))
            m_containedIn.insert(element, refAt(args, 5));
    }
    for (const QByteArray &type : {QByteArray("IFCRELAGGREGATES"), QByteArray("IFCRELNESTS")}) {
        for (const qint64 rel : m_file.instancesOf(type)) {
            const auto args = m_file.arguments(rel);
            for (const qint64 child : refsAt(args, 5))
                m_parentOf.insert(child, refAt(args, 4));
        }
    }

    // The project's unit assignment, which every measure without a unit of its
    // own is in.
    for (const qint64 assignment : m_file.instancesOf("IFCUNITASSIGNMENT")) {
        for (const qint64 unit : refsAt(m_file.arguments(assignment), 0)) {
            const auto args = m_file.arguments(unit);
            const QByteArray type = m_file.type(unit);
            if (type == "IFCSIUNIT" && args.size() >= 4)
                m_units.symbols.insert(args.at(1).text, siSymbol(args.at(2).text, args.at(3).text));
            else if (type == "IFCCONVERSIONBASEDUNIT" && args.size() >= 3)
                m_units.symbols.insert(args.at(1).text, conversionSymbol(args.at(2).string()));
        }
    }
    const QHash<QString, QString> defaults = {
        {"LENGTHUNIT", "m"}, {"AREAUNIT", "m²"}, {"VOLUMEUNIT", "m³"}, {"MASSUNIT", "kg"},
        {"TIMEUNIT", "s"}, {"PLANEANGLEUNIT", "rad"}, {"THERMODYNAMICTEMPERATUREUNIT", "K"},
    };
    for (auto it = defaults.begin(); it != defaults.end(); ++it)
        if (!m_units.symbols.contains(it.key()))
            m_units.symbols.insert(it.key(), it.value());
}

QString IfcInfo::prettyClassName(const QByteArray &upper) {
    const QString known = classNames().value(upper);
    if (!known.isEmpty())
        return known;
    if (upper.startsWith("IFC") && upper.size() > 3)
        return "Ifc" + QString::fromLatin1(upper.mid(3, 1)) + QString::fromLatin1(upper.mid(4)).toLower();
    return QString::fromLatin1(upper);
}

QString IfcInfo::formatNumber(double value) {
    const QLocale locale;
    if (!std::isfinite(value))
        return locale.toString(value);
    if (std::abs(value - std::round(value)) < 1e-9 && std::abs(value) < 1e15)
        return locale.toString(qint64(std::llround(value)));
    if (std::abs(value) < 0.01)
        return locale.toString(value, 'g', 3);
    QString text = locale.toString(value, 'f', std::abs(value) >= 100 ? 2 : 3);
    // Trailing zeros after the decimal point say nothing, in any script.
    const QString point = locale.decimalPoint(), zero = locale.zeroDigit();
    if (text.contains(point)) {
        while (text.endsWith(zero))
            text.chop(zero.size());
        if (text.endsWith(point))
            text.chop(point.size());
    }
    return text;
}

QString IfcInfo::name(qint64 id) const {
    return attribute(m_file.arguments(id), 2);
}

QString IfcInfo::className(const QString &globalId) const {
    const qint64 id = m_file.idForGlobalId(globalId);
    return id ? prettyClassName(m_file.type(id)) : QString();
}

QString IfcInfo::storeyName(const QString &globalId) const {
    const qint64 storey = storeyOf(m_file.idForGlobalId(globalId));
    return storey ? name(storey) : QString();
}

qint64 IfcInfo::storeyOf(qint64 id) const {
    // An element sits in a storey directly, through a space, or as part of an
    // assembly (a roof's slabs, a stair's flights) that does.
    for (int depth = 0; id && depth < 64; ++depth) {
        if (m_file.type(id) == "IFCBUILDINGSTOREY")
            return id;
        const qint64 next = m_containedIn.value(id, 0);
        id = next ? next : m_parentOf.value(id, 0);
    }
    return 0;
}

QString IfcInfo::unitFor(const QString &measureType) const {
    const QString type = unitTypeOfMeasure(measureType);
    return type.isEmpty() ? derivedSymbol(measureType) : m_units.symbols.value(type);
}

QString IfcInfo::valueText(const Step::Value &value, const QString &unitHint) const {
    switch (value.kind) {
    case Step::Value::Null:
    case Step::Value::Derived:
        return QStringLiteral("—");
    case Step::Value::Typed: {
        if (value.items.isEmpty())
            return QString();
        const Step::Value &inner = value.items.first();
        if (inner.isNumber()) {
            const QString unit = unitHint.isEmpty() ? unitFor(value.text) : unitHint;
            return unit.isEmpty() ? formatNumber(inner.number) : formatNumber(inner.number) + " " + unit;
        }
        return valueText(inner, unitHint);
    }
    case Step::Value::Enum:
        if (value.text == "T" || value.text == "TRUE")
            return tr("Yes");
        if (value.text == "F" || value.text == "FALSE")
            return tr("No");
        if (value.text == "U" || value.text == "UNKNOWN")
            return tr("Unknown");
        return value.text;
    case Step::Value::Integer:
    case Step::Value::Real:
        return unitHint.isEmpty() ? formatNumber(value.number) : formatNumber(value.number) + " " + unitHint;
    case Step::Value::String:
    case Step::Value::Binary:
        return value.text;
    case Step::Value::Ref:
        return name(value.ref);
    case Step::Value::List: {
        QStringList parts;
        for (const Step::Value &item : value.items)
            parts.append(valueText(item, unitHint));
        return parts.join(", ");
    }
    }
    return QString();
}

QString IfcInfo::materialText(qint64 id, int depth) const {
    if (!id || depth > 8)
        return QString();
    const QByteArray type = m_file.type(id);
    const auto args = m_file.arguments(id);
    QStringList parts;
    auto add = [&parts](const QString &text) {
        if (!text.isEmpty() && !parts.contains(text))
            parts.append(text);
    };

    if (type == "IFCMATERIAL") {
        add(attribute(args, 0));
    } else if (type == "IFCMATERIALLIST") {
        for (const qint64 material : refsAt(args, 0))
            add(materialText(material, depth + 1));
    } else if (type == "IFCMATERIALLAYERSETUSAGE" || type == "IFCMATERIALPROFILESETUSAGE"
               || type == "IFCMATERIALPROFILESETUSAGETAPERING") {
        add(materialText(refAt(args, 0), depth + 1));
    } else if (type == "IFCMATERIALLAYERSET") {
        // Each layer with its thickness, outside to inside as the file lists them.
        for (const qint64 layer : refsAt(args, 0)) {
            const auto layerArgs = m_file.arguments(layer);
            const QString material = materialText(refAt(layerArgs, 0), depth + 1);
            if (layerArgs.size() > 1 && layerArgs.at(1).unwrapped().isNumber())
                add(tr("%1 (%2)").arg(material.isEmpty() ? tr("Layer") : material,
                                      formatNumber(layerArgs.at(1).unwrapped().number) + " "
                                          + m_units.symbols.value("LENGTHUNIT")));
            else
                add(material);
        }
    } else if (type == "IFCMATERIALCONSTITUENTSET") {
        for (const qint64 constituent : refsAt(args, 2))
            add(materialText(refAt(m_file.arguments(constituent), 2), depth + 1));
    } else if (type == "IFCMATERIALPROFILESET") {
        for (const qint64 profile : refsAt(args, 2))
            add(materialText(refAt(m_file.arguments(profile), 2), depth + 1));
    } else if (type == "IFCMATERIALCONSTITUENT" || type == "IFCMATERIALPROFILE") {
        add(materialText(refAt(args, 2), depth + 1));
    } else if (type == "IFCMATERIALLAYER") {
        add(materialText(refAt(args, 0), depth + 1));
    }
    return parts.join(", ");
}

QVector<IfcInfo::Section> IfcInfo::propertySets(qint64 definition) const {
    const QByteArray type = m_file.type(definition);
    const auto args = m_file.arguments(definition);
    Section section;
    section.title = attribute(args, 2);

    if (type == "IFCPROPERTYSET") {
        // Complex properties nest further properties; they show as
        // "Complex.Name" rows so nothing is hidden.
        std::function<void(qint64, const QString &, int)> addProperty = [&](qint64 property, const QString &prefix, int depth) {
            const QByteArray propertyType = m_file.type(property);
            const auto p = m_file.arguments(property);
            const QString label = prefix + attribute(p, 0);
            auto unitOf = [&](int index) { return unitSymbol(refAt(p, index)); };
            if (propertyType == "IFCPROPERTYSINGLEVALUE" && p.size() > 2) {
                section.rows.append({label, valueText(p.at(2), unitOf(3))});
            } else if ((propertyType == "IFCPROPERTYENUMERATEDVALUE" || propertyType == "IFCPROPERTYLISTVALUE") && p.size() > 2) {
                section.rows.append({label, valueText(p.at(2), propertyType == "IFCPROPERTYLISTVALUE" ? unitOf(3) : QString())});
            } else if (propertyType == "IFCPROPERTYBOUNDEDVALUE" && p.size() > 3) {
                const QString unit = unitOf(4);
                section.rows.append({label, tr("%1 to %2").arg(valueText(p.at(3), unit), valueText(p.at(2), unit))});
            } else if (propertyType == "IFCCOMPLEXPROPERTY" && depth < 4) {
                for (const qint64 child : refsAt(p, 3))
                    addProperty(child, label + ".", depth + 1);
            }
        };
        for (const qint64 property : refsAt(args, 4))
            addProperty(property, QString(), 0);
    } else if (type == "IFCELEMENTQUANTITY") {
        for (const qint64 quantity : refsAt(args, 5)) {
            const QString text = quantityText(quantity);
            if (!text.isEmpty())
                section.rows.append({attribute(m_file.arguments(quantity), 0), text});
        }
    } else {
        return {};
    }
    // Exporters write many properties with nothing in them; they say nothing.
    section.rows.erase(std::remove_if(section.rows.begin(), section.rows.end(),
                                      [](const Row &row) { return row.value.trimmed().isEmpty(); }),
                       section.rows.end());
    if (section.rows.isEmpty())
        return {};
    return {section};
}

QString IfcInfo::unitSymbol(qint64 unit) const {
    if (!unit)
        return QString();
    const auto u = m_file.arguments(unit);
    const QByteArray type = m_file.type(unit);
    if (type == "IFCSIUNIT" && u.size() >= 4)
        return siSymbol(u.at(2).text, u.at(3).text);
    if (type == "IFCCONVERSIONBASEDUNIT" && u.size() >= 3)
        return conversionSymbol(u.at(2).string());
    return QString();
}

QString IfcInfo::quantityText(qint64 quantity) const {
    static const QHash<QByteArray, QString> kinds = {
        {"IFCQUANTITYLENGTH", "LENGTHUNIT"}, {"IFCQUANTITYAREA", "AREAUNIT"},
        {"IFCQUANTITYVOLUME", "VOLUMEUNIT"}, {"IFCQUANTITYWEIGHT", "MASSUNIT"},
        {"IFCQUANTITYTIME", "TIMEUNIT"}, {"IFCQUANTITYCOUNT", QString()},
    };
    const QByteArray type = m_file.type(quantity);
    const auto q = m_file.arguments(quantity);
    if (!kinds.contains(type) || q.size() < 4 || !q.at(3).unwrapped().isNumber())
        return QString();
    // A quantity's own unit wins over the project's.
    QString unit = unitSymbol(refAt(q, 2));
    if (unit.isEmpty())
        unit = m_units.symbols.value(kinds.value(type));
    const QString number = formatNumber(q.at(3).unwrapped().number);
    return unit.isEmpty() ? number : number + " " + unit;
}

IfcInfo::Details IfcInfo::describe(const QString &globalId) const {
    return describe(globalId, Measured());
}

IfcInfo::Details IfcInfo::describe(const QString &globalId, const Measured &measured) const {
    Details details;
    const qint64 id = m_file.idForGlobalId(globalId);
    if (!id)
        return details;
    details.found = true;

    const QByteArray type = m_file.type(id);
    const auto args = m_file.arguments(id);
    details.ifcClass = prettyClassName(type);
    details.name = attribute(args, 2);

    auto add = [&details](const QString &label, const QString &value) {
        if (!value.trimmed().isEmpty())
            details.summary.append({label, value});
    };
    add(tr("Class"), details.ifcClass);
    add(tr("Description"), attribute(args, 3));
    add(tr("Object type"), attribute(args, 4));

    const qint64 typeId = m_typeOf.value(id, 0);
    if (typeId) {
        const QString typeName = name(typeId);
        add(tr("Type"), typeName.isEmpty() ? prettyClassName(m_file.type(typeId)) : typeName);
    }

    if (const qint64 storey = storeyOf(id)) {
        add(tr("Storey"), name(storey));
    } else {
        // Bounded like storeyOf(), so an aggregation cycle in a broken file
        // can't hang the window.
        qint64 container = m_containedIn.value(id, 0);
        qint64 up = m_parentOf.value(id, 0);
        for (int depth = 0; !container && up && depth < 64; ++depth, up = m_parentOf.value(up, 0))
            container = m_containedIn.value(up, 0);
        if (container)
            add(tr("Located in"), name(container));
    }

    QString material = materialText(m_materialOf.value(id, 0));
    if (material.isEmpty() && typeId)
        material = materialText(m_materialOf.value(typeId, 0));
    add(tr("Material"), material);

    // Instance property sets and quantities first, then the type's, which the
    // instance inherits unless it overrides them.
    QVector<Section> instanceSets, typeSets;
    for (const qint64 definition : m_definitions.value(id))
        instanceSets += propertySets(definition);
    if (typeId) {
        for (const qint64 definition : refsAt(m_file.arguments(typeId), 5))
            for (Section section : propertySets(definition)) {
                section.title = tr("%1 (type)").arg(section.title);
                typeSets += section;
            }
    }

    // The headline area and volume: the file's own quantities when it has
    // them, measured from the mesh otherwise, and said so.
    auto findQuantity = [&](const QByteArray &quantityType, const QStringList &preferred) -> QString {
        QString best;
        int bestRank = -1;
        for (const qint64 definition : m_definitions.value(id)) {
            if (m_file.type(definition) != "IFCELEMENTQUANTITY")
                continue;
            for (const qint64 quantity : refsAt(m_file.arguments(definition), 5)) {
                if (m_file.type(quantity) != quantityType)
                    continue;
                const QString text = quantityText(quantity);
                if (text.isEmpty())
                    continue;
                const auto q = m_file.arguments(quantity);
                // The most preferred name wins; any other name only fills in.
                const int rank = preferred.indexOf(attribute(q, 0));
                if (best.isEmpty() || (rank >= 0 && (bestRank < 0 || rank < bestRank))) {
                    best = text;
                    bestRank = rank;
                }
            }
        }
        return best;
    };
    const QString area = findQuantity("IFCQUANTITYAREA", {"NetArea", "NetSideArea", "GrossArea", "GrossSideArea",
                                                          "NetFloorArea", "GrossFloorArea", "NetSurfaceArea"});
    const QString volume = findQuantity("IFCQUANTITYVOLUME", {"NetVolume", "GrossVolume"});
    if (!area.isEmpty())
        add(tr("Area"), area);
    else if (measured.area >= 0)
        add(tr("Surface area (measured)"), formatNumber(measured.area) + " m²");
    if (!volume.isEmpty())
        add(tr("Volume"), volume);
    else if (measured.volume > 0)
        add(tr("Volume (measured)"), formatNumber(measured.volume) + " m³");

    add(isSpatial(type) ? tr("Long name") : tr("Tag"), attribute(args, 7));
    add(tr("GlobalId"), globalId);

    details.sections = instanceSets + typeSets;
    return details;
}
