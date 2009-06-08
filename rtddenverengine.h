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

#ifndef RTDDENVERENGINE_H
#define RTDDENVERENGINE_H

#include <QtCore/QByteArray>
#include <QtCore/QDate>
#include <QtCore/QHash>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <Plasma/DataEngine>

class KJob;
namespace KIO { class Job; };

class RtdDenverEngine : public Plasma::DataEngine
{
    Q_OBJECT

    public:
	RtdDenverEngine(QObject *parent, const QVariantList& args);
	~RtdDenverEngine();
	QStringList sources() const;

    protected:
	bool sourceRequestEvent(const QString& sourceName);
        bool updateSourceEvent(const QString& sourceName);

    private slots:
	void dataReceived(KIO::Job *job, const QByteArray& data);
	void routeListResult(KJob *job);
	void schedulePageResult(KJob *job);

    private:
	enum DayType {
	    Saturday = 1,
	    SundayHoliday = 2,
	    Weekday = 3
	};
	enum TodayTomorrow {
	    Today,
	    Tomorrow
	};
	DayType dayType(TodayTomorrow tt) const;
	QString dayTypeName(DayType d) const;

	bool schedulesValid() const { return (m_validCheckedDate == QDate::currentDate()); }
	void checkValidity();

	bool alreadyFetchingFor(const QString& sourceName);
	KJob *fetchSchedule(const QString& routeKey, DayType day, int direction);
	KJob *fetchRouteList();

	QVariantMap parseSchedule(const QByteArray& schedule) const;
	QHash<QString, QString> parseRouteList(const QByteArray& routeList) const;

	void saveRouteList() const;
	bool loadRouteList();
	QString keyForRoute(const QString& route) const { return m_routes[route].key; }
	QStringList routeList() const { return m_routes.keys(); }

	QString scheduleFilePath(const QString& route, DayType day, int direction) const;
	void saveSchedule(const QString& route, DayType day, int direction, const QVariantMap& schedule) const;
	Plasma::DataEngine::Data loadSchedule(const QString& route, DayType day, int direction) const;

	struct JobData {
	    QString sourceName;
	    QString routeName;
	    DayType routeDay;
	    QByteArray networkData;

	    JobData() { }
	    JobData(const QString& n) : sourceName(n) { }
	    JobData(const QString& n, const QString& r, DayType d)
	      : sourceName(n), routeName(r), routeDay(d) { }
	};

	QMap<KJob *, JobData> m_jobData;

	struct RouteData {
	    QString key;
	    QString directions;

	    RouteData() { }
	    RouteData(const QString& k) : key(k) { }
	    RouteData(const QString& k, const QString& d) : key(k), directions(d) { }
	};

	QHash<QString, RouteData> m_routes;
	QStringList m_pendingValidation;
	QStringList m_pendingRoutes;
	QDate m_validCheckedDate;
	QDate m_validAsOf;
};

#endif
