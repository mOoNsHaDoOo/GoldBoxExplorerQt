#include "widgets/characterview.h"

#include "parse/character_tables.h"

#include <QHeaderView>

#include <algorithm>
#include <QTableWidget>
#include <QVBoxLayout>

CharacterView::CharacterView(CharacterDocument document, QWidget *parent)
    : QTabWidget(parent)
{
    const QVector<Annotation> annotations = characterAnnotations(document.format);
    const QFont mono(QStringLiteral("Courier New"), 10);

    for (const CharacterRecord &record : document.records) {
        QVector<Annotation> sorted = annotations;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Annotation &a, const Annotation &b) { return a.addr > b.addr; });

        auto *table = new QTableWidget(this);
        table->setColumnCount(2);
        table->setHorizontalHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
        table->verticalHeader()->setVisible(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setAlternatingRowColors(true);
        table->setFont(mono);
        table->horizontalHeader()->setStretchLastSection(true);

        int extra = document.displayItemTypeName ? 1 : 0;
        table->setRowCount(sorted.size() + extra);
        int row = 0;

        if (document.displayItemTypeName) {
            auto *nameItem = new QTableWidgetItem(QStringLiteral("item template name"));
            auto *valueItem = new QTableWidgetItem(
                QString::fromLatin1(CharacterTables::itemTypes[
                    qBound(0, record.pageNumber, CharacterTables::itemTypesCount - 1)]));
            nameItem->setBackground(QColor(0xF0, 0xFF, 0xFF));
            valueItem->setBackground(QColor(0xF0, 0xFF, 0xFF));
            table->setItem(row, 0, nameItem);
            table->setItem(row, 1, valueItem);
            ++row;
        }

        for (const Annotation &annotation : sorted) {
            auto *nameItem = new QTableWidgetItem(annotation.description);
            auto *valueItem = new QTableWidgetItem(annotation.interpret(record.data));
            if (annotation.firstInRecord) {
                nameItem->setBackground(QColor(0xF0, 0xFF, 0xFF));
                valueItem->setBackground(QColor(0xF0, 0xFF, 0xFF));
            }
            table->setItem(row, 0, nameItem);
            table->setItem(row, 1, valueItem);
            ++row;
        }

        table->resizeColumnToContents(0);
        if (table->rowCount() > 0)
            table->setCurrentCell(0, 0);
        table->scrollToTop();
        addTab(table, record.name);
    }
}
