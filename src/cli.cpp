#include "cli.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <cstdio>

#include "ifcinfo.h"

namespace {

void print(const QString &text) {
    fputs(text.toUtf8().constData(), stdout);
    fflush(stdout);
}

void printJson(const QJsonValue &value) {
    const QJsonDocument document = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    print(QString::fromUtf8(document.toJson(QJsonDocument::Indented)));
}

int fail(const QString &message) {
    fputs(("keystone: " + message + "\n").toUtf8().constData(), stderr);
    return 1;
}

QString usage() {
    return "Usage: keystone <command> [options]\n\n"
           "Commands:\n"
           "  open [model.ifc]                Open the viewer (also: keystone model.ifc)\n"
           "  info <model.ifc>                Schema, storeys, and element counts by class\n"
           "  elements <model.ifc>            Every element with its GlobalId, class, name, and storey\n"
           "      --class <IfcClass>          Only this class (IfcWall matches IfcWallStandardCase too)\n"
           "      --storey <name>             Only elements on this storey\n"
           "  properties <model.ifc> <GlobalId>\n"
           "                                  An element's type, storey, materials, quantities, and property sets\n"
           "  skill [install]                 Print the skill for coding agents, or install it\n"
           "  help                            This help\n\n"
           "info, elements, and properties take --json. Nothing needs a display, and\n"
           "nothing writes to the model.\n";
}

// The shared shape of a command: its options, a positional count, and the
// model it reads.
struct Command {
    QCommandLineParser parser;
    QString name;
    explicit Command(const QString &commandName) : name(commandName) {
        parser.addOption({"json", "Print JSON"});
        parser.addHelpOption();
    }
    bool parse(const QStringList &arguments, int positionals, QString *error) {
        // The command word stands in for the program name the parser skips.
        if (!parser.parse(arguments.mid(1))) {
            *error = parser.errorText();
            return false;
        }
        if (parser.positionalArguments().size() < positionals) {
            *error = QString("%1 needs %2.").arg(name, positionals == 1 ? "a model" : "a model and a GlobalId");
            return false;
        }
        return true;
    }
    bool json() const { return parser.isSet("json"); }
};

bool loadModel(const QString &path, IfcInfo *info, QString *error) {
    if (!QFileInfo(path).isFile()) {
        *error = QString("%1 doesn't exist.").arg(path);
        return false;
    }
    QString reason;
    if (!info->load(path, &reason)) {
        *error = QString("%1 can't be read as IFC: %2").arg(path, reason);
        return false;
    }
    return true;
}

QHash<qint64, QString> storeyNames(const IfcInfo &info) {
    QHash<qint64, QString> names;
    for (const IfcInfo::Storey &storey : info.storeys())
        names.insert(storey.id, storey.name);
    return names;
}

int info(const QStringList &arguments) {
    Command command("info");
    QString error;
    if (!command.parse(arguments, 1, &error))
        return fail(error);
    const QString path = command.parser.positionalArguments().at(0);
    IfcInfo model;
    if (!loadModel(path, &model, &error))
        return fail(error);

    const QVector<IfcInfo::Product> products = model.products();
    QMap<QString, int> classes;
    QHash<qint64, int> perStorey;
    for (const IfcInfo::Product &product : products) {
        ++classes[product.ifcClass];
        ++perStorey[product.storey];
    }
    // Most common first.
    QList<QPair<QString, int>> byCount;
    for (auto it = classes.begin(); it != classes.end(); ++it)
        byCount.append({it.key(), it.value()});
    std::stable_sort(byCount.begin(), byCount.end(), [](const auto &a, const auto &b) { return a.second > b.second; });

    if (command.json()) {
        QJsonArray storeys, classList;
        for (const IfcInfo::Storey &storey : model.storeys()) {
            QJsonObject object{{"name", storey.name}, {"elements", perStorey.value(storey.id)}};
            if (storey.hasElevation)
                object.insert("elevation", storey.elevation);
            storeys.append(object);
        }
        for (const auto &entry : byCount)
            classList.append(QJsonObject{{"class", entry.first}, {"count", entry.second}});
        printJson(QJsonObject{{"file", QFileInfo(path).fileName()}, {"schema", model.schema()},
                              {"elements", int(products.size())}, {"storeys", storeys}, {"classes", classList}});
        return 0;
    }
    QString text = QString("%1\n%2, %3 elements\n").arg(QFileInfo(path).fileName(), model.schema()).arg(products.size());
    if (!model.storeys().isEmpty()) {
        text += "\nStoreys\n";
        for (const IfcInfo::Storey &storey : model.storeys())
            text += QString("  %1  %2\n").arg(perStorey.value(storey.id), 6).arg(storey.name);
        if (perStorey.value(0))
            text += QString("  %1  (in no storey)\n").arg(perStorey.value(0), 6);
    }
    text += "\nClasses\n";
    for (const auto &entry : byCount)
        text += QString("  %1  %2\n").arg(entry.second, 6).arg(entry.first);
    print(text);
    return 0;
}

int elements(const QStringList &arguments) {
    Command command("elements");
    command.parser.addOption({"class", "Only this IFC class", "IfcClass"});
    command.parser.addOption({"storey", "Only this storey", "name"});
    QString error;
    if (!command.parse(arguments, 1, &error))
        return fail(error);
    const QString path = command.parser.positionalArguments().at(0);
    IfcInfo model;
    if (!loadModel(path, &model, &error))
        return fail(error);

    const QString wantedClass = command.parser.value("class"), wantedStorey = command.parser.value("storey");
    const QHash<qint64, QString> storeys = storeyNames(model);
    QJsonArray list;
    QString text;
    for (const IfcInfo::Product &product : model.products()) {
        if (!wantedClass.isEmpty() && !product.ifcClass.startsWith(wantedClass, Qt::CaseInsensitive))
            continue;
        const QString storey = storeys.value(product.storey);
        if (!wantedStorey.isEmpty() && storey.compare(wantedStorey, Qt::CaseInsensitive) != 0)
            continue;
        list.append(QJsonObject{{"globalId", product.globalId}, {"class", product.ifcClass},
                                {"name", product.name}, {"storey", storey}});
        text += QString("%1  %2  %3%4\n").arg(product.globalId, product.ifcClass.leftJustified(24), product.name,
                                              storey.isEmpty() ? QString() : "  [" + storey + "]");
    }
    if (command.json())
        printJson(list);
    else
        print(text);
    return 0;
}

int properties(const QStringList &arguments) {
    Command command("properties");
    QString error;
    if (!command.parse(arguments, 2, &error))
        return fail(error);
    const QString path = command.parser.positionalArguments().at(0);
    const QString globalId = command.parser.positionalArguments().at(1);
    IfcInfo model;
    if (!loadModel(path, &model, &error))
        return fail(error);
    const IfcInfo::Details details = model.describe(globalId);
    if (!details.found)
        return fail(QString("%1 has no element %2.").arg(QFileInfo(path).fileName(), globalId));

    if (command.json()) {
        QJsonObject summary;
        for (const IfcInfo::Row &row : details.summary)
            summary.insert(row.label, row.value);
        QJsonObject sections;
        for (const IfcInfo::Section &section : details.sections) {
            QJsonObject rows;
            for (const IfcInfo::Row &row : section.rows)
                rows.insert(row.label, row.value);
            sections.insert(section.title, rows);
        }
        printJson(QJsonObject{{"globalId", globalId}, {"class", details.ifcClass}, {"name", details.name},
                              {"summary", summary}, {"propertySets", sections}});
        return 0;
    }
    QString text = QString("%1  %2\n").arg(details.ifcClass, details.name);
    for (const IfcInfo::Row &row : details.summary)
        text += QString("  %1  %2\n").arg(row.label.leftJustified(14), row.value);
    for (const IfcInfo::Section &section : details.sections) {
        text += "\n" + section.title + "\n";
        for (const IfcInfo::Row &row : section.rows)
            text += QString("  %1  %2\n").arg(row.label.leftJustified(24), row.value);
    }
    print(text);
    return 0;
}

int skill(const QStringList &arguments) {
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return fail("The skill isn't bundled in this build.");
    const QByteArray text = bundled.readAll();
    if (arguments.size() < 3) {
        print(QString::fromUtf8(text));
        return 0;
    }
    if (arguments.at(2) != "install")
        return fail("Use keystone skill to print the skill, or keystone skill install.");

    // One copy under ~/.agents, linked into ~/.claude, as Hype installs its own.
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/keystone");
    if (!QDir().mkpath(directory))
        return fail("Could not create " + directory);
    QFile target(directory + "/SKILL.md");
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate) || target.write(text) != text.size())
        return fail("Could not write " + target.fileName());
    target.close();
    const QString link = home.filePath(".claude/skills/keystone");
    if (!QFileInfo(link).exists() && !QFileInfo(link).isSymLink()) {
        if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/keystone", link))
            return fail("Could not link " + link);
    }
    print("Installed the Keystone skill in " + directory + " and linked it into ~/.claude/skills.\n");
    return 0;
}

}

bool isCliCommand(const char *word) {
    return QStringList{"info", "elements", "properties", "skill", "help", "--help", "-h", "--version", "-v"}
        .contains(QString::fromUtf8(word));
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    if (command == "--version" || command == "-v") {
        print("keystone " + QCoreApplication::applicationVersion() + "\n");
        return 0;
    }
    if (command == "info")
        return info(arguments);
    if (command == "elements")
        return elements(arguments);
    if (command == "properties")
        return properties(arguments);
    if (command == "skill")
        return skill(arguments);
    print(usage());
    return 0;
}
