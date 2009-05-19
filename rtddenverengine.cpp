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

#include "rtddenverengine.h"

#include <KDE/KJob>
#include <KDE/KIO/Job>
#include <KDE/KIO/TransferJob>

#include <QtCore/QByteArray>
#include <QtCore/QDate>
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QStringList>

#include <QtWebKit/QWebFrame>
#include <QtWebKit/QWebPage>

RtdDenverEngine::RtdDenverEngine(QObject *parent, const QVariantList& args)
    : Plasma::DataEngine(parent, args)
{
}

bool RtdDenverEngine::sourceRequestEvent(const QString& sourceName)
{
    return updateSourceEvent(sourceName);
}

static bool isRtdHoliday(const QDate& date)
{
    // New Years'
    if (date.month() == 1 && date.day() == 1)
	return true;

    // Memorial Day: last Monday in May
    if (date.month() == 5 && date.dayOfWeek() == Qt::Monday && date.day() > 24)
	return true;

    // Independance Day
    if (date.month() == 6 && date.day() == 4)
	return true;

    // Labor Day: first Monday in September
    if (date.month() == 9 && date.dayOfWeek() == Qt::Monday && date.day() < 8)
	return true;

    // Thanksgiving Day: 4th Thursday in November
    if (date.month() == 11 && date.dayOfWeek() == Qt::Thursday && date.day() > 21 && date.day() <= 28)
	return true;

    // Christmas Day
    if (date.month() == 12 && date.day() == 25)
	return true;

    return false;
}

RtdDenverEngine::DayType RtdDenverEngine::parseDay(const QString& day)
{
    static QHash<QString, DayType> dayTypes;

    if (dayTypes.isEmpty()) {
	dayTypes.insert("Weekday", Weekday);
	dayTypes.insert("Saturday", Saturday);
	dayTypes.insert("Sunday", SundayHoliday);
	dayTypes.insert("Holiday", SundayHoliday);
    }
    if (dayTypes.contains(day))
	return dayTypes[day];

    return Weekday;
}

bool RtdDenverEngine::updateSourceEvent(const QString& sourceName)
{
    KJob *fetchJob = 0;
    if (sourceName == QLatin1String("Routes")) {
	fetchJob = fetchRouteList();
    } else if (sourceName.startsWith(QLatin1String("Schedule/"))) {
	QStringList parts = sourceName.split('/');

	if (parts.length() < 2 || parts.length() > 4)
	    return false;

	// figure out what day to look up the schedule for
	DayType day;
	if (parts.length() > 2 && parts[2] != QLatin1String("Today")) {
	    day = parseDay(parts[2]);
	} else {
	    // default to whatever "today" is
	    QDate today = QDate::currentDate();
	    if (today.day() == Qt::Saturday)
		day = Saturday;
	    else if (today.day() == Qt::Sunday || isRtdHoliday(today))
		day = SundayHoliday;
	    else
		day = Weekday;
	}

	// see if a travel direction was specified
	QString direction;
	if (parts.length() > 3)
	    direction = parts[3];

	fetchJob = fetchSchedule(parts[1], day, direction);
    }

    if (!fetchJob)
	return false;

    JobData jd;
    jd.sourceName = sourceName;
    m_jobData.insert(fetchJob, jd);
    return true;
}

void RtdDenverEngine::dataReceived(KIO::Job *job, const QByteArray& data)
{
    m_jobData[job].networkData += data;
}

void RtdDenverEngine::routeListResult(KJob *job)
{
    JobData jd = m_jobData.take(job);

    if (job->error())
	return;

    Plasma::DataEngine::Data routes = parseRouteList(jd.networkData);

    setData(jd.sourceName, routes);
}

void RtdDenverEngine::schedulePageResult(KJob *job)
{
    JobData jd = m_jobData.take(job);

    if (job->error())
	return;

    Plasma::DataEngine::Data schedules = parseSchedule(jd.networkData);

    setData(jd.sourceName, schedules);
}

KJob *RtdDenverEngine::fetchSchedule(const QString& query, DayType day, const QString& direction)
{
    QString scheduleUrl = QLatin1String("http://www3.rtd-denver.com/schedules/getSchedule.action?");
    scheduleUrl += query;
    scheduleUrl += QString(QLatin1String("&serviceType=%1")).arg(int(day));

    if (!direction.isEmpty()) {
	scheduleUrl += QLatin1String("&direction=");
	scheduleUrl += direction;
    }

    KJob *fetchJob = KIO::get(KUrl(scheduleUrl), KIO::NoReload, KIO::HideProgressInfo);
    connect(fetchJob, SIGNAL(data(KIO::Job*,QByteArray)), this, SLOT(dataReceived(KIO::Job*,QByteArray)));
    connect(fetchJob, SIGNAL(result(KJob*)), this, SLOT(schedulePageResult(KJob*)));

    return fetchJob;
}

// fetch the route list from RTD, using the JavaScript data structure they
// use to back up the schedule menu on their website
KJob *RtdDenverEngine::fetchRouteList()
{
    KUrl routeListUrl(QLatin1String("http://www3.rtd-denver.com/schedules/ajax/getAjaxRouteMenu.action"));

    KJob *fetchJob = KIO::get(routeListUrl, KIO::NoReload, KIO::HideProgressInfo);
    connect(fetchJob, SIGNAL(data(KIO::Job*,QByteArray)), this, SLOT(dataReceived(KIO::Job*,QByteArray)));
    connect(fetchJob, SIGNAL(result(KJob*)), this, SLOT(routeListResult(KJob*)));

    return fetchJob;
}

static QString dumpJsObj(const QVariant& obj, QString indent = QString());
static QString dumpJsArray(const QVariant& array, QString indent = QString());

static QString dumpJsArray(const QVariant& array, QString indent)
{
    QString ret;
    QVariantList list = qvariant_cast<QVariantList>(array);

    for (int i = 0; i < list.length(); i++) {
	QVariant val = list[i];
	ret += indent;
	switch (val.type()) {
	case QVariant::Map:
	  ret += "{\n" + dumpJsObj(val, indent + "  ");
	  ret += indent + '}';
	  break;
	case QVariant::List:
	  ret += "[\n" + dumpJsArray(val, indent + "  ");
	  ret += indent + ']';
	  break;
	case QVariant::Int:
	case QVariant::Double:
	  ret += val.toString();
	  break;
	default:
	  ret += '"' + val.toString() + '"';
	  break;
	}

	if (i < list.length() - 1)
	  ret += ',';

	ret += '\n';
    }

    return ret;
}

static QString dumpJsObj(const QVariant& obj, QString indent)
{
    QString ret;
    QVariantMap map = qvariant_cast<QVariantMap>(obj);
    QStringList keys = map.keys();

    for (int i = 0; i < keys.length(); i++) {
	ret += indent + keys[i] + ": ";
	QVariant val = map[keys[i]];
	switch (val.type()) {
	case QVariant::Map:
	  ret += "{\n" + dumpJsObj(val, indent + "  ");
	  ret += indent + '}';
	  break;
	case QVariant::List:
	  ret += "[\n" + dumpJsArray(val, indent + "  ");
	  ret += indent + ']';
	  break;
	case QVariant::Int:
	case QVariant::Double:
	  ret += val.toString();
	  break;
	default:
	  ret += '"' + val.toString() + '"';
	  break;
	}

	if (i < keys.length() - 1)
	  ret += ',';

	ret += '\n';
    }

    return ret;
}

// use QtWebKit to parse RTD's "html", and then use some JavaScript to
// pull out the elements we care about
Plasma::DataEngine::Data RtdDenverEngine::parseSchedule(const QByteArray& schedule)
{
    Plasma::DataEngine::Data schedules;

    QString scheduleHtml = QString::fromUtf8(schedule);
    scheduleHtml.remove(QRegExp("<\\s*link[^>]+>"));
    scheduleHtml.remove(QRegExp("<\\s*script[^>]+src\\s*=[^>]>\\s*<\\s*/\\s*script\\s*>"));
    scheduleHtml.remove(QRegExp("<\\s*object[^>]+>"));
    scheduleHtml.remove(QRegExp("<\\s*img[^>]+>"));
    scheduleHtml.remove(QRegExp("<\\s*embed[^>]+>"));

    QWebPage page;
    QWebSettings *settings = page.settings();
    settings->setAttribute(QWebSettings::AutoLoadImages, false);
    settings->setAttribute(QWebSettings::JavascriptEnabled, false);
    settings->setAttribute(QWebSettings::JavaEnabled, false);
    settings->setAttribute(QWebSettings::PluginsEnabled, false);
    settings->setAttribute(QWebSettings::PrivateBrowsingEnabled, true);

    QWebFrame *frame = page.mainFrame();

    frame->setHtml(scheduleHtml);

    QFile parseScript(":/data/parseSchedule.js");
    parseScript.open(QIODevice::ReadOnly);
    QVariant jsRet = frame->evaluateJavaScript(parseScript.readAll());
//    kDebug() << dumpJsObj(jsRet);

    QVariantMap vm = qvariant_cast<QVariantMap>(jsRet);
    for (QVariantMap::const_iterator it = vm.constBegin(); it != vm.constEnd(); it++)
      schedules.insert(it.key(), it.value());

    return schedules;
}

// sort-of parse the JavaScript data structure that the RTD website uses
// for its schedule menu
Plasma::DataEngine::Data RtdDenverEngine::parseRouteList(const QByteArray& scheduleList)
{
    Plasma::DataEngine::Data routes;
    QRegExp urlPattern("\\?(.+)$");

    int nextPos = 0;
    forever {
	int textPos = scheduleList.indexOf("text:", nextPos);
	if (textPos < 0)
	    break;

	int firstQPos = scheduleList.indexOf("\"", textPos+5);
	if (firstQPos < 0)
	    break;

	int secondQPos = scheduleList.indexOf("\"", firstQPos+1);
	if (secondQPos < 0)
	    break;

	QString routeName = QString::fromAscii(scheduleList.mid(firstQPos+1, secondQPos - firstQPos - 1));

	int urlPos = scheduleList.indexOf("url:", secondQPos+1);
	if (urlPos < 0)
	    break;

	firstQPos = scheduleList.indexOf("\"", urlPos+4);
	if (firstQPos < 0)
	    break;

	secondQPos = scheduleList.indexOf("\"", firstQPos+1);
	if (secondQPos < 0)
	    break;

	QString routeUrlPart = QString::fromAscii(scheduleList.mid(firstQPos+1, secondQPos - firstQPos - 1));
	if (urlPattern.indexIn(routeUrlPart) >= 0) {
	    routes.insert(routeName, urlPattern.cap(1));
	}

	nextPos = secondQPos+1;
    }

    return routes;
}

K_EXPORT_PLASMA_DATAENGINE(rtddenver, RtdDenverEngine)

#include "rtddenverengine.moc"
