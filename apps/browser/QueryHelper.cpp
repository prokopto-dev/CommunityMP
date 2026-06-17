#include "netutils/QueryClient.hpp"
#include "netutils/Utils.hpp"
#include "QueryHelper.hpp"
#include "PingHelper.hpp"
#include <components/openmw-mp/Transport/PacketIdentity.hpp>

QueryHelper::QueryHelper(QAbstractItemModel *model)
{
    qRegisterMetaType<QueryData>("QueryData");
    queryThread = new QThread;
    queryUpdate = new QueryUpdate;
    _model = model;
    connect(queryThread, SIGNAL(started()), queryUpdate, SLOT(process()));
    connect(queryUpdate, SIGNAL(finished()), queryThread, SLOT(quit()));
    connect(queryUpdate, &QueryUpdate::finished, [this](){emit finished();});
    connect(queryUpdate, SIGNAL(updateModel(const QString&, unsigned short, const QueryData&)),
            this, SLOT(update(const QString&, unsigned short, const QueryData&)));
    queryUpdate->moveToThread(queryThread);
}

QueryHelper::~QueryHelper()
{
    terminate();
    queryUpdate->moveToThread(QThread::currentThread());
    delete queryUpdate;
    delete queryThread;
}

void QueryHelper::refresh()
{
    if (!queryThread->isRunning())
    {
        _model->removeRows(0, _model->rowCount());
        PingHelper::Get().Stop();
        queryThread->start();
        emit started();
    }
}

void QueryHelper::terminate()
{
    if (!queryThread->isRunning())
        return;

    queryThread->quit();
    queryThread->wait();
}

void QueryHelper::update(const QString &addr, unsigned short port, const QueryData& data)
{
    ServerModel *model = ((ServerModel*)_model);
    model->insertRow(model->rowCount());
    int row = model->rowCount() - 1;

    QModelIndex mi = model->index(row, ServerData::ADDR);
    model->setData(mi, addr + ":" + QString::number(port));

    mi = model->index(row, ServerData::PLAYERS);
    model->setData(mi, (int)data.players.size());

    mi = model->index(row, ServerData::MAX_PLAYERS);
    model->setData(mi, data.GetMaxPlayers());

    mi = model->index(row, ServerData::HOSTNAME);
    model->setData(mi, data.GetName());

    mi = model->index(row, ServerData::MODNAME);
    model->setData(mi, data.GetGameMode());

    mi = model->index(row, ServerData::VERSION);
    model->setData(mi, data.GetVersion());

    mi = model->index(row, ServerData::PASSW);
    model->setData(mi, data.GetPassword() == 1);

    mi = model->index(row, ServerData::PING);
    model->setData(mi, PING_UNREACHABLE);
    PingHelper::Get().Add(model, row, {addr, port});
}

void QueryUpdate::process()
{
    auto data = QueryClient::Get().Query();
    if (QueryClient::Get().Status() != ID_MASTER_QUERY)
    {
        emit finished();
        return;
    }

    for (const auto &server : data)
        emit updateModel(QString::fromStdString(mwmp::packetAddressToString(server.first, false)),
            mwmp::packetAddressPort(server.first), server.second);
    emit finished();
}
