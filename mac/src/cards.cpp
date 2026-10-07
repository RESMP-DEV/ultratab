#include "cards.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <algorithm>

namespace lapis::ultratab {
namespace {
constexpr qsizetype kShortText = 300;
constexpr qsizetype kLongText = 2000;
constexpr qsizetype kCellText = 200;
constexpr qsizetype kLabelText = 120;
constexpr qsizetype kUrlText = 2048;
// A light fill for unstyled SVG (the card's body text colour).
constexpr auto kDefaultFill = "#c6d2e4";

QString bounded(const QJsonValue& value, qsizetype limit) {
    auto text = value.toString().trimmed();
    if (text.size() > limit) {
        auto cut = limit - 1;
        if (text.at(cut - 1).isHighSurrogate())
            --cut;
        text = text.left(cut).trimmed() + QChar(0x2026);
    }
    return text;
}

// A reference that stays in the document: "#id" or url(#id).
bool local_reference(QStringView value) {
    static const QRegularExpression url(QStringLiteral("url\\s*\\(\\s*['\"]?\\s*([^)'\"\\s]*)"),
                                        QRegularExpression::CaseInsensitiveOption);
    auto matches = url.globalMatchView(value);
    while (matches.hasNext())
        if (!matches.next().captured(1).startsWith(QLatin1Char('#')))
            return false;
    return true;
}

bool safe_style(QStringView css) {
    return !css.contains(QLatin1String("@import"), Qt::CaseInsensitive) &&
           !css.contains(QLatin1String("javascript:"), Qt::CaseInsensitive) &&
           !css.contains(QLatin1String("expression("), Qt::CaseInsensitive) && local_reference(css);
}

// One pass over an SVG's elements; `safe` turns false at the first thing
// that could run code or reach outside the document.
struct SvgScan {
    bool safe = true;
    bool root = true;
    bool in_style = false;
    bool root_fill = false;

    static bool safe_attribute(const QXmlStreamAttribute& attribute) {
        const auto name = attribute.name().toString().toLower();
        const auto value = attribute.value();
        if (name.startsWith(QLatin1String("on")))
            return false;
        if (name == QLatin1String("href") && !value.trimmed().startsWith(QLatin1Char('#')))
            return false;
        return safe_style(value);
    }

    void element(const QXmlStreamReader& reader) {
        static const QStringList forbidden{
            QStringLiteral("script"), QStringLiteral("foreignobject"), QStringLiteral("iframe"),
            QStringLiteral("object"), QStringLiteral("embed"),         QStringLiteral("audio"),
            QStringLiteral("video"),  QStringLiteral("handler"),       QStringLiteral("listener")};
        const auto name = reader.name().toString().toLower();
        if ((root && name != QLatin1String("svg")) || forbidden.contains(name)) {
            safe = false;
            return;
        }
        const auto attributes = reader.attributes();
        safe = std::all_of(attributes.cbegin(), attributes.cend(), &safe_attribute);
        if (root)
            root_fill = attributes.hasAttribute(QStringLiteral("fill"));
        in_style = name == QLatin1String("style");
        root = false;
    }
};

std::optional<Block> parse_text(const QJsonObject& object) {
    Block block;
    block.type = Block::Type::text;
    block.text = bounded(object.value(QStringLiteral("text")), kLongText);
    return block.text.isEmpty() ? std::nullopt : std::optional<Block>(std::move(block));
}

std::optional<Block> parse_list(const QJsonObject& object) {
    Block block;
    block.type = Block::Type::list;
    for (const auto& item : object.value(QStringLiteral("items")).toArray())
        if (const auto text = bounded(item, kShortText);
            !text.isEmpty() && block.items.size() < max_list_items)
            block.items.append(text);
    return block.items.isEmpty() ? std::nullopt : std::optional<Block>(std::move(block));
}

// A column is numeric when every filled cell in it is a number.
QList<bool> numeric_columns(const Block& block) {
    QList<bool> numeric;
    for (qsizetype column = 0; column < block.columns.size(); ++column) {
        bool numbers = false;
        bool all = true;
        for (const auto& row : block.rows) {
            if (row.at(column).isEmpty())
                continue;
            numbers = true;
            all = all && numeric_cell(row.at(column));
        }
        numeric.append(numbers && all);
    }
    return numeric;
}

std::optional<Block> parse_table(const QJsonObject& object) {
    Block block;
    block.type = Block::Type::table;
    for (const auto& column : object.value(QStringLiteral("columns")).toArray())
        if (block.columns.size() < max_table_columns)
            block.columns.append(bounded(column, kCellText));
    if (block.columns.isEmpty())
        return std::nullopt;
    for (const auto& value : object.value(QStringLiteral("rows")).toArray()) {
        if (block.rows.size() == max_table_rows) {
            ++block.more_rows;
            continue;
        }
        QStringList row;
        for (const auto& cell : value.toArray())
            if (row.size() < block.columns.size())
                row.append(bounded(cell, kCellText));
        while (row.size() < block.columns.size())
            row.append(QString());
        block.rows.append(row);
    }
    if (block.rows.isEmpty())
        return std::nullopt;
    block.numeric = numeric_columns(block);
    return block;
}

std::optional<Block> parse_diagram(const QJsonObject& object) {
    auto clean = sanitize_svg(object.value(QStringLiteral("svg")).toString());
    if (!clean)
        return std::nullopt;
    const QSvgRenderer renderer(clean->toUtf8());
    if (!renderer.isValid())
        return std::nullopt;
    Block block;
    block.type = Block::Type::diagram;
    const auto box = renderer.viewBoxF();
    block.size = box.isEmpty() ? QSizeF(renderer.defaultSize()) : box.size();
    if (block.size.isEmpty())
        return std::nullopt;
    block.svg = std::move(*clean);
    return block;
}

std::optional<Block> parse_link(const QJsonObject& object) {
    const auto url = object.value(QStringLiteral("url")).toString().trimmed();
    Block block;
    block.type = Block::Type::link;
    block.url = QUrl(url, QUrl::StrictMode);
    if (url.size() > kUrlText || !openable(block.url))
        return std::nullopt;
    block.label = bounded(object.value(QStringLiteral("label")), kLabelText);
    if (block.label.isEmpty())
        block.label = block.url.isLocalFile() ? block.url.fileName() : block.url.host();
    return block;
}

std::optional<Block> parse_block(const QJsonObject& object) {
    using Parse = std::optional<Block> (*)(const QJsonObject&);
    static const QHash<QString, Parse> parsers{{QStringLiteral("text"), &parse_text},
                                               {QStringLiteral("list"), &parse_list},
                                               {QStringLiteral("table"), &parse_table},
                                               {QStringLiteral("diagram"), &parse_diagram},
                                               {QStringLiteral("link"), &parse_link}};
    const auto parser = parsers.value(object.value(QStringLiteral("type")).toString());
    return parser == nullptr ? std::nullopt : parser(object);
}

std::optional<ComposedCard> parse_card(const QJsonObject& object) {
    ComposedCard card;
    card.key = object.value(QStringLiteral("key")).toString();
    if (card.key.isEmpty())
        return std::nullopt;
    card.composed = QDateTime::fromString(object.value(QStringLiteral("composed")).toString(),
                                          Qt::ISODateWithMs);
    card.model = bounded(object.value(QStringLiteral("model")), kLabelText);
    card.since = bounded(object.value(QStringLiteral("since")), kShortText);
    card.tldr = bounded(object.value(QStringLiteral("tldr")), kShortText);
    card.prompt = bounded(object.value(QStringLiteral("prompt")), kLongText);
    for (const auto& value : object.value(QStringLiteral("blocks")).toArray()) {
        if (card.blocks.size() == max_blocks)
            break;
        if (auto block = parse_block(value.toObject()))
            card.blocks.append(std::move(*block));
    }
    return card;
}
} // namespace

ComposedCards parse_cards(const QByteArray& bytes) {
    ComposedCards result;
    if (bytes.isEmpty())
        return result;
    const auto root = QJsonDocument::fromJson(bytes).object();
    if (root.value(QStringLiteral("v")).toInt() != cards_version) {
        result.problem = QStringLiteral("ultratab_cards.json has another version");
        return result;
    }
    result.present = true;
    const auto cards = root.value(QStringLiteral("cards")).toObject();
    for (auto entry = cards.constBegin(); entry != cards.constEnd(); ++entry)
        if (auto card = parse_card(entry.value().toObject()))
            result.cards.insert(entry.key(), std::move(*card));
    return result;
}

std::optional<QString> sanitize_svg(const QString& svg) {
    if (svg.isEmpty() || svg.size() > max_svg_bytes)
        return std::nullopt;
    QXmlStreamReader reader(svg);
    SvgScan scan;
    while (!reader.atEnd() && scan.safe) {
        switch (reader.readNext()) {
        case QXmlStreamReader::DTD:
        case QXmlStreamReader::EntityReference:
        case QXmlStreamReader::ProcessingInstruction:
            return std::nullopt;
        case QXmlStreamReader::StartElement:
            scan.element(reader);
            break;
        case QXmlStreamReader::EndElement:
            scan.in_style = false;
            break;
        case QXmlStreamReader::Characters:
            scan.safe = !scan.in_style || safe_style(reader.text());
            break;
        default:
            break;
        }
    }
    if (!scan.safe || reader.hasError() || scan.root)
        return std::nullopt;
    auto clean = svg;
    if (!scan.root_fill) {
        // Inherited by every shape and text that sets no fill of its own.
        const auto at = clean.indexOf(QLatin1String("<svg"), 0, Qt::CaseInsensitive);
        if (at >= 0)
            clean.insert(
                at + 4,
                QStringLiteral(" fill=\"%1\" color=\"%1\"").arg(QLatin1String(kDefaultFill)));
    }
    return clean;
}

bool openable(const QUrl& url) {
    if (!url.isValid() || !url.userInfo().isEmpty())
        return false;
    if (url.scheme() == QLatin1String("https"))
        return !url.host().isEmpty();
    return url.scheme() == QLatin1String("file") && url.host().isEmpty() &&
           url.path().startsWith(QLatin1Char('/'));
}

bool numeric_cell(const QString& cell) {
    static const QRegularExpression number(
        QStringLiteral("^[~≈<>]?\\s*[-+−]?[$€£]?(\\d{1,3}(,\\d{3})+|\\d+)?(\\.\\d+)?\\s*"
                       "(%|x|×|ms|us|µs|ns|s|min|h|k|K|M|B|G|T|KB|MB|GB|TB|KiB|MiB|GiB|TiB|"
                       "TFLOPS|GFLOPS|tok/s|it/s)?$"));
    const auto trimmed = cell.trimmed();
    return trimmed.contains(QRegularExpression(QStringLiteral("\\d"))) &&
           number.match(trimmed).hasMatch();
}

bool key_current(const ComposedCard& composed, const QString& card_key) {
    if (composed.key.isEmpty())
        return false;
    if (composed.key == card_key)
        return true;
    const auto bar = card_key.indexOf(QLatin1Char('|'));
    return bar > 0 && composed.key == card_key.mid(bar + 1);
}
} // namespace lapis::ultratab
