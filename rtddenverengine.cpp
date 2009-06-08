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

RtdDenverEngine::DayType RtdDenverEngine::dayType(TodayTomorrow tt) const
{
    QDate today = QDate::currentDate();

    if (tt == Tomorrow)
	today.addDays(1);

    if (today.day() == Qt::Saturday)
	return Saturday;
    else if (today.day() == Qt::Sunday || isRtdHoliday(today))
	return SundayHoliday;
    else
	return Weekday;
}

static int directionFromCode(const QString& directionCode)
{
    if (directionCode == QLatin1String("N"))
	return 'N';
    else if (directionCode == QLatin1String("S"))
	return 'S';
    else if (directionCode == QLatin1String("E"))
	return 'E';
    else if (directionCode == QLatin1String("W"))
	return 'W';
    else if (directionCode == QLatin1String("?"))
	return '?';
    else if (directionCode == QLatin1String("CW"))
	return 'C';
    else if (directionCode == QLatin1String("CCW"))
	return 'c';
    else if (directionCode == QLatin1String("Loop"))
	return 'L';
    return 0;
}

bool RtdDenverEngine::updateSourceEvent(const QString& sourceName)
{
    if (m_pendingRoutes.contains(sourceName))
	return true;

    if (m_routes.isEmpty() && !loadRouteList()) {
	// we need our route mapping before we can do anything else:
	// request a load of the route list and queue up this source
	if (m_pendingRoutes.isEmpty()) {
	    KJob *fetchJob = fetchRouteList();
	    if (fetchJob) {
		m_jobData.insert(fetchJob, JobData());
	    }
	}
	m_pendingRoutes.insert(sourceName);
	return true;
    }

    // before we try to load things from cache, we need to know our cache validity
    if (!schedulesValid()) {
	// we haven't loaded anything in the last day: do a network load to recheck
	// our schedule validity
	checkValidity(sourceName);
	return true;
    }

    if (sourceName == QLatin1String("ValidAsOf")) {
	// "ValidAsOf": returns the date as of which our bus schedules are valid
	setData(sourceName, m_validAsOf);
	return true;
    } else if (sourceName == QLatin1String("Routes")) {
	// "Routes": returns the list of route names
	setData(sourceName, routeList());
	return true;
    } else if (sourceName.startsWith("DirectionOf ")) {
	// "DirectionOf routeName": returns the direction code for the
	// direction(s) of the route @p routeName, i.e. "N", "S", "E", "W", "Loop", "CW",
	// or "CCW"; a two-direction route joins its directions with a hyphen, e.g.
	// "N-S", "E-W", or "CW-CCW"
	QString routeName = sourceName.mid(12);
	if (!m_routes.contains(routeName))
	    return false;

	QString directions = m_routes[routeName].directions;

	// we already know which way this route runs
	if (!directions.isEmpty()) {
	    setData(sourceName, directions);
	    return true;
	}

	// load a schedule for this route with an unspecified direction: that will
	// tell us the directions for this route
	return setupScheduleFetch(sourceName, routeName + "-?", Weekday);
    } else if (sourceName.startsWith("ScheduleOf ")) {
	// "ScheduleOf routeName-directionCode": returns a map of <stop name, timetable>
	// for all the stops of the route @p routeName going in the direction @p
	// directionCode. The timetable is a sorted list of QPair<QTime, QString>
	// where the time is the arrival time of the bus or train, and the QString
	// is the subroute of that bus or train (e.g. B, BF, or BX). Note that the list
	// is sorted by arrival time, so stops storted with A.M. times after stops with
	// P.M. times are actually arriving on the next day.
	QString fullRouteName = sourceName.mid(11);

	// try to load the schedule from cache
	Plasma::DataEngine::Data stops = loadSchedule(fullRouteName, dayType(Today));

	// no cached data: go to the network
	if (stops.isEmpty())
	    return setupScheduleFetch(sourceName, fullRouteName, dayType(Today));

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

    return false;
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

    // we've got data
    setData(QLatin1String("Routes"), routeList());

    // tell the sources that were waiting for the route list to retry
    foreach (const QString& sourceName, m_pendingRoutes)
	updateSourceEvent(sourceName);
    m_pendingRoutes.clear();
}

void RtdDenverEngine::maybeRetrySource(const QString& sourceName, KJob *completedJob)
{
    Q_ASSERT(m_pendingSchedules[sourceName].contains(completedJob));
    m_pendingSchedules[sourceName].remove(completedJob);

    // if this source is not waiting on anything else, retry it
    if (m_pendingSchedules[sourceName].isEmpty()) {
	m_pendingSchedules.remove(sourceName);
	updateSourceEvent(sourceName);
    }
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
	// we've got a known validity: if it's new, refresh everything
	QDate oldValidAsOf = m_validAsOf;
	m_validAsOf = validAsOf;
	if (oldValidAsOf.isValid() && oldValidAsOf != validAsOf) {
	    setData("ValidAsOf", m_validAsOf);
	    updateAllSources();
	} else if (!oldValidAsOf.isValid()) {
	    setData("ValidAsOf", m_validAsOf);
	}
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

    // let each source that is waiting for us know that we're done
    foreach (const QString& sourceName, jd.pendingSources)
	maybeRetrySource(sourceName, job);
}

// request a schedule from the network or join a pending fetch of the same schedule, as needed
bool RtdDenverEngine::setupScheduleFetch(const QString& sourceName, const QString& fullRouteName, DayType day)
{
    int hyphenPos = fullRouteName.indexOf('-');
    if (hyphenPos < 0)
	return false;

    QString routeName = fullRouteName.left(hyphenPos);
    QString directionCode = fullRouteName.mid(hyphenPos + 1);
    int direction = directionFromCode(directionCode);

    if (!m_routes.contains(routeName))
	return false;

    // see if there's already a pending network load for this job
    for (QMap<KJob *, JobData>::iterator it = m_jobData.begin(); it != m_jobData.end(); it++) {
	if (it.value().routeName == routeName && it.value().routeDay == day &&
	    it.value().direction == direction) {
	    // there is: add us to its queue and note that we're waiting on it
	    // (if we aren't already...)
	    if (it.value().pendingSources.contains(sourceName))
		return true;

	    it.value().pendingSources.insert(sourceName);
	    m_pendingSchedules[sourceName].insert(it.key());
	    return true;
	}
    }

    // no pending load: set one up
    KJob *fetchJob = fetchSchedule(keyForRoute(routeName), day, direction);
    if (!fetchJob)
	return false;

    // store the parameters of this job and note that this source is waiting on it
    m_jobData.insert(fetchJob, JobData(sourceName, routeName, day, direction));
    m_pendingSchedules[sourceName].insert(fetchJob);
    kDebug() << "load for " << sourceName << "is " << fetchJob;
    return true;
}

// do a direct network load to check our cache validity timestamp
void RtdDenverEngine::checkValidity(const QString& sourceName)
{
    // if there's already a pending network load of a schedule page, we can
    // piggy-back off of it
    for (QMap<KJob *, JobData>::iterator it = m_jobData.begin(); it != m_jobData.end(); it++) {
	if (!it.value().routeName.isEmpty()) {
	    it.value().pendingSources.insert(sourceName);
	    m_pendingSchedules[sourceName].insert(it.key());
	    return;
	}
    }

    // if there's no network load going, we have to kick one off
    // since we may not nave the route list yet, we have to fully specify the load
    // ourselves -- the B/BF/BX route (Denver-Boulder) is unlikely to ever be canceled,
    // so we'll use it by default
    KJob *fetchJob = fetchSchedule(QLatin1String("routeId=B"), Weekday, 'W');
    if (!fetchJob)
	return;

    m_jobData.insert(fetchJob, JobData(sourceName, "B/BF/BX", Weekday, 'W'));
    m_pendingSchedules[sourceName].insert(fetchJob);
}

// actually perform a network fetch of a schedule for a given route, day, and direction
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
	case '?':
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

Plasma::DataEngine::Data RtdDenverEngine::loadSchedule(const QString& fullRouteName, DayType day) const
{
    Plasma::DataEngine::Data data;

//    kDebug() << "trying to load " << fullRouteName;
    QStringList parts = fullRouteName.split('-');
    if (parts.length() != 2)
	return data;

    QFile scheduleFile(scheduleFilePath(parts.first(), day, directionFromCode(parts.last())));

    if (!scheduleFile.exists() || !scheduleFile.open(QIODevice::ReadOnly))
	return data;

//    kDebug() << "cache file found";
    QDataStream in(&scheduleFile);

    qint32 version;
    QDate validAsOf;
    in >> version;
    if (version != SCHEDULE_FORMAT_VERSION)
	goto remove;

    in >> validAsOf;
    if (validAsOf != m_validAsOf)
	goto remove;


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
