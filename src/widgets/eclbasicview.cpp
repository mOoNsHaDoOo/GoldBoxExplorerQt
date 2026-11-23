#include "widgets/eclbasicview.h"

#include "parse/ecl_basic.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QToolTip>
#include <QVBoxLayout>

namespace {

bool isWordChar(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

QFont listingFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(qMax(font.pointSize(), 10));
    return font;
}

// Syntax colours for ECL Basic: directives, labels, commands, strings,
// comments, [variables], numbers and references to labels/tables.  Strings
// may contain ';', so each line is scanned rather than matched with regexes.
class EclBasicHighlighter : public QSyntaxHighlighter
{
public:
    EclBasicHighlighter(QTextDocument *document, const QHash<QString, int> &definitions)
        : QSyntaxHighlighter(document)
        , m_definitions(definitions)
    {
        m_comment.setForeground(QColor(0x6A, 0x73, 0x7D));
        m_comment.setFontItalic(true);
        m_directive.setForeground(QColor(0x8E, 0x24, 0xAA));
        m_directive.setFontWeight(QFont::Bold);
        m_label.setForeground(QColor(0xB0, 0x3A, 0x00));
        m_label.setFontWeight(QFont::Bold);
        m_command.setForeground(QColor(0x00, 0x4E, 0xB4));
        m_command.setFontWeight(QFont::Bold);
        m_string.setForeground(QColor(0x1A, 0x7F, 0x37));
        m_variable.setForeground(QColor(0x00, 0x7A, 0x87));
        m_number.setForeground(QColor(0x9A, 0x67, 0x00));
        m_reference.setForeground(QColor(0xB0, 0x3A, 0x00));
        m_reference.setFontUnderline(true);
        m_role.setForeground(QColor(0x8E, 0x24, 0xAA));
    }

protected:
    void highlightBlock(const QString &text) override
    {
        const int n = text.size();
        int i = 0;
        while (i < n && text.at(i).isSpace())
            ++i;
        const int indent = i;
        if (i >= n)
            return;

        // "name:" label definition
        static const QRegularExpression labelDef(QStringLiteral("^([A-Za-z_]\\w*):\\s*$"));
        if (labelDef.match(text).hasMatch()) {
            setFormat(0, n, m_label);
            return;
        }

        bool firstWord = true;
        int wordIndex = 0;
        const bool entryLine = text.startsWith(QLatin1String("entry "));
        while (i < n) {
            const QChar ch = text.at(i);
            if (ch == QLatin1Char(';')) {
                setFormat(i, n - i, m_comment);
                return;
            }
            if (ch == QLatin1Char('"')) {
                int j = text.indexOf(QLatin1Char('"'), i + 1);
                j = j < 0 ? n : j + 1;
                setFormat(i, j - i, m_string);
                i = j;
                firstWord = false;
                continue;
            }
            if (ch == QLatin1Char('[')) {
                int start = i;
                if (start >= 3 && text.mid(start - 3, 3) == QLatin1String("str"))
                    start -= 3;
                else if (start >= 2 && text.mid(start - 2, 2) == QLatin1String("v3"))
                    start -= 2;
                int j = text.indexOf(QLatin1Char(']'), i + 1);
                j = j < 0 ? n : j + 1;
                setFormat(start, j - start, m_variable);
                i = j;
                firstWord = false;
                continue;
            }
            if (ch == QLatin1Char('$') || ch == QLatin1Char('#') || ch.isDigit()) {
                int j = i + 1;
                while (j < n && text.at(j).isLetterOrNumber())
                    ++j;
                setFormat(i, j - i, m_number);
                i = j;
                firstWord = false;
                continue;
            }
            if (isWordChar(ch)) {
                int j = i;
                while (j < n && isWordChar(text.at(j)))
                    ++j;
                const QString word = text.mid(i, j - i);
                if (j < n && text.at(j) == QLatin1Char('[')) {
                    i = j; // str[...] / v3[...]: coloured with the bracket
                    continue;
                }
                if (firstWord && indent == 0
                    && (word == QLatin1String("program") || word == QLatin1String("entry")
                        || word == QLatin1String("table"))) {
                    setFormat(i, j - i, m_directive);
                } else if (firstWord && word == QLatin1String("db")) {
                    setFormat(i, j - i, m_directive);
                } else if (firstWord && indent > 0) {
                    setFormat(i, j - i, m_command);
                } else if (entryLine && wordIndex == 1) {
                    setFormat(i, j - i, m_role); // entry role, not a label
                } else if (m_definitions.contains(word)) {
                    setFormat(i, j - i, m_reference);
                }
                firstWord = false;
                ++wordIndex;
                i = j;
                continue;
            }
            ++i;
        }
    }

private:
    QHash<QString, int> m_definitions;
    QTextCharFormat m_comment;
    QTextCharFormat m_directive;
    QTextCharFormat m_label;
    QTextCharFormat m_command;
    QTextCharFormat m_string;
    QTextCharFormat m_variable;
    QTextCharFormat m_number;
    QTextCharFormat m_reference;
    QTextCharFormat m_role;
};

struct Definition
{
    QString name;
    QString detail;
    int block = 0;
};

QVector<Definition> scanDefinitions(const QString &text)
{
    static const QRegularExpression labelDef(QStringLiteral("^([A-Za-z_]\\w*):\\s*$"));
    static const QRegularExpression tableDef(QStringLiteral("^table\\s+(\\S+)\\s*(?:;\\s*(.*))?$"));
    static const QRegularExpression entryDef(QStringLiteral("^entry\\s+(\\S+)\\s+(\\S+)"));

    QHash<QString, QStringList> roles;
    QVector<Definition> result;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        QRegularExpressionMatch m = entryDef.match(line);
        if (m.hasMatch()) {
            roles[m.captured(2)].append(m.captured(1));
            continue;
        }
        m = labelDef.match(line);
        if (m.hasMatch()) {
            result.append({m.captured(1), QString(), i});
            continue;
        }
        m = tableDef.match(line);
        if (m.hasMatch())
            result.append({m.captured(1), m.captured(2), i});
    }
    for (Definition &d : result) {
        if (roles.contains(d.name))
            d.detail = QStringLiteral("entry: %1").arg(roles.value(d.name).join(QStringLiteral(", ")));
    }
    return result;
}

} // namespace

EclBasicEdit::EclBasicEdit(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(listingFont());
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
}

bool EclBasicEdit::jumpTo(const QString &name)
{
    const auto it = m_definitions.constFind(name);
    if (it == m_definitions.constEnd())
        return false;
    const QTextBlock block = document()->findBlockByNumber(it.value());
    if (!block.isValid())
        return false;
    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    setTextCursor(cursor);
    // Put the definition near the top of the view (the scroll unit is a line).
    verticalScrollBar()->setValue(qMax(0, it.value() - 3));
    return true;
}

QString EclBasicEdit::wordAt(const QPoint &pos) const
{
    const QTextCursor cursor = cursorForPosition(pos);
    const QString line = cursor.block().text();
    int at = cursor.positionInBlock();
    if (at >= line.size() || !isWordChar(line.at(at)))
        --at;
    if (at < 0 || at >= line.size() || !isWordChar(line.at(at)))
        return QString();
    int start = at;
    int end = at;
    while (start > 0 && isWordChar(line.at(start - 1)))
        --start;
    while (end < line.size() && isWordChar(line.at(end)))
        ++end;
    return line.mid(start, end - start);
}

QString EclBasicEdit::variableAt(const QPoint &pos) const
{
    const QTextCursor cursor = cursorForPosition(pos);
    const QString line = cursor.block().text();
    const int at = cursor.positionInBlock();
    const int open = line.lastIndexOf(QLatin1Char('['), qMax(0, qMin(at, line.size() - 1)));
    if (open < 0)
        return QString();
    const int close = line.indexOf(QLatin1Char(']'), open);
    if (close < 0 || close < at - 1)
        return QString();
    return line.mid(open + 1, close - open - 1);
}

void EclBasicEdit::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        if (jumpTo(wordAt(event->position().toPoint()))) {
            event->accept();
            return;
        }
    }
    QPlainTextEdit::mousePressEvent(event);
}

void EclBasicEdit::mouseMoveEvent(QMouseEvent *event)
{
    const bool link = (event->modifiers() & Qt::ControlModifier)
                      && m_definitions.contains(wordAt(event->position().toPoint()));
    viewport()->setCursor(link ? Qt::PointingHandCursor : Qt::IBeamCursor);
    QPlainTextEdit::mouseMoveEvent(event);
}

bool EclBasicEdit::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const QPoint pos = viewport()->mapFromGlobal(help->globalPos());
        const QString variable = variableAt(pos);
        QString tip = variable.isEmpty() ? QString() : eclBasicSymbolDescription(variable);
        if (tip.isEmpty() && m_definitions.contains(wordAt(pos)))
            tip = QStringLiteral("Ctrl+click to go to %1").arg(wordAt(pos));
        if (tip.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), tip, this);
        return true;
    }
    return QPlainTextEdit::event(event);
}

EclBasicView::EclBasicView(bool ok, const QString &text, const QString &error, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_edit = new EclBasicEdit;
    if (!ok) {
        m_edit->setPlainText(
            QStringLiteral("This block is not an ECL program of Champions of Krynn,\n"
                           "Death Knights of Krynn or Pool of Radiance (Amiga),\n"
                           "so it cannot be shown as ECL Basic.\n\n"
                           "Decoder: %1\n\n"
                           "The other views of this block are unaffected.\n")
                .arg(error));
        layout->addWidget(m_edit);
        return;
    }

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(4, 4, 4, 0);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("Find (Ctrl+F, Enter / Shift+Enter)"));
    m_search->setClearButtonEnabled(true);
    auto *prev = new QPushButton(QStringLiteral("Previous"));
    auto *next = new QPushButton(QStringLiteral("Next"));
    m_status = new QLabel;
    bar->addWidget(new QLabel(QStringLiteral("Find:")));
    bar->addWidget(m_search, 1);
    bar->addWidget(prev);
    bar->addWidget(next);
    bar->addWidget(m_status);
    layout->addLayout(bar);

    const QVector<Definition> definitions = scanDefinitions(text);
    QHash<QString, int> byName;
    for (const Definition &d : definitions)
        byName.insert(d.name, d.block);

    m_edit->setPlainText(text);
    m_edit->setDefinitions(byName);
    new EclBasicHighlighter(m_edit->document(), byName);

    m_labels = new QListWidget;
    m_labels->setFont(listingFont());
    for (const Definition &d : definitions) {
        auto *item = new QListWidgetItem(
            d.detail.isEmpty() ? d.name : QStringLiteral("%1  (%2)").arg(d.name, d.detail));
        item->setData(Qt::UserRole, d.name);
        item->setToolTip(QStringLiteral("Line %1").arg(d.block + 1));
        m_labels->addItem(item);
    }

    auto *labelPane = new QWidget;
    auto *labelLayout = new QVBoxLayout(labelPane);
    labelLayout->setContentsMargins(0, 0, 0, 0);
    labelLayout->addWidget(new QLabel(QStringLiteral("Labels and tables (%1)").arg(definitions.size())));
    labelLayout->addWidget(m_labels);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_edit);
    splitter->addWidget(labelPane);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({700, 240});
    labelPane->setMinimumWidth(180);
    layout->addWidget(splitter, 1);

    connect(m_labels, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        m_edit->jumpTo(item->data(Qt::UserRole).toString());
    });
    connect(m_labels, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        m_edit->jumpTo(item->data(Qt::UserRole).toString());
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        find(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
    });
    connect(m_search, &QLineEdit::textChanged, this, [this] { m_status->clear(); });
    connect(next, &QPushButton::clicked, this, [this] { find(false); });
    connect(prev, &QPushButton::clicked, this, [this] { find(true); });

    auto *focusFind = new QShortcut(QKeySequence::Find, this);
    focusFind->setContext(Qt::WidgetWithChildrenShortcut);
    connect(focusFind, &QShortcut::activated, this, [this] {
        m_search->setFocus();
        m_search->selectAll();
    });
    auto *findNext = new QShortcut(QKeySequence::FindNext, this);
    findNext->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findNext, &QShortcut::activated, this, [this] { find(false); });
    auto *findPrev = new QShortcut(QKeySequence::FindPrevious, this);
    findPrev->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findPrev, &QShortcut::activated, this, [this] { find(true); });
}

QString EclBasicView::text() const
{
    return m_edit->toPlainText();
}

void EclBasicView::find(bool backwards)
{
    if (!m_search || m_search->text().isEmpty())
        return;
    const QString needle = m_search->text();
    const QTextDocument::FindFlags flags = backwards ? QTextDocument::FindBackward
                                                     : QTextDocument::FindFlags();
    if (m_edit->find(needle, flags)) {
        m_status->clear();
        return;
    }
    // Wrap around.
    QTextCursor cursor = m_edit->textCursor();
    cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
    m_edit->setTextCursor(cursor);
    if (m_edit->find(needle, flags))
        m_status->setText(QStringLiteral("wrapped"));
    else
        m_status->setText(QStringLiteral("not found"));
}

EclBasicBlocksView::EclBasicBlocksView(const QVector<QPair<int, QByteArray>> &blocks,
                                       const QString &fileTag, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *summary = new QLabel;
    summary->setMargin(4);
    layout->addWidget(summary);

    m_list = new QListWidget;
    m_stack = new QStackedWidget;
    for (const auto &block : blocks) {
        EclBasicResult result = eclBasicDisassembleBlockAuto(block.second, fileTag, block.first,
                                                             EclBasicDialect::ChampionsOfKrynn);
        QString how = QStringLiteral("ECL block");
        if (!result.ok) {
            EclBasicResult bare = eclBasicDisassemble(block.second, fileTag, block.first);
            for (EclBasicDialect other : {EclBasicDialect::DeathKnightsOfKrynn, EclBasicDialect::PoolOfRadiance}) {
                if (bare.ok)
                    break;
                bare = eclBasicDisassemble(block.second, fileTag, block.first, other);
            }
            if (bare.ok) {
                result = bare;
                how = QStringLiteral("program image");
            }
        }
        if (result.ok)
            ++m_decoded;
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  %2").arg(block.first).arg(result.ok ? how : QStringLiteral("not decoded")));
        if (!result.ok)
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        m_list->addItem(item);
        m_stack->addWidget(new EclBasicView(result.ok, result.text, result.error));
    }
    summary->setText(QStringLiteral("%1 of %2 blocks decode as ECL Basic.")
                         .arg(m_decoded)
                         .arg(blocks.size()));

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_list);
    splitter->addWidget(m_stack);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 5);
    layout->addWidget(splitter, 1);

    connect(m_list, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    if (!blocks.isEmpty())
        m_list->setCurrentRow(0);
}
