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
#include <KDE/KStandardDirs>
#include <KDE/KIO/Job>
#include <KDE/KIO/TransferJob>

#include <QtCore/QByteArray>
#include <QtCore/QDataStream>
#include <QtCore/QFile>
#include <QtCore/QPair>
#include <QtCore/QRegExp>
#include <QtCore/QTime>

#include <QtWebKit/QWebFrame>
#include <QtWebKit/QWebPage>

RtdDenverEngine::RtdDenverEngine(QObject *parent, const QVariantList& args)
    : Plasma::DataEngine(parent, args)
{
}

RtdDenverEngine::~RtdDenverEngine()
{
    if (!m_routes.isEmpty())
	saveRouteList();
}

QStringList RtdDenverEngine::sources() const
{
    QStringList ret;

    ret << QLatin1String("Routes") << QLatin1String("ValidAsOf");

    return ret;
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

RtdDenverEngine::DayType RtdDenverEngine::todaysType() const
{
    QDate today = QDate::currentDate();

    if (today.day() == Qt::Saturday)
	return Saturday;
    else if (today.day() == Qt::Sunday || isRtdHoliday(today))
	return SundayHoliday;
    else
	return Weekday;
}

bool RtdDenverEngine::updateSourceEvent(const QString& sourceName)
{
    if (m_pendingValidation.contains(sourceName) || m_pendingRoutes.contains(sourceName))
	return false;

    if (!schedulesValid()) {
	checkValidity();
	m_pendingValidation.append(sourceName);
	return false;
    }

    if (sourceName == QLatin1String("ValidAsOf")) {
	setData(sourceName, m_validAsOf);
	return true;
    }

    if (m_routes.isEmpty() && !loadRouteList()) {
	// we need our route mapping before we can do anything else:
	// request a load of the route list and queue up this source
	if (!alreadyFetchingFor("Routes")) {
	    KJob *fetchJob = fetchRouteList();
	    if (fetchJob) {
		m_jobData.insert(fetchJob, JobData("Routes"));
	    }
	}
	m_pendingRoutes.append(sourceName);

	// we don't have any new information yet
	return false;
    }

    if (sourceName == QLatin1String("Routes")) {
	setData(sourceName, routeList());
	return true;
    } else if (sourceName.startsWith("DirectionOf ")) {
	QStringList parts = sourceName.split(' ');

	if (parts.length() < 2)
	    return false;

      QString routeName = QStringList(parts.mid(1)).join(QLatin1String(" "));
	if (!m_routes.contains(routeName))
	    return false;

	QString directions = m_routes[routeName].directions;

	// we already know which way this route runs
	if (!directions.isEmpty()) {
	    setData(sourceName, directions);
	    return true;
	}

	if (alreadyFetchingFor(sourceName))
	    return true;

	// load a schedule for this route with an unspecified direction: that will
	// tell us the directions for this route
	KJob *fetchJob = fetchSchedule(keyForRoute(routeName), Weekday, 0);
	m_jobData.insert(fetchJob, JobData(sourceName, routeName, Weekday));
	return true;

    } else if (sourceName.startsWith("ScheduleOf ")) {
	QStringList parts = sourceName.split(' ');

	if (parts.length() < 3)
	    return false;

	QString routeName = QStringList(parts.mid(1, parts.length() - 2)).join(QLatin1String(" "));
	if (!m_routes.contains(routeName))
	    return false;

	int direction = parts.last().at(0).unicode();

	Plasma::DataEngine::Data stops = loadSchedule(routeName, todaysType(), direction);

	if (stops.isEmpty()) {
	    if (alreadyFetchingFor(sourceName))
		return true;

	    // we don't have this route cached: load it from the network
	    KJob *fetchJob = fetchSchedule(keyForRoute(routeName), todaysType(), direction);
	    m_jobData.insert(fetchJob, JobData(sourceName, routeName, todaysType()));
	    return true;
	}

	setData(sourceName, stops);
	return true;
    }

    return false;
}

void RtdDenverEngine::checkValidity()
{
    // fetching any route from the network will update our validity timestamp,
    // so we arbitrarily fetch the B (Denver-Boulder) route, since it
    // is highly unlikely to be canceled...

    if (!m_jobData.isEmpty())
	return;  // there's already a running job, it will take care of things for us

    KJob *fetchJob = fetchSchedule("routeId=B", Weekday, 0);

    if (fetchJob)
      m_jobData.insert(fetchJob, JobData(QString()));
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

    QHash<QString, QString> routes = parseRouteList(jd.networkData);

    for (QHash<QString, QString>::const_iterator it = routes.constBegin(); it != routes.constEnd(); it++)
	m_routes.insert(it.key(), RouteData(it.value()));

    // if we had an explicit request for the routes
    if (!jd.sourceName.isEmpty())
	setData(jd.sourceName, routeList());

    while (!m_pendingRoutes.isEmpty())
	updateSourceEvent(m_pendingRoutes.takeFirst());
}

void RtdDenverEngine::schedulePageResult(KJob *job)
{
    JobData jd = m_jobData.take(job);

    if (job->error())
	return;

    // parse the downloaded schedule
    QVariantMap scheduleData = parseSchedule(jd.networkData);

    if (scheduleData.isEmpty())
	return;

    // store the parsed data:
    // first check the schedule's temporal validity
    m_validCheckedDate = QDate::currentDate();
    QDate validAsOf = QDate::fromString(scheduleData["validAsOf"].toString(), QLatin1String("MMMM d, yyyy"));
    if (validAsOf.isValid()) {
	QDate oldValidAsOf = m_validAsOf;
	m_validAsOf = validAsOf;
	if (oldValidAsOf.isValid() && oldValidAsOf != validAsOf)
	    updateAllSources();
    }

//    kDebug() << "availableDirections:" << scheduleData[QLatin1String("availableDirections")].toString()
//	  << "direction:" << scheduleData[QLatin1String("direction")].toString();

    // then record the direction of this route
    if (!jd.routeName.isEmpty() && m_routes[jd.routeName].directions.isEmpty()) {
	QString directions = scheduleData[QLatin1String("availableDirections")].toString();
	m_routes[jd.routeName].directions = directions;
    }

    // finally save the schedules
    if (!jd.routeName.isEmpty()) {
      QString direction = scheduleData[QLatin1String("direction")].toString();
      if (direction == QLatin1String("CW"))
	  direction = "C";
      else if (direction == QLatin1String("CCW"))
	  direction = "c";

      if (!direction.isEmpty())
	  saveSchedule(jd.routeName, jd.routeDay, direction.at(0).unicode(),
		    scheduleData[QLatin1String("schedules")].toMap());
    }

    // call sourceUpdateEvent again to retry the desired source
    if (!jd.sourceName.isEmpty())
	updateSourceEvent(jd.sourceName);

    // and retry any queries waiting for a validity test
    while (!m_pendingValidation.isEmpty())
	updateSourceEvent(m_pendingValidation.takeFirst());
}

// do we already have a fetch underway for the give source name
bool RtdDenverEngine::alreadyFetchingFor(const QString& query)
{
    foreach (const JobData& jd, m_jobData) {
	if (jd.sourceName == query)
	    return true;
    }
    return false;
}

// fetch a schedule for a given route, day, and direction
// direction == 0 means no direction specified
KJob *RtdDenverEngine::fetchSchedule(const QString& query, DayType day, int direction)
{
    QString scheduleUrl = QLatin1String("http://www3.rtd-denver.com/schedules/getSchedule.action?");
    scheduleUrl += query;
    scheduleUrl += QString(QLatin1String("&serviceType=%1")).arg(int(day));

    if (direction) {
	switch (direction) {
	case 'N':
	case 'S':
	case 'E':
	case 'W':
	    scheduleUrl += QString(QLatin1String("&direction=%1-Bound")).arg(QChar(direction));
	    break;
	case 'C':
	    scheduleUrl += QLatin1String("&direction=Clock");
	    break;
	case 'c':
	    scheduleUrl += QLatin1String("&direction=Counterclock");
	    break;
	case 'L':
	    break;
	default:
	    kWarning() << "Unknown direction " << QChar(direction);
	    return 0;
	}
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
    QVariantList list = array.toList();

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
    QVariantMap map = obj.toMap();
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

// use QtWebKit to parse RTD's (buggy) html, and then use some JavaScript to
// pull out the elements we care about
QVariantMap RtdDenverEngine::parseSchedule(const QByteArray& schedule) const
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

    return jsRet.toMap();
}

// sort-of parse the JavaScript data structure that the RTD website uses
// for its schedule menu
QHash<QString, QString> RtdDenverEngine::parseRouteList(const QByteArray& scheduleList) const
{
    QHash<QString, QString> routes;
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

enum {
    ROUTE_LIST_FORMAT_VERSION = 1,
    SCHEDULE_FORMAT_VERSION = 1
};

void RtdDenverEngine::saveRouteList() const
{
    QString routeListPath = KStandardDirs::locateLocal("data", 
			      QLatin1String("plasma_engine_rtddenver/route_list.dat"));
    QFile routeFile(routeListPath);

    if (!routeFile.open(QIODevice::WriteOnly))
	return;

    QDataStream out(&routeFile);
    out << qint32(ROUTE_LIST_FORMAT_VERSION);
    for (QHash<QString, RouteData>::const_iterator it = m_routes.constBegin(); it != m_routes.constEnd(); it++) {
	out << it.key();
	out << it.value().key;
	out << it.value().directions;
    }
}

bool RtdDenverEngine::loadRouteList()
{
    QString routeListPath = KStandardDirs::locateLocal("data", 
			      QLatin1String("plasma_engine_rtddenver/route_list.dat"));
    QFile routeFile(routeListPath);

    if (!routeFile.open(QIODevice::ReadOnly))
	return false;

    QDataStream in(&routeFile);

    qint32 version;
    in >> version;
    if (version != ROUTE_LIST_FORMAT_VERSION)
	return false;

    while (!in.atEnd()) {
	QString route, key, directions;
	in >> route >> key >> directions;
	m_routes.insert(route, RouteData(key, directions));
    }

    return true;
}

QString RtdDenverEngine::dayTypeName(DayType day) const
{
    switch (day) {
    case Weekday:
	return QLatin1String("Weekday");
    case Saturday:
	return QLatin1String("Saturday");
    case SundayHoliday:
	return QLatin1String("SundayHoliday");
    }
    Q_ASSERT_X(false, "RtdDenverEngine::dayTypeName", "bad day type");
    return QLatin1String("unknown");
}

QString RtdDenverEngine::scheduleFilePath(const QString& route, DayType day, int direction) const
{
    QString sanitizedRoute = route;
    sanitizedRoute.replace('/', '_');

    QString fileName = QLatin1String("Schedule-") +
		       sanitizedRoute + '-' + QChar(direction) + '-' +
		       dayTypeName(day) + QLatin1String(".dat");

    return KStandardDirs::locateLocal("data", QLatin1String("plasma_engine_rtddenver/") + fileName);
}

typedef QPair<QTime, QString> TimeRoutePair;
Q_DECLARE_METATYPE(QList<TimeRoutePair>)

static QTime parseRtdTime(const QString& str)
{
    int hr, min;
    int digitCount;

    if (str.length() < 4)
	return QTime();

    for (int i = 0; i < str.length(); i++) {
	if (str[i] < '0' || str[i] > '9')
	    break;
	digitCount++;
    }

    if (digitCount == 3) {
	hr = str.left(1).toInt();
	min = str.mid(1, 2).toInt();
    } else if (digitCount == 4) {
	hr = str.left(2).toInt();
	min = str.mid(2, 2).toInt();
    } else {
	return QTime();
    }

    if (hr == 12 && str[digitCount] == 'A')
	hr -= 12;

    if (str[digitCount] == 'P')
	hr += 12;

    return QTime(hr, min);
}

void RtdDenverEngine::saveSchedule(const QString& route, DayType day, int direction, const QVariantMap& schedule) const
{
    QFile scheduleFile(scheduleFilePath(route, day, direction));

    if (!m_validAsOf.isValid() || !scheduleFile.open(QIODevice::WriteOnly))
	return;

    QDataStream out(&scheduleFile);
    out << qint32(SCHEDULE_FORMAT_VERSION);
    out << m_validAsOf;

    for (QVariantMap::const_iterator it = schedule.constBegin(); it != schedule.constEnd(); it++) {
	out << it.key();

	QList< QPair<QTime, QString> > outputStopList;
	QVariantList stops = it.value().toList();
	foreach (const QVariant& stop, stops) {
	    QVariantMap stopData = stop.toMap();
	    TimeRoutePair p;

	    p.first = parseRtdTime(stopData[QLatin1String("time")].toString().trimmed());

	    if (stopData.contains(QLatin1String("route")))
		p.second = stopData[QLatin1String("route")].toString();
	    else
		p.second = route;

	    if (p.first.isValid())
		outputStopList.append(p);
	}
	out << outputStopList;
    }
}

Plasma::DataEngine::Data RtdDenverEngine::loadSchedule(const QString& route, DayType day, int direction) const
{
    Plasma::DataEngine::Data data;

    QFile scheduleFile(scheduleFilePath(route, day, direction));

    if (!scheduleFile.exists() || !scheduleFile.open(QIODevice::ReadOnly))
	return data;

    QDataStream in(&scheduleFile);

    qint32 version;
    QDate validAsOf;
    in >> version;
    if (version != SCHEDULE_FORMAT_VERSION)
	goto remove;

    in >> validAsOf;
    if (validAsOf != m_validAsOf)
	goto remove;

//    kDebug() << "route:" << route << "direction:" << char(direction);

    while (!in.atEnd()) {
	QString station;
	in >> station;
	QList<TimeRoutePair> stops;
	in >> stops;
	data.insert(station, qVariantFromValue(stops));
    }
    return data;

remove:
    scheduleFile.close();
    scheduleFile.remove();
    return Plasma::DataEngine::Data();
}

K_EXPORT_PLASMA_DATAENGINE(rtddenver, RtdDenverEngine)

#include "rtddenverengine.moc"
