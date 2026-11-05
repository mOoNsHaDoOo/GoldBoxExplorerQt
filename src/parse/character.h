#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct Annotation
{
    int len = 0;
    int addr = 0;
    QString description;
    QString fieldType = QStringLiteral("number");
    bool duplicated = false;
    bool firstInRecord = false;
    bool isItem = false;

    QString interpret(const QByteArray &data) const;
};

enum class CharacterFormat
{
    Poolrad,
    Item,
    ItemTemplate,
    Spc,
    DaxSpc,
    DaxItem,
};

int characterRecordLength(CharacterFormat format);
QVector<Annotation> characterAnnotations(CharacterFormat format);
QString characterFormatName(CharacterFormat format);

struct CharacterRecord
{
    QString name;
    QByteArray data;
    int pageNumber = 0;
};

struct CharacterDocument
{
    QVector<CharacterRecord> records;
    CharacterFormat format = CharacterFormat::Poolrad;
    bool displayItemTypeName = false;
};

bool loadCharacterDocument(const QString &path, CharacterDocument &document, QString *error = nullptr);
