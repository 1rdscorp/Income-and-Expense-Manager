#pragma once

#include <QColor>
#include <QIcon>
#include <QObject>
#include <QString>

struct ThemeColors {
    bool dark = false;
    QColor page, surface, surface2, surface3, border, borderStrong;
    QColor text, text2, muted, grid, axis;
    QColor accent, accentHover, accentSoft;
    QColor goodText, badText, critical, warning, warningBg, warningBorder;
    QColor income, expense;
    QColor sidebar, sidebarText;
};

class Theme : public QObject
{
    Q_OBJECT
public:
    static Theme &instance();
    static const ThemeColors &c() { return instance().m_colors; }

    // mode: "system", "light" or "dark"
    void apply(const QString &mode);
    QString mode() const { return m_mode; }

    static QIcon icon(const QString &name, const QColor &color = QColor());
    static QPixmap iconPixmap(const QString &name, const QColor &color, int size);
    static QIcon appIcon();

signals:
    void changed();

private:
    Theme() = default;
    bool systemPrefersDark() const;
    QString m_mode = QStringLiteral("system");
    ThemeColors m_colors;
};
