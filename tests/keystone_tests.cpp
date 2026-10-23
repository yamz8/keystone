#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cstdio>
#include <unistd.h>

#include "backend.h"
#include "cli.h"
#include "filepicker.h"
#include "ifcinfo.h"
#include "loader.h"
#include "mesh.h"
#include "step.h"
#include "theme.h"
#include "translator.h"

namespace {

// A small IFC4 model in millimetres: a wall of a named type on a storey,
// with a property set, base quantities, and a two-layer material.
const QByteArray sampleIfc = R"(ISO-10303-21;
HEADER;
FILE_DESCRIPTION(('ViewDefinition [ReferenceView]'),'2;1');
FILE_NAME('wall.ifc','2026-10-24T00:00:00',(''),(''),'test','test','');
FILE_SCHEMA(('IFC4'));
ENDSEC;
DATA;
#1=IFCPROJECT('0YvctVUKr0kugbFTf53O9L',$,'Project',$,$,$,$,$,#2);
#2=IFCUNITASSIGNMENT((#3,#4,#5));
#3=IFCSIUNIT(*,.LENGTHUNIT.,.MILLI.,.METRE.);
#4=IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.);
#5=IFCSIUNIT(*,.VOLUMEUNIT.,$,.CUBIC_METRE.);
#10=IFCBUILDINGSTOREY('2HnL8Sk2v2pBQHpMWrhu1Y',$,'Ground floor',$,$,$,$,$,.ELEMENT.,0.);
#11=IFCWALL('3vB2YO$MX4xv5uCqZZG05x',$,'Party wall','Between the flats','solid',$,#60,'W-01',.SOLIDWALL.);
#12=IFCRELCONTAINEDINSPATIALSTRUCTURE('1Fk0yz8Ub0Wf1h5IfvA3MX',$,$,$,(#11),#10);
#13=IFCBUILDINGSTOREY('2HnL8Sk2v2pBQHpMWrhu2Y',$,'First floor',$,$,$,$,$,.ELEMENT.,3000.);
#14=IFCSLAB('0tB4bfd1z0Q8Y2kBVXz0Zz',$,'Floor slab',$,$,$,#60,$,.FLOOR.);
#15=IFCRELCONTAINEDINSPATIALSTRUCTURE('1Fk0yz8Ub0Wf1h5IfvA3MY',$,$,$,(#14),#13);
#60=IFCPRODUCTDEFINITIONSHAPE($,$,());
#20=IFCWALLTYPE('0tB4bfd1z0Q8Y2kBVXz0Ab',$,'Brick 240',$,$,(#30),$,$,$,.SOLIDWALL.);
#21=IFCRELDEFINESBYTYPE('2rZ2Ng0rT4Hu7B0vUE3qkP',$,$,$,(#11),#20);
#30=IFCPROPERTYSET('1zyXNNdqf3wfSzlDa4D8qB',$,'Pset_WallCommon',$,(#31,#32,#33));
#31=IFCPROPERTYSINGLEVALUE('IsExternal',$,IFCBOOLEAN(.F.),$);
#32=IFCPROPERTYSINGLEVALUE('FireRating',$,IFCLABEL('REI 90'),$);
#33=IFCPROPERTYSINGLEVALUE('Width',$,IFCPOSITIVELENGTHMEASURE(240.),$);
#40=IFCELEMENTQUANTITY('3Ug8I4gYH5YhUcyRKnRx8N',$,'Qto_WallBaseQuantities',$,'BaseQuantities',(#41,#42,#43));
#41=IFCQUANTITYLENGTH('Length',$,$,5000.,$);
#42=IFCQUANTITYAREA('NetSideArea',$,$,12.5,$);
#43=IFCQUANTITYVOLUME('NetVolume',$,$,3.,$);
#44=IFCRELDEFINESBYPROPERTIES('0eU9HsX0D2zf4Dp1M4V4Xa',$,$,$,(#11),#40);
#50=IFCMATERIAL('Brick',$,$);
#51=IFCMATERIAL('Plaster \X2\00E9\X0\clat',$,$);
#52=IFCMATERIALLAYER(#50,225.,$,$,$,$,$);
#53=IFCMATERIALLAYER(#51,15.,$,$,$,$,$);
#54=IFCMATERIALLAYERSET((#52,#53),'Brick wall',$);
#55=IFCMATERIALLAYERSETUSAGE(#54,.AXIS2.,.POSITIVE.,0.,$);
#56=IFCRELASSOCIATESMATERIAL('2bH8yJ9Rv0kQk4aX4B7m2d',$,$,$,(#11),#55);
ENDSEC;
END-ISO-10303-21;
)";

// Two elements: a unit cube in a grey material and a small red cube beside
// it, in IFC's Z-up coordinates.
QByteArray cubeObj(const QString &mtl) {
    QByteArray obj = "mtllib " + mtl.toUtf8() + "\n";
    auto cube = [&obj](double x, double y, double z, double s, int base) {
        for (int i = 0; i < 8; ++i)
            obj += QByteArray("v ") + QByteArray::number(x + (i & 1 ? s : 0)) + " "
                   + QByteArray::number(y + (i & 2 ? s : 0)) + " " + QByteArray::number(z + (i & 4 ? s : 0)) + "\n";
        // Twelve outward triangles; 1-based, offset by the cube's first vertex.
        const int faces[12][3] = {{0, 2, 1}, {1, 2, 3}, {4, 5, 6}, {5, 7, 6}, {0, 1, 4}, {1, 5, 4},
                                  {2, 6, 3}, {3, 6, 7}, {0, 4, 2}, {2, 4, 6}, {1, 3, 5}, {3, 7, 5}};
        for (const auto &f : faces)
            obj += "f " + QByteArray::number(base + f[0] + 1) + " " + QByteArray::number(base + f[1] + 1) + " "
                   + QByteArray::number(base + f[2] + 1) + "\n";
    };
    obj += "g 3vB2YO$MX4xv5uCqZZG05x\nusemtl grey\n";
    cube(100, 200, 0, 1, 0);
    obj += "g 0tB4bfd1z0Q8Y2kBVXz0Zz\nusemtl red\n";
    cube(102, 200, 0, 0.5, 8);
    return obj;
}

const QByteArray cubeMtl = "newmtl grey\nKd 0.5 0.5 0.5\nnewmtl red\nKd 1 0 0\nd 0.25\n";

void write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

// A stand-in IfcConvert that takes `seconds`, draws a progress bar, copies
// the cube OBJ to the output path it's given, and counts its runs.
QString writeFakeIfcConvert(const QTemporaryDir &dir, double seconds = 0) {
    write(dir.filePath("fixture.mtl"), cubeMtl);
    write(dir.filePath("fixture.obj"), cubeObj("model.mtl"));
    const QString fake = dir.filePath("IfcConvert");
    write(fake, "#!/bin/sh\n"
                "echo run >> '" + dir.filePath("runs").toUtf8() + "'\n"
                "sleep " + QByteArray::number(seconds) + "\n"
                "for last; do :; done\n"
                "printf 'Creating geometry...\\n\\r[#########################                         ]'\n"
                "cp '" + dir.filePath("fixture.obj").toUtf8() + "' \"$last\"\n"
                "cp '" + dir.filePath("fixture.mtl").toUtf8() + "' \"$(dirname \"$last\")/model.mtl\"\n");
    QFile::setPermissions(fake, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return fake;
}

// Runs a command with stdout and stderr in files, and returns what it printed.
struct Captured {
    int code = 0;
    QString out, err;
};
Captured capture(const QStringList &arguments) {
    QTemporaryDir dir;
    fflush(stdout);
    fflush(stderr);
    const int savedOut = dup(fileno(stdout)), savedErr = dup(fileno(stderr));
    FILE *out = freopen(dir.filePath("out").toLocal8Bit().constData(), "w", stdout);
    FILE *err = freopen(dir.filePath("err").toLocal8Bit().constData(), "w", stderr);
    Q_UNUSED(out);
    Q_UNUSED(err);
    Captured result;
    result.code = runCli(QStringList{"keystone"} + arguments);
    fflush(stdout);
    fflush(stderr);
    dup2(savedOut, fileno(stdout));
    dup2(savedErr, fileno(stderr));
    close(savedOut);
    close(savedErr);
    auto read = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    };
    result.out = read(dir.filePath("out"));
    result.err = read(dir.filePath("err"));
    return result;
}

class FakePicker : public FilePicker {
public:
    QUrl next;
    void openModel() override { emit openSelected(next); }
};

}

class KeystoneTests : public QObject {
    Q_OBJECT

private slots:
    void init() {
        // Numbers format with the default locale; the tests expect C's.
        QLocale::setDefault(QLocale::c());
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
        QCOMPARE(theme.background(), QString("#0e0e10"));
        QVERIFY(!theme.light());
    }

    void themeReadsColors() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml",
              "mode = \"light\"\naccent = \"#112233\"\nbackground = \"#fafafa\"\nforeground = \"#202020\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
        QCOMPARE(theme.background(), QString("#fafafa"));
        QCOMPARE(theme.foreground(), QString("#202020"));
        QVERIFY(theme.light());
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Open model…").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Storey": "Piso", "Type": "Tipo"})");
        write(dir.path() + "/pt-BR.json", R"({"Storey": "Pavimento"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("IfcInfo", "Storey"), QString("Pavimento"));
        QCOMPARE(translator.translate("IfcInfo", "Type"), QString("Tipo"));
        QVERIFY(translator.translate("IfcInfo", "Material").isNull());
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n elements": {"one": "%n element", "other": "%n elements"}})");
        write(dir.path() + "/ru.json",
              R"({"%n elements": {"one": "%n элемент", "few": "%n элемента", "many": "%n элементов", "other": "%n элемента"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n elements", nullptr, 21), QString("%n элемент"));
        QCOMPARE(translator.translate("Main", "%n elements", nullptr, 22), QString("%n элемента"));
        QCOMPARE(translator.translate("Main", "%n elements", nullptr, 25), QString("%n элементов"));
    }

    void bundledCatalogsAreValid() {
        // Every catalog compiled in parses, and keeps the placeholders of
        // the English it translates.
        const QStringList catalogs = QDir(":/i18n").entryList({"*.json"});
        QVERIFY(catalogs.contains("en.json"));
        for (const QString &name : catalogs) {
            QFile file(":/i18n/" + name);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QJsonParseError error;
            const QJsonObject object = QJsonDocument::fromJson(file.readAll(), &error).object();
            QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(name + ": " + error.errorString()));
            for (auto it = object.begin(); it != object.end(); ++it) {
                if (!it.value().isString())
                    continue;
                for (const QString &placeholder : {QString("%1"), QString("%2")})
                    if (it.key().contains(placeholder))
                        QVERIFY2(it.value().toString().contains(placeholder),
                                 qPrintable(name + ": " + it.key()));
            }
        }
    }

    void stepDecodesStrings() {
        QCOMPARE(Step::decodeString("It''s"), QString("It's"));
        QCOMPARE(Step::decodeString("caf\\X2\\00E9\\X0\\"), QString("café"));
        QCOMPARE(Step::decodeString("\\X2\\00FC00DF\\X0\\e"), QString("üße"));
        QCOMPARE(Step::decodeString("na\\X\\EFve"), QString("naïve"));
        QCOMPARE(Step::decodeString("\\X4\\0001F3E0\\X0\\"), QString::fromUtf8("🏠"));
        QCOMPARE(Step::decodeString("a\\\\b"), QString("a\\b"));
    }

    void stepReadsLatin1Bytes() {
        // Raw ISO 8859-1 from an exporter that ignores the escapes, and raw
        // UTF-8 from one that writes it directly.
        QCOMPARE(Step::decodeString("W\xe4rmed\xe4mmung"), QString::fromUtf8("Wärmedämmung"));
        QCOMPARE(Step::decodeString("W\xc3\xa4rme"), QString::fromUtf8("Wärme"));
    }

    void stepParsesArguments() {
        const QByteArray text = "'abc',$,*,#12,.T.,(1,2.5,-3.E-2),IFCLABEL('x'),(#1,#2)";
        const Step::Arguments args = Step::parseArguments(text.constData(), text.constData() + text.size());
        QCOMPARE(args.size(), 8);
        QCOMPARE(args.at(0).kind, Step::Value::String);
        QCOMPARE(args.at(0).text, QString("abc"));
        QVERIFY(args.at(1).isNull());
        QCOMPARE(args.at(2).kind, Step::Value::Derived);
        QCOMPARE(args.at(3).ref, 12);
        QCOMPARE(args.at(4).text, QString("T"));
        QCOMPARE(args.at(5).items.size(), 3);
        QCOMPARE(args.at(5).items.at(2).number, -0.03);
        QCOMPARE(args.at(6).kind, Step::Value::Typed);
        QCOMPARE(args.at(6).string(), QString("x"));
        QCOMPARE(args.at(7).refs(), (QVector<qint64>{1, 2}));
    }

    void stepIndexesAFile() {
        Step::File file;
        QVERIFY(file.parse(sampleIfc));
        QCOMPARE(file.schema(), QString("IFC4"));
        QCOMPARE(file.type(11), QByteArray("IFCWALL"));
        QCOMPARE(file.idForGlobalId("3vB2YO$MX4xv5uCqZZG05x"), 11);
        QCOMPARE(file.instancesOf("IFCSIUNIT").size(), 3);
        QVERIFY(!file.parse("not a step file"));
    }

    void ifcInfoDescribesAnElement() {
        IfcInfo info;
        QVERIFY(info.parse(sampleIfc));
        const IfcInfo::Details details = info.describe("3vB2YO$MX4xv5uCqZZG05x");
        QVERIFY(details.found);
        QCOMPARE(details.ifcClass, QString("IfcWall"));
        QCOMPARE(details.name, QString("Party wall"));

        QHash<QString, QString> summary;
        for (const IfcInfo::Row &row : details.summary)
            summary.insert(row.label, row.value);
        QCOMPARE(summary.value("Type"), QString("Brick 240"));
        QCOMPARE(summary.value("Storey"), QString("Ground floor"));
        QCOMPARE(summary.value("Material"), QString("Brick (225 mm), Plaster éclat (15 mm)"));
        QCOMPARE(summary.value("Area"), QString("12.5 m²"));
        QCOMPARE(summary.value("Volume"), QString("3 m³"));
        QCOMPARE(summary.value("Tag"), QString("W-01"));
        QCOMPARE(summary.value("Object type"), QString("solid"));

        QHash<QString, QString> rows;
        QStringList titles;
        for (const IfcInfo::Section &section : details.sections) {
            titles.append(section.title);
            for (const IfcInfo::Row &row : section.rows)
                rows.insert(section.title + "/" + row.label, row.value);
        }
        QCOMPARE(titles, (QStringList{"Qto_WallBaseQuantities", "Pset_WallCommon (type)"}));
        QCOMPARE(rows.value("Qto_WallBaseQuantities/Length"), QString("5000 mm"));
        QCOMPARE(rows.value("Pset_WallCommon (type)/IsExternal"), QString("No"));
        QCOMPARE(rows.value("Pset_WallCommon (type)/FireRating"), QString("REI 90"));
        QCOMPARE(rows.value("Pset_WallCommon (type)/Width"), QString("240 mm"));
    }

    void ifcInfoSurvivesAggregationCycles() {
        // Two assemblies that each claim the other, in no container.
        const QByteArray cyclic = R"(ISO-10303-21;
HEADER;
FILE_SCHEMA(('IFC4'));
ENDSEC;
DATA;
#1=IFCELEMENTASSEMBLY('1AAAAAAAAAAAAAAAAAAAAA',$,'A',$,$,$,$,$,$,$);
#2=IFCELEMENTASSEMBLY('2BBBBBBBBBBBBBBBBBBBBB',$,'B',$,$,$,$,$,$,$);
#3=IFCRELAGGREGATES('3CCCCCCCCCCCCCCCCCCCCC',$,$,$,#1,(#2));
#4=IFCRELAGGREGATES('4DDDDDDDDDDDDDDDDDDDDD',$,$,$,#2,(#1));
ENDSEC;
END-ISO-10303-21;
)";
        IfcInfo info;
        QVERIFY(info.parse(cyclic));
        const IfcInfo::Details details = info.describe("1AAAAAAAAAAAAAAAAAAAAA");
        QVERIFY(details.found);
        QCOMPARE(details.name, QString("A"));
    }

    void ifcInfoUsesAQuantitysOwnUnit() {
        // A quantity in square feet, in a project measured in square metres.
        QByteArray feet = sampleIfc;
        feet.replace("#42=IFCQUANTITYAREA('NetSideArea',$,$,12.5,$);",
                     "#42=IFCQUANTITYAREA('NetSideArea',$,#45,120.,$);\n"
                     "#45=IFCCONVERSIONBASEDUNIT(#46,.AREAUNIT.,'square foot',#47);\n"
                     "#46=IFCDIMENSIONALEXPONENTS(2,0,0,0,0,0,0);\n"
                     "#47=IFCMEASUREWITHUNIT(IFCAREAMEASURE(0.0929),#4);");
        IfcInfo info;
        QVERIFY(info.parse(feet));
        const IfcInfo::Details details = info.describe("3vB2YO$MX4xv5uCqZZG05x");
        QString area, row;
        for (const IfcInfo::Row &r : details.summary)
            if (r.label == "Area")
                area = r.value;
        for (const IfcInfo::Section &section : details.sections)
            for (const IfcInfo::Row &r : section.rows)
                if (r.label == "NetSideArea")
                    row = r.value;
        QCOMPARE(area, QString("120 ft²"));
        QCOMPARE(row, QString("120 ft²"));
    }

    void ifcInfoListsProductsAndStoreys() {
        IfcInfo info;
        QVERIFY(info.parse(sampleIfc));
        const QVector<IfcInfo::Product> products = info.products();
        QCOMPARE(products.size(), 2);
        QCOMPARE(products.at(0).ifcClass, QString("IfcSlab"));
        QCOMPARE(products.at(1).name, QString("Party wall"));
        const QVector<IfcInfo::Storey> storeys = info.storeys();
        QCOMPARE(storeys.size(), 2);
        QCOMPARE(storeys.at(0).name, QString("Ground floor"));
        QCOMPARE(storeys.at(1).name, QString("First floor"));
        QCOMPARE(info.storeyIdOf("0tB4bfd1z0Q8Y2kBVXz0Zz"), storeys.at(1).id);
    }

    void ifcInfoSortsDisciplines() {
        QCOMPARE(IfcInfo::disciplinesOfClass("IfcColumn"), int(IfcInfo::Structure));
        QCOMPARE(IfcInfo::disciplinesOfClass("IfcFlowSegment"), int(IfcInfo::Services));
        QCOMPARE(IfcInfo::disciplinesOfClass("IfcDuctSegment"), int(IfcInfo::Services));
        QCOMPARE(IfcInfo::disciplinesOfClass("IfcDoor"), int(IfcInfo::Architecture));
        QCOMPARE(IfcInfo::disciplinesOfClass("IfcWallStandardCase", 1), int(IfcInfo::Structure));
        QCOMPARE(IfcInfo::disciplinesOfClass("IfcWall", 0), int(IfcInfo::Architecture));
        QCOMPARE(IfcInfo::disciplinesOfClass("IfcSlab"), IfcInfo::Structure | IfcInfo::Architecture);

        // The wall's type says it carries load.
        QByteArray loadBearing = sampleIfc;
        loadBearing.replace("#31=IFCPROPERTYSINGLEVALUE('IsExternal',$,IFCBOOLEAN(.F.),$);",
                            "#31=IFCPROPERTYSINGLEVALUE('LoadBearing',$,IFCBOOLEAN(.T.),$);");
        IfcInfo info;
        QVERIFY(info.parse(loadBearing));
        QCOMPARE(info.disciplines("3vB2YO$MX4xv5uCqZZG05x"), int(IfcInfo::Structure));
    }

    void ifcInfoFallsBackToMeasuredQuantities() {
        // The same wall with its quantities left out.
        QByteArray withoutQuantities = sampleIfc;
        withoutQuantities.replace("#44=IFCRELDEFINESBYPROPERTIES", "#44=IFCRELNOTHING");
        IfcInfo info;
        QVERIFY(info.parse(withoutQuantities));
        IfcInfo::Measured measured;
        measured.area = 6;
        measured.volume = 1;
        QHash<QString, QString> summary;
        for (const IfcInfo::Row &row : info.describe("3vB2YO$MX4xv5uCqZZG05x", measured).summary)
            summary.insert(row.label, row.value);
        QCOMPARE(summary.value("Surface area (measured)"), QString("6 m²"));
        QCOMPARE(summary.value("Volume (measured)"), QString("1 m³"));
        QVERIFY(!summary.contains("Area"));
    }

    void ifcInfoNamesClasses() {
        QCOMPARE(IfcInfo::prettyClassName("IFCWALLSTANDARDCASE"), QString("IfcWallStandardCase"));
        QCOMPARE(IfcInfo::prettyClassName("IFCBUILDINGSTOREY"), QString("IfcBuildingStorey"));
        QCOMPARE(IfcInfo::prettyClassName("IFCMADEUPTHING"), QString("IfcMadeupthing"));
    }

    void ifcInfoFormatsNumbers() {
        QLocale::setDefault(QLocale::c());
        QCOMPARE(IfcInfo::formatNumber(3), QString("3"));
        QCOMPARE(IfcInfo::formatNumber(6.4375000000003784), QString("6.438"));
        QCOMPARE(IfcInfo::formatNumber(250.123), QString("250.12"));
        QCOMPARE(IfcInfo::formatNumber(0.0012), QString("0.0012"));
        QLocale::setDefault(QLocale(QLocale::German));
        QCOMPARE(IfcInfo::formatNumber(1.5), QString("1,5"));
        QLocale::setDefault(QLocale::c());
    }

    void meshReadsGroupsAndMaterials() {
        QTemporaryDir dir;
        write(dir.filePath("m.mtl"), cubeMtl);
        write(dir.filePath("m.obj"), cubeObj("m.mtl"));
        Mesh mesh;
        QVERIFY(readObj(dir.filePath("m.obj"), &mesh));
        QCOMPARE(mesh.elements.size(), 2);
        QCOMPARE(mesh.elements.at(0).globalId, QString("3vB2YO$MX4xv5uCqZZG05x"));
        QCOMPARE(mesh.positions.size(), 72);
        QCOMPARE(mesh.materials.size(), 2);
        QCOMPARE(mesh.materials.at(1).color, QColor(255, 0, 0));
        QCOMPARE(mesh.materials.at(1).opacity, 0.25f);
        QCOMPARE(mesh.elements.at(1).parts.first().material, 1);

        // Centred on the model, and Z up became Y up: the cubes stand 1 and
        // 0.5 tall along Y, side by side along X.
        QCOMPARE(mesh.originX, 101.25);
        QVERIFY(qFuzzyCompare(mesh.max.y() - mesh.min.y(), 1.0f));
        QVERIFY(qFuzzyCompare(mesh.elements.at(0).max.y() - mesh.elements.at(0).min.y(), 1.0f));
        QVERIFY(qFuzzyCompare(mesh.elements.at(1).max.y() - mesh.elements.at(1).min.y(), 0.5f));
        QVERIFY(mesh.elements.at(0).max.x() < mesh.elements.at(1).min.x());
    }

    void meshPicksTheNearestElement() {
        QTemporaryDir dir;
        write(dir.filePath("m.mtl"), cubeMtl);
        write(dir.filePath("m.obj"), cubeObj("m.mtl"));
        Mesh mesh;
        QVERIFY(readObj(dir.filePath("m.obj"), &mesh));

        // A ray along +X at the small cube's height passes through the big
        // cube first.
        const float y = mesh.elements.at(1).min.y() + 0.25f;
        const float z = (mesh.elements.at(1).min.z() + mesh.elements.at(1).max.z()) / 2;
        Mesh::Hit hit = mesh.pick(QVector3D(-50, y, z), QVector3D(1, 0, 0));
        QCOMPARE(hit.element, 0);
        QVERIFY(qAbs(hit.point.x() - mesh.elements.at(0).min.x()) < 1e-4f);
        // From the other side, the small cube is first.
        hit = mesh.pick(QVector3D(50, y, z), QVector3D(-1, 0, 0));
        QCOMPARE(hit.element, 1);
        // Above both, nothing.
        QCOMPARE(mesh.pick(QVector3D(-50, 10, z), QVector3D(1, 0, 0)).element, -1);
    }

    void meshPicksAcrossManyElements() {
        // A grid of cubes, enough for the picking tree to split many times:
        // a ray straight down onto each cube finds that one.
        QTemporaryDir dir;
        QByteArray obj;
        int base = 0;
        for (int x = 0; x < 15; ++x)
            for (int y = 0; y < 15; ++y) {
                obj += "g element-" + QByteArray::number(x * 15 + y) + "\n";
                for (int i = 0; i < 8; ++i)
                    obj += "v " + QByteArray::number(x * 2 + (i & 1 ? 1 : 0)) + " "
                           + QByteArray::number(y * 2 + (i & 2 ? 1 : 0)) + " " + QByteArray::number(i & 4 ? 1 : 0) + "\n";
                const int faces[12][3] = {{0, 2, 1}, {1, 2, 3}, {4, 5, 6}, {5, 7, 6}, {0, 1, 4}, {1, 5, 4},
                                          {2, 6, 3}, {3, 6, 7}, {0, 4, 2}, {2, 4, 6}, {1, 3, 5}, {3, 7, 5}};
                for (const auto &f : faces)
                    obj += "f " + QByteArray::number(base + f[0] + 1) + " " + QByteArray::number(base + f[1] + 1) + " "
                           + QByteArray::number(base + f[2] + 1) + "\n";
                base += 8;
            }
        write(dir.filePath("grid.obj"), obj);
        Mesh mesh;
        QVERIFY(readObj(dir.filePath("grid.obj"), &mesh));
        QCOMPARE(mesh.elements.size(), 225);
        QVERIFY(mesh.nodes.size() > 50);
        for (int e = 0; e < mesh.elements.size(); ++e) {
            const QVector3D c = (mesh.elements.at(e).min + mesh.elements.at(e).max) / 2;
            const Mesh::Hit hit = mesh.pick(c + QVector3D(0, 50, 0), QVector3D(0, -1, 0));
            QCOMPARE(mesh.elements.at(hit.element).globalId, mesh.elements.at(e).globalId);
            QVERIFY(qAbs(hit.point.y() - mesh.elements.at(e).max.y()) < 1e-4f);
        }
        // Between the cubes, nothing.
        const Mesh::Element &first = mesh.elements.at(0), &next = mesh.elements.at(15);
        const QVector3D gap((first.max.x() + next.min.x()) / 2, 50, (first.min.z() + first.max.z()) / 2);
        QCOMPARE(mesh.pick(gap, QVector3D(0, -1, 0)).element, -1);
    }

    void meshMeasuresElements() {
        QTemporaryDir dir;
        write(dir.filePath("m.mtl"), cubeMtl);
        write(dir.filePath("m.obj"), cubeObj("m.mtl"));
        Mesh mesh;
        QVERIFY(readObj(dir.filePath("m.obj"), &mesh));
        QVERIFY(qAbs(mesh.volume(0) - 1.0) < 1e-4);
        QVERIFY(qAbs(mesh.surfaceArea(0) - 6.0) < 1e-4);
        QVERIFY(qAbs(mesh.volume(1) - 0.125) < 1e-4);
    }

    void meshFocusLeavesOutOutliers() {
        Mesh mesh;
        auto add = [&mesh](const QVector3D &min, const QVector3D &max) {
            Mesh::Element element;
            element.min = min;
            element.max = max;
            mesh.elements.append(element);
        };
        for (int i = 0; i < 10; ++i)
            add(QVector3D(i, 0, 0), QVector3D(i + 1, 3, 1));
        add(QVector3D(900, 0, 900), QVector3D(901, 1, 901));
        mesh.min = QVector3D(0, 0, 0);
        mesh.max = QVector3D(901, 3, 901);
        QVector3D min, max;
        mesh.focusBounds(&min, &max);
        QCOMPARE(min, QVector3D(0, 0, 0));
        QCOMPARE(max, QVector3D(10, 3, 1));
    }

    void loaderReadsProgress() {
        QCOMPARE(Loader::parseProgress("Creating geometry...\n\r[#########################                         ]"), 0.5);
        QCOMPARE(Loader::parseProgress("\r[##########"), 0.2);
        QCOMPARE(Loader::parseProgress("Scanning file..."), -1.0);
        QCOMPARE(Loader::parseProgress("[Error] [GEO313] something"), -1.0);
    }

    void loaderSaysWhenIfcConvertIsMissing() {
        QTemporaryDir dir;
        write(dir.filePath("wall.ifc"), sampleIfc);
        qputenv("KEYSTONE_IFCCONVERT", dir.filePath("missing").toUtf8());
        Backend backend(new FakePicker(), dir.filePath("cache"));
        QVERIFY(backend.openPath(dir.filePath("wall.ifc")));
        QTRY_COMPARE(backend.state(), Backend::Failed);
        QVERIFY(backend.errorMessage().contains("IfcConvert"));
        qunsetenv("KEYSTONE_IFCCONVERT");
    }

    void backendOpensConvertsAndCaches() {
        QTemporaryDir dir;
        write(dir.filePath("wall.ifc"), sampleIfc);
        const QString fake = writeFakeIfcConvert(dir);
        qputenv("KEYSTONE_IFCCONVERT", fake.toUtf8());

        auto *picker = new FakePicker();
        picker->next = QUrl::fromLocalFile(dir.filePath("wall.ifc"));
        Backend backend(picker, dir.filePath("cache"));
        QSignalSpy progress(&backend, &Backend::loadProgressChanged);
        backend.open();
        QCOMPARE(backend.state(), Backend::Loading);
        QTRY_COMPARE(backend.state(), Backend::Ready);
        QVERIFY(progress.count() > 0);
        QCOMPARE(backend.elementCount(), 2);
        QCOMPARE(backend.schema(), QString("IFC4"));
        QCOMPARE(backend.fileName(), QString("wall.ifc"));
        QCOMPARE(backend.materials().size(), 2);
        QVERIFY(backend.errorMessage().isEmpty());

        // Each mesh has its element's IFC properties.
        backend.select(0);
        QCOMPARE(backend.selection().value("ifcClass").toString(), QString("IfcWall"));
        QCOMPARE(backend.selection().value("name").toString(), QString("Party wall"));
        QVERIFY(backend.selection().value("sections").toList().size() == 2);
        backend.select(1);
        QCOMPARE(backend.selection().value("ifcClass").toString(), QString("IfcSlab"));
        backend.clearSelection();
        QCOMPARE(backend.selectedElement(), -1);

        // A ray down onto the first element selects it.
        const QVector3D c = backend.mesh()->elements.at(0).min + QVector3D(0.5f, 0, 0.5f);
        backend.selectAt(c + QVector3D(0, 10, 0), c);
        QCOMPARE(backend.selectedElement(), 0);

        // The same file again comes from the cache, without converting.
        QVERIFY(backend.openPath(dir.filePath("wall.ifc")));
        QTRY_COMPARE(backend.state(), Backend::Ready);
        QFile runs(dir.filePath("runs"));
        QVERIFY(runs.open(QIODevice::ReadOnly));
        QCOMPARE(runs.readAll().count('\n'), 1);
        QCOMPARE(backend.selectedElement(), -1);
        qunsetenv("KEYSTONE_IFCCONVERT");
    }

    void backendPeelsStoreysXraysAndCuts() {
        QTemporaryDir dir;
        write(dir.filePath("wall.ifc"), sampleIfc);
        qputenv("KEYSTONE_IFCCONVERT", writeFakeIfcConvert(dir).toUtf8());
        Backend backend(new FakePicker(), dir.filePath("cache"));
        QVERIFY(backend.openPath(dir.filePath("wall.ifc")));
        QTRY_COMPARE(backend.state(), Backend::Ready);
        qunsetenv("KEYSTONE_IFCCONVERT");
        QCOMPARE(backend.storeys(), (QStringList{"Ground floor", "First floor"}));
        QCOMPARE(backend.storeyLimit(), 2);
        QCOMPARE(backend.shownCount(), 2);
        const int wall = backend.mesh()->indexOf("3vB2YO$MX4xv5uCqZZG05x");
        const int slab = backend.mesh()->indexOf("0tB4bfd1z0Q8Y2kBVXz0Zz");
        QCOMPARE(backend.storeyOf(slab), 1);

        // Peeling the first floor away hides the slab, and it can't be picked.
        QSignalSpy visibility(&backend, &Backend::visibilityChanged);
        backend.select(slab);
        backend.setStoreyLimit(1);
        QCOMPARE(visibility.count(), 1);
        QCOMPARE(backend.visibility().at(slab), quint8(Backend::Hidden));
        QCOMPARE(backend.selectedElement(), -1);
        backend.select(slab);
        QCOMPARE(backend.selectedElement(), -1);
        backend.setStoreyLimit(0);
        QCOMPARE(backend.storeyLimit(), 1);
        backend.setStoreyLimit(2);

        // X-ray for services ghosts both, so clicks pass through them.
        backend.setXray(Backend::Services);
        QCOMPARE(backend.shownCount(), 0);
        QCOMPARE(backend.visibility().at(wall), quint8(Backend::Ghosted));
        const QVector3D top = backend.mesh()->elements.at(wall).max + QVector3D(-0.5f, 0, -0.5f);
        backend.selectAt(top + QVector3D(0, 10, 0), top);
        QCOMPARE(backend.selectedElement(), -1);
        // The tree can still pick a ghosted element.
        backend.select(wall);
        QCOMPARE(backend.selectedElement(), wall);
        backend.setXray(Backend::Architecture);
        QCOMPARE(backend.shownCount(), 2);
        backend.cycleXray();
        QCOMPARE(backend.xray(), Backend::All);

        // A plan section at the bottom cuts the whole wall away; at the top it
        // keeps it all.
        backend.clearSelection();
        backend.setSectionAxis(Backend::Plan);
        backend.setSectionPosition(0);
        backend.selectAt(top + QVector3D(0, 10, 0), top);
        QCOMPARE(backend.selectedElement(), -1);
        backend.setSectionPosition(1);
        backend.selectAt(top + QVector3D(0, 10, 0), top);
        QCOMPARE(backend.selectedElement(), wall);
        QCOMPARE(backend.sectionPlane().y(), 1.0f);
        backend.setSectionFlipped(true);
        QCOMPARE(backend.sectionPlane().y(), -1.0f);

        // The element list, storey by storey, narrowed by a search.
        ElementList *elements = backend.elements();
        QCOMPARE(elements->total(), 2);
        QCOMPARE(elements->data(elements->index(0), ElementList::NameRole).toString(), QString("Party wall"));
        QCOMPARE(elements->data(elements->index(1), ElementList::StoreyRole).toString(), QString("First floor"));
        elements->setFilter("slab first");
        QCOMPARE(elements->rowCount(), 1);
        QCOMPARE(elements->rowOf(slab), 0);
        QCOMPARE(elements->rowOf(wall), -1);
        elements->setFilter("nothing like it");
        QCOMPARE(elements->rowCount(), 0);
    }

    void backendKeepsLoadingThroughAFailedOpen() {
        // A second open that fails while the first is converting leaves the
        // first alone: still loading, and named right once it's done.
        QTemporaryDir dir;
        write(dir.filePath("wall.ifc"), sampleIfc);
        qputenv("KEYSTONE_IFCCONVERT", writeFakeIfcConvert(dir, 0.5).toUtf8());
        Backend backend(new FakePicker(), dir.filePath("cache"));
        QSignalSpy notices(&backend, &Backend::notice);
        QVERIFY(backend.openPath(dir.filePath("wall.ifc")));
        QVERIFY(!backend.openPath(dir.filePath("gone.ifc")));
        QVERIFY(!backend.openUrl(QUrl("https://example.com/x.ifc")));
        QCOMPARE(backend.state(), Backend::Loading);
        QCOMPARE(notices.count(), 2);
        QCOMPARE(notices.at(0).at(0).toString(), QString("gone.ifc doesn't exist."));
        QTRY_COMPARE_WITH_TIMEOUT(backend.state(), Backend::Ready, 10000);
        QCOMPARE(backend.fileName(), QString("wall.ifc"));
        QCOMPARE(backend.filePath(), dir.filePath("wall.ifc"));
        qunsetenv("KEYSTONE_IFCCONVERT");
    }

    void loaderDropsACancelledLoad() {
        // Starting again while a conversion runs neither waits for it nor
        // reports it.
        QTemporaryDir dir;
        write(dir.filePath("a.ifc"), sampleIfc);
        write(dir.filePath("b.ifc"), sampleIfc + "\n");
        qputenv("KEYSTONE_IFCCONVERT", writeFakeIfcConvert(dir, 0.3).toUtf8());
        Loader loader(dir.filePath("cache"));
        QSignalSpy finished(&loader, &Loader::finished);
        QSignalSpy failed(&loader, &Loader::failed);
        loader.start(dir.filePath("a.ifc"));
        QElapsedTimer timer;
        timer.start();
        loader.start(dir.filePath("b.ifc"));
        QVERIFY(timer.elapsed() < 250);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QTest::qWait(500);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(finished.at(0).at(0).value<Loader::Result>().path, dir.filePath("b.ifc"));
        qunsetenv("KEYSTONE_IFCCONVERT");
    }

    void cliReportsOnAModel() {
        QTemporaryDir dir;
        write(dir.filePath("wall.ifc"), sampleIfc);
        const QString model = dir.filePath("wall.ifc");

        Captured info = capture({"info", model, "--json"});
        QCOMPARE(info.code, 0);
        const QJsonObject summary = QJsonDocument::fromJson(info.out.toUtf8()).object();
        QCOMPARE(summary.value("schema").toString(), QString("IFC4"));
        QCOMPARE(summary.value("elements").toInt(), 2);
        QCOMPARE(summary.value("storeys").toArray().at(1).toObject().value("name").toString(), QString("First floor"));

        Captured walls = capture({"elements", model, "--class", "IfcWall", "--json"});
        const QJsonArray list = QJsonDocument::fromJson(walls.out.toUtf8()).array();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).toObject().value("globalId").toString(), QString("3vB2YO$MX4xv5uCqZZG05x"));
        QCOMPARE(list.at(0).toObject().value("storey").toString(), QString("Ground floor"));

        Captured wall = capture({"properties", model, "3vB2YO$MX4xv5uCqZZG05x", "--json"});
        const QJsonObject properties = QJsonDocument::fromJson(wall.out.toUtf8()).object();
        QCOMPARE(properties.value("summary").toObject().value("Material").toString(),
                 QString("Brick (225 mm), Plaster éclat (15 mm)"));
        QCOMPARE(properties.value("propertySets").toObject().value("Qto_WallBaseQuantities").toObject()
                     .value("Length").toString(), QString("5000 mm"));

        Captured missing = capture({"properties", model, "nope"});
        QCOMPARE(missing.code, 1);
        QVERIFY(missing.err.contains("has no element nope"));
        QVERIFY(missing.out.isEmpty());
        QCOMPARE(capture({"info"}).code, 1);
        QVERIFY(capture({"help"}).out.contains("properties <model.ifc> <GlobalId>"));
        QVERIFY(capture({"skill"}).out.contains("name: keystone"));
    }

    void loaderKeepsTheCacheSmall() {
        QTemporaryDir dir;
        qputenv("KEYSTONE_IFCCONVERT", writeFakeIfcConvert(dir).toUtf8());
        const QString cache = dir.filePath("cache");
        auto entries = [&cache] { return QDir(cache).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(); };
        Loader loader(cache);
        loader.setCacheLimits(2, qint64(1) << 30);
        for (int i = 0; i < 3; ++i) {
            const QString model = dir.filePath(QString("m%1.ifc").arg(i));
            write(model, sampleIfc);
            QSignalSpy finished(&loader, &Loader::finished);
            loader.start(model);
            QTRY_COMPARE(finished.count(), 1);
        }
        QCOMPARE(entries(), 2);
        // Too little room for even one more: only the newest stays.
        loader.setCacheLimits(12, 1);
        write(dir.filePath("m3.ifc"), sampleIfc);
        QSignalSpy finished(&loader, &Loader::finished);
        loader.start(dir.filePath("m3.ifc"));
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(entries(), 1);
        qunsetenv("KEYSTONE_IFCCONVERT");
    }

    void backendRefusesWhatItCantOpen() {
        QTemporaryDir dir;
        Backend backend(new FakePicker(), dir.filePath("cache"));
        QVERIFY(!backend.openPath(dir.filePath("nowhere.ifc")));
        QCOMPARE(backend.state(), Backend::Failed);
        QCOMPARE(backend.errorMessage(), QString("nowhere.ifc doesn't exist."));
        QVERIFY(!backend.openUrl(QUrl("https://example.com/model.ifc")));
        QVERIFY(backend.errorMessage().contains("this computer"));
    }
};

QTEST_MAIN(KeystoneTests)
#include "keystone_tests.moc"
