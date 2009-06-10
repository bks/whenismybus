/*
 *   Copyright 2009 Benjamin K. Stuhl <bks24@cornell.edu>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "rtdscheduleapplet.h"

#include <QtCore/QDateTime>
#include <QtCore/QPair>

#include <QtGui/QGraphicsLinearLayout>

RtdScheduleApplet::RtdScheduleApplet(QObject *parent, const QVariantList& args)
    : Plasma::Applet(parent, args)
{
    m_label = new Plasma::Label(this);

    QGraphicsLinearLayout *layout = new QGraphicsLinearLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addItem(m_label);

    setBackgroundHints(DefaultBackground);
    resize(170, 150);
}

RtdScheduleApplet::~RtdScheduleApplet()
{
}

void RtdScheduleApplet::init()
{
    QString sourceName = QLatin1String("NextStops [B/BF/BX-E:Broadway - 16th St (University of Colorado)"
                                       ",DASH-E:Broadway - 16th St (University of Colorado)"
                                       ",204-S:Broadway - 16th St (University of Colorado)"
                                       ",AB-E:Broadway - 16th St (University of Colorado)"
                                       "] 4");

// NextStops [B/BF/BX-E:Broadway - 16th St (University of Colorado),DASH-E:Broadway - 16th St (University of Colorado),204-S:Broadway - 16th St (University of Colorado),AB-E:Broadway - 16th St (University of Colorado)] 4 TEXT
    Plasma::DataEngine *de = dataEngine("rtddenver");
    if (!de->isValid()) {
        setFailedToLaunch(true, i18n("Cannot connect to RTD Denver data engine"));
        return;
    }

    setBusy(true);
    de->connectSource(sourceName, this, 60*1000, Plasma::AlignToMinute);
}

void RtdScheduleApplet::dataUpdated(const QString& sourceName, const Plasma::DataEngine::Data& data)
{
    if (data.isEmpty())
        return;

    setBusy(false);
    QList<DateTimeRoutePair> stops = data[sourceName].value< QList<DateTimeRoutePair> >();

    QString text = QLatin1String("<html><body style='background-color: transparent;'>"
                                 "<table style='margin-top: 8px;' cellpadding='3' cellspacing='0' width='100%'>"
                                 "<tr style='background-color: rgba(0, 0, 0, 50); border: 1px solid black;'>"
                                 "<th width='30%' align='left' style='background-color: rgba(0,0,0,50);'>%1</th>"
                                 "<th width='70%' align='left'>%2</th>");
    text = text.arg(i18n("Route"), i18n("Departs"));

    QStringList styles;
    styles << QLatin1String("background-color: rgba(255, 255, 255, 50);");
    styles << QLatin1String("background-color: rgba(0, 0, 0, 50);");
    int rowStyle = 0;

    foreach (const DateTimeRoutePair& dr, stops) {
        text += tableRowForDataRow(dr, styles[rowStyle]);
        rowStyle = rowStyle ? 0 : 1;
    }
    
    text += QLatin1String("</table></body></html>");
    m_label->setText(text);
}

QString RtdScheduleApplet::tableRowForDataRow(const DateTimeRoutePair& dr, const QString& style)
{
    QString ret = QString(QLatin1String("<tr style='%1'><td width='30%'>")).arg(style);
    ret += dr.second;
    ret += QLatin1String("</td><td width='70%'>");
    ret += dr.first.toString("h:mm' 'AP");
    ret += QLatin1String("</td></tr>");

    return ret;
}

K_EXPORT_PLASMA_APPLET(rtdschedule, RtdScheduleApplet)

#include "rtdscheduleapplet.moc"
