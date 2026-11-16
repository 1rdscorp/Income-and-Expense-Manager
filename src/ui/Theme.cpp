#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>
#include <QSvgRenderer>

namespace {

ThemeColors lightColors()
{
    ThemeColors t;
    t.dark = false;
    t.page = QColor(0xf6f6f3);
    t.surface = QColor(0xffffff);
    t.surface2 = QColor(0xf9f9f7);
    t.surface3 = QColor(0xf0efec);
    t.border = QColor(0xe4e3de);
    t.borderStrong = QColor(0xd2d1cb);
    t.text = QColor(0x0b0b0b);
    t.text2 = QColor(0x52514e);
    t.muted = QColor(0x6f6e69);
    t.grid = QColor(0xe1e0d9);
    t.axis = QColor(0xc3c2b7);
    t.accent = QColor(0x2a78d6);
    t.accentHover = QColor(0x256abf);
    t.accentSoft = QColor(0xe6f0fc);
    t.goodText = QColor(0x006300);
    t.badText = QColor(0xb42323);
    t.critical = QColor(0xd03b3b);
    t.warning = QColor(0xfab219);
    t.warningBg = QColor(0xfef4dd);
    t.warningBorder = QColor(0xf3cf7d);
    t.income = QColor(0x1baf7a);
    t.expense = QColor(0xeb6834);
    t.sidebar = QColor(0x101826);
    t.sidebarText = QColor(0xc9d1dd);
    return t;
}

ThemeColors darkColors()
{
    ThemeColors t;
    t.dark = true;
    t.page = QColor(0x0d0d0d);
    t.surface = QColor(0x1a1a19);
    t.surface2 = QColor(0x212120);
    t.surface3 = QColor(0x2c2c2a);
    t.border = QColor(0x2f2f2d);
    t.borderStrong = QColor(0x444441);
    t.text = QColor(0xf0efec);
    t.text2 = QColor(0xc3c2b7);
    t.muted = QColor(0x9a9890);
    t.grid = QColor(0x2c2c2a);
    t.axis = QColor(0x4a4a46);
    t.accent = QColor(0x3987e5);
    t.accentHover = QColor(0x5598e7);
    t.accentSoft = QColor(0x1b2a3d);
    t.goodText = QColor(0x0ca30c);
    t.badText = QColor(0xe66767);
    t.critical = QColor(0xd03b3b);
    t.warning = QColor(0xfab219);
    t.warningBg = QColor(0x2e2510);
    t.warningBorder = QColor(0x6b5214);
    t.income = QColor(0x199e70);
    t.expense = QColor(0xd95926);
    t.sidebar = QColor(0x121212);
    t.sidebarText = QColor(0xc9d1dd);
    return t;
}

QString hex(const QColor &c) { return c.name(QColor::HexRgb); }

QString styleSheet(const ThemeColors &t)
{
    QString s = QStringLiteral(R"(
QMainWindow, #pageRoot, QScrollArea#pageScroll, QScrollArea#pageScroll > QWidget > QWidget { background: @page; }
QWidget { color: @text; }
#sidebar { background: @sidebar; }
#sidebar QLabel#brandName { color: #ffffff; font-size: 15px; font-weight: 600; }
#sidebar QLabel#brandSub, #sidebar QLabel#sidebarFoot { color: #8a95a5; font-size: 12px; }
QListWidget#nav { background: transparent; border: none; outline: 0; color: @sidebarText; font-size: 14px; }
QListWidget#nav::item { padding: 8px 10px; border-radius: 8px; margin: 1px 0; }
QListWidget#nav::item:hover { background: rgba(255,255,255,0.07); color: #ffffff; }
QListWidget#nav::item:selected { background: rgba(57,135,229,0.28); color: #ffffff; }
#topBar { background: @page; border-bottom: 1px solid @border; }
QLabel#pageTitle { font-size: 20px; font-weight: 600; }
QFrame#card { background: @surface; border: 1px solid @border; border-radius: 12px; }
QFrame#card QLabel { background: transparent; }
QLabel#cardTitle { font-size: 14px; font-weight: 600; }
QLabel#cardSub, QLabel#muted { color: @muted; font-size: 12px; }
QLabel#kpiLabel { color: @text2; font-size: 12px; font-weight: 600; }
QLabel#kpiValue { font-size: 24px; font-weight: 700; }
QLabel#kpiValueSmall { font-size: 18px; font-weight: 700; }
QLabel#kpiMeta { color: @muted; font-size: 12px; }
QFrame#banner { background: @warningBg; border: 1px solid @warningBorder; border-radius: 12px; }
QFrame#banner QLabel { background: transparent; }
QPushButton { background: @surface; border: 1px solid @borderStrong; border-radius: 8px; padding: 6px 14px; min-height: 22px; }
QPushButton:hover { background: @surface3; }
QPushButton:pressed { background: @borderStrong; }
QPushButton:disabled { color: @muted; background: @surface2; }
QPushButton#primary { background: @accent; border-color: @accent; color: #ffffff; font-weight: 600; }
QPushButton#primary:hover { background: @accentHover; }
QPushButton#danger { background: @critical; border-color: @critical; color: #ffffff; font-weight: 600; }
QPushButton#dangerText { color: @badText; }
QPushButton#flat { border: none; background: transparent; padding: 4px 6px; }
QPushButton#flat:hover { background: @surface3; }
QPushButton#link { border: none; background: transparent; color: @accent; padding: 0; min-height: 0; }
QPushButton#link:hover { text-decoration: underline; }
QFrame#seg { background: @surface3; border-radius: 9px; }
QFrame#seg QPushButton { border: none; background: transparent; color: @text2; padding: 5px 14px; border-radius: 7px; font-weight: 600; }
QFrame#seg QPushButton:checked { background: @surface; color: @text; }
QFrame#seg QPushButton:hover:!checked { color: @text; }
QTreeWidget, QTableWidget, QListWidget#plainList { background: @surface; border: none; alternate-background-color: @surface2; gridline-color: @border; selection-background-color: @accentSoft; selection-color: @text; }
QTreeWidget::item { padding: 5px 2px; }
QHeaderView::section { background: @surface2; color: @text2; font-weight: 600; padding: 6px 8px; border: none; border-bottom: 1px solid @border; }
QTableWidget QTableCornerButton::section { background: @surface2; border: none; }
QLineEdit, QComboBox, QDateEdit, QPlainTextEdit, QSpinBox { min-height: 24px; }
QLineEdit#amountEdit { font-size: 20px; font-weight: 700; }
QToolTip { background: @surface; color: @text; border: 1px solid @borderStrong; padding: 6px; }
QProgressBar { background: @surface3; border: none; border-radius: 4px; max-height: 8px; }
QProgressBar::chunk { background: @expense; border-radius: 4px; }
QScrollArea { border: none; }
)");
    const QList<QPair<QString, QColor>> tokens = {
        {QStringLiteral("@pageRoot"), t.page},
        {QStringLiteral("@page"), t.page},
        {QStringLiteral("@surface3"), t.surface3},
        {QStringLiteral("@surface2"), t.surface2},
        {QStringLiteral("@surface"), t.surface},
        {QStringLiteral("@borderStrong"), t.borderStrong},
        {QStringLiteral("@border"), t.border},
        {QStringLiteral("@text2"), t.text2},
        {QStringLiteral("@text"), t.text},
        {QStringLiteral("@muted"), t.muted},
        {QStringLiteral("@accentHover"), t.accentHover},
        {QStringLiteral("@accentSoft"), t.accentSoft},
        {QStringLiteral("@accent"), t.accent},
        {QStringLiteral("@badText"), t.badText},
        {QStringLiteral("@critical"), t.critical},
        {QStringLiteral("@warningBg"), t.warningBg},
        {QStringLiteral("@warningBorder"), t.warningBorder},
        {QStringLiteral("@expense"), t.expense},
        {QStringLiteral("@sidebarText"), t.sidebarText},
        {QStringLiteral("@sidebar"), t.sidebar},
    };
    for (const auto &tok : tokens) s.replace(tok.first, hex(tok.second));
    return s;
}

QPalette paletteFor(const ThemeColors &t)
{
    QPalette p;
    p.setColor(QPalette::Window, t.page);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.surface);
    p.setColor(QPalette::AlternateBase, t.surface2);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::Button, t.surface);
    p.setColor(QPalette::ButtonText, t.text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, t.accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, t.surface);
    p.setColor(QPalette::ToolTipText, t.text);
    p.setColor(QPalette::PlaceholderText, t.muted);
    p.setColor(QPalette::Link, t.accent);
    p.setColor(QPalette::Light, t.dark ? t.surface3 : QColor(Qt::white));
    p.setColor(QPalette::Midlight, t.surface3);
    p.setColor(QPalette::Mid, t.borderStrong);
    p.setColor(QPalette::Dark, t.dark ? QColor(0x111111) : t.axis);
    p.setColor(QPalette::Shadow, t.dark ? QColor(Qt::black) : QColor(0x9a9890));
    p.setColor(QPalette::Disabled, QPalette::Text, t.muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, t.muted);
    p.setColor(QPalette::Disabled, QPalette::WindowText, t.muted);
    return p;
}

} // namespace

Theme &Theme::instance()
{
    static Theme t;
    return t;
}

bool Theme::systemPrefersDark() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return false;
#endif
}

void Theme::apply(const QString &mode)
{
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
            if (m_mode == QLatin1String("system")) apply(m_mode);
        });
#endif
    }
    m_mode = mode;
    const bool dark = mode == QLatin1String("dark") || (mode == QLatin1String("system") && systemPrefersDark());
    m_colors = dark ? darkColors() : lightColors();
    qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    qApp->setPalette(paletteFor(m_colors));
    qApp->setStyleSheet(styleSheet(m_colors));
    emit changed();
}

QPixmap Theme::iconPixmap(const QString &name, const QColor &color, int size)
{
    QFile f(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!f.open(QIODevice::ReadOnly)) return QPixmap();
    QByteArray svg = f.readAll();
    const QColor col = color.isValid() ? color : c().text2;
    svg.replace("currentColor", col.name().toLatin1());
    QSvgRenderer r(svg);
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(QSize(size, size) * dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    r.render(&p);
    pm.setDevicePixelRatio(dpr);
    return pm;
}

QIcon Theme::icon(const QString &name, const QColor &color)
{
    QIcon ic;
    for (int s : {16, 24, 32, 48}) ic.addPixmap(iconPixmap(name, color, s));
    return ic;
}

QIcon Theme::appIcon()
{
    QIcon ic;
    QSvgRenderer r(QStringLiteral(":/icons/app.svg"));
    for (int s : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        r.render(&p);
        ic.addPixmap(pm);
    }
    return ic;
}
