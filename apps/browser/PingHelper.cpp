#include "PingHelper.hpp"
#include "ServerModel.hpp"
#include <QDebug>
#include "PingUpdater.hpp"

void PingHelper::Add(int row, const AddrPair &addrPair)
{
    Add(model, row, addrPair);
}

void PingHelper::Add(QAbstractTableModel *modelToUpdate, int row, const AddrPair &addrPair)
{
    if (modelToUpdate == nullptr)
        return;

    pingUpdater->addServer(modelToUpdate, row, addrPair);
    if (!pingThread->isRunning())
        pingThread->start();
}

void PingHelper::Reset()
{
    //if (pingThread->isRunning())
    Stop();
}

void PingHelper::Stop()
{
    emit pingUpdater->stop();
}

void PingHelper::SetModel(QAbstractTableModel *modelToUse)
{
    model = modelToUse;
}

void PingHelper::update(QAbstractTableModel *modelToUpdate, int row, unsigned ping)
{
    if (modelToUpdate == nullptr || row < 0 || row >= modelToUpdate->rowCount())
        return;

    modelToUpdate->setData(modelToUpdate->index(row, ServerData::PING), ping);
}

PingHelper &PingHelper::Get()
{
    static PingHelper helper;
    return helper;
}

PingHelper::PingHelper()
    : QObject()
    , pingThread(new QThread)
    , pingUpdater(new PingUpdater)
    , model(nullptr)
{
    pingUpdater->moveToThread(pingThread);

    connect(pingThread, SIGNAL(started()), pingUpdater, SLOT(process()));
    connect(pingUpdater, SIGNAL(start()), pingThread, SLOT(start()));
    connect(pingUpdater, SIGNAL(finished()), pingThread, SLOT(quit()));
    connect(this, SIGNAL(stop()), pingUpdater, SLOT(stop()));
    //connect(pingUpdater, SIGNAL(finished()), pingUpdater, SLOT(deleteLater()));
    connect(pingUpdater, &PingUpdater::updateModel, this, &PingHelper::update);
}
