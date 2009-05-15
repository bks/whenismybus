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
#include <QtCore/QMap>
#include <QtCore/QString>

#include <Plasma/DataEngine>

class KJob;
namespace KIO { class Job; };

class RtdDenverEngine : public Plasma::DataEngine
{
    Q_OBJECT

    public:
	RtdDenverEngine(QObject *parent, const QVariantList& args);

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
	DayType parseDay(const QString& day);

	KJob *fetchSchedule(const QString& query, DayType day, const QString& direction = QString());
	KJob *fetchRouteList();

	Plasma::DataEngine::Data parseSchedule(const QByteArray& schedule);
	Plasma::DataEngine::Data parseRouteList(const QByteArray& routeList);

	struct JobData {
	    QString sourceName;
	    QByteArray networkData;
	};
	QMap<KJob *, JobData> m_jobData;
};

#endif
