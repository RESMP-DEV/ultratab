#ifndef LAPIS_ULTRATAB_CARDS_HPP
#define LAPIS_ULTRATAB_CARDS_HPP
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtGlobal>
#include <cstdint>
#include <optional>

// Composed cards (runtime/ultratab_cards.json, version 1): a richer card for a
// session, written by a composer beside lapis. A composed card replaces the
// plain one only while its key still names the agent's current turn; anything
// that fails validation is dropped and the plain card shows instead.
namespace lapis::ultratab {
struct Block {
    enum class Type : std::uint8_t { text, list, table, diagram, link };
    Type type{Type::text};
    QString text;            // text
    QStringList items;       // list
    QStringList columns;     // table
    QList<QStringList> rows; // table, each padded to columns.size()
    QList<bool> numeric;     // table: a column of numbers, right-aligned
    int more_rows{};         // table rows left out past the limit
    QString svg;             // diagram, sanitized
    QSizeF size;             // diagram's own size (for its aspect ratio)
    QString label;           // link
    QUrl url;                // link: file: or https: only
};
struct ComposedCard {
    QString key;
    QDateTime composed;
    QString model;
    QString since;
    QString attention; // needs, steer or fyi (the composer's judgement); may be empty
    QString tldr;
    QList<Block> blocks; // at most max_blocks
    QString prompt;
};
struct ComposedCards {
    QHash<QString, ComposedCard> cards; // by session (agent) id
    bool present{};
    QString problem;
};

constexpr qint64 max_cards_bytes = qint64{8} * 1024 * 1024;
constexpr qsizetype max_svg_bytes = qsizetype{256} * 1024;
constexpr int cards_version = 1;
constexpr int max_blocks = 3;
constexpr int max_list_items = 8;
constexpr int max_table_columns = 6;
constexpr int max_table_rows = 8;

[[nodiscard]] ComposedCards parse_cards(const QByteArray& bytes);
// The SVG when it is safe to draw, else nothing: no script, foreignObject,
// event handlers, DTDs or references outside the document. A root without a
// fill is given a light one so unstyled shapes and text read on a dark card.
[[nodiscard]] std::optional<QString> sanitize_svg(const QString& svg);
// Only file: and https: links are shown and opened.
[[nodiscard]] bool openable(const QUrl& url);
// Whether a cell reads as a number ("1,024", "-3.5%", "12 ms", "2.1x").
[[nodiscard]] bool numeric_cell(const QString& cell);
// A composed card is current when its key is the card's key
// ("<session id>|<turn>|<needed>|<offer key>|<requests>"), with or without the
// leading "<session id>|".
[[nodiscard]] bool key_current(const ComposedCard& composed, const QString& card_key);
} // namespace lapis::ultratab
#endif
