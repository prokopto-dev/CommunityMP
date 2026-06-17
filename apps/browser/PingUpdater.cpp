#include "PingUpdater.hpp"
#include "netutils/Utils.hpp"
#include <QDebug>
#include <QModelIndex>
#include <QMutexLocker>
#include <QThread>

void PingUpdater::stop()
{
    QMutexLocker lock(&mutex);
    servers.clear();
    run = false;
}

void PingUpdater::addServer(QAbstractTableModel *model, int row, const AddrPair &addr)
{
    {
        QMutexLocker lock(&mutex);
        servers.push_back({model, row, addr});
        run = true;
    }

    emit start();
}

void PingUpdater::process()
{
    while (true)
    {
        PingRequest server = {nullptr, -1, {}};

        {
            QMutexLocker lock(&mutex);
            if (!run)
                break;

            if (!servers.isEmpty())
            {
                server = servers.back();
                servers.pop_back();
            }
        }

        if (server.model == nullptr)
        {
            QThread::msleep(1000);

            QMutexLocker lock(&mutex);
            if (!run)
                break;

            if (servers.isEmpty())
            {
                qDebug() << "PingUpdater stopped due to inactivity";
                run = false;
            }

            continue;
        }

        unsigned ping = PingServer(server.address.first.toLatin1(), server.address.second);

        qDebug() << "Pong from" << server.address.first + "|" + QString::number(server.address.second)
                 << ":" << ping << "ms";

        emit updateModel(server.model, server.row, ping);
    }
    emit finished();
}
