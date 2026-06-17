#include <apps/browser/netutils/QueryClient.hpp>
#include "qdebug.h"

#include "ServerInfoDialog.hpp"
#include <algorithm>
#include <utility>
#include <QByteArray>
#include <QThread>

using namespace std;
using namespace mwmp;

ThrWorker::ThrWorker(ServerInfoDialog *dialog, QString addr, unsigned short port): addr(std::move(addr)), port(port), stopped(false)
{
    this->dialog = dialog;
}

void ThrWorker::process()
{
    stopped = false;
    const QByteArray address = addr.toUtf8();
    auto newSD = QueryClient::Get().Update(makePacketAddress(address.constData(), port));
    const bool directReachable = !isPacketAddressAssigned(newSD.first)
        && PingServer(address.constData(), port) != PING_UNREACHABLE;

    if (dialog != nullptr)
    {
        dialog->setData(newSD);
        dialog->setDirectReachable(directReachable);
    }
    stopped = true;
    emit finished();
}

ServerInfoDialog::ServerInfoDialog(const QString &addr, QWidget *parent): QDialog(parent)
{
    setupUi(this);
    refreshThread = new QThread;

    const AddrPair server = splitServerAddress(addr);
    mAddr = server.first;
    mPort = server.second;
    mDirectReachable = false;
    sd.first = unassignedPacketAddress();

    worker = new ThrWorker(this, mAddr, mPort);
    worker->moveToThread(refreshThread);
    connect(refreshThread, SIGNAL(started()), worker, SLOT(process()));
    connect(worker, SIGNAL(finished()), refreshThread, SLOT(quit()));
    connect(refreshThread, SIGNAL(finished()), this, SLOT(refresh()));

    connect(btnRefresh, &QPushButton::clicked, [this]{
        if (!refreshThread->isRunning())
            refreshThread->start();
    });
}

ServerInfoDialog::~ServerInfoDialog()
{
    worker->dialog = nullptr;
    if (refreshThread->isRunning())
    {
        refreshThread->quit();
        refreshThread->wait();
    }
}

bool ServerInfoDialog::isUpdated()
{
    return isPacketAddressAssigned(sd.first) || mDirectReachable;
}

void ServerInfoDialog::setData(std::pair<PacketAddress, QueryData> &newSD)
{
    sd = newSD;
}

void ServerInfoDialog::setDirectReachable(bool reachable)
{
    mDirectReachable = reachable;
}

void ServerInfoDialog::refresh()
{
    const bool hasMasterData = isPacketAddressAssigned(sd.first);
    if (hasMasterData || mDirectReachable)
    {
        const QByteArray address = hasMasterData ? QByteArray(packetAddressToString(sd.first, false).c_str()) : mAddr.toUtf8();
        const unsigned short port = hasMasterData ? packetAddressPort(sd.first) : mPort;

        leAddr->setText(hasMasterData ? QString::fromStdString(packetAddressToString(sd.first, true, ':')) : formatServerAddress(AddrPair(mAddr, mPort)));
        lblName->setText(hasMasterData ? QString::fromUtf8(sd.second.GetName()) : mAddr);
        int ping = PingServer(address.constData(), port);
        lblPing->setNum(ping);
        btnConnect->setDisabled(ping == PING_UNREACHABLE);

        listPlayers->clear();
        if (hasMasterData)
        {
            for (const auto &player : sd.second.players)
                listPlayers->addItem(QString::fromStdString(player));
        }

        listPlugins->clear();
        if (hasMasterData)
        {
            for (const auto &plugin : sd.second.plugins)
                listPlugins->addItem(QString::fromStdString(plugin.name));
        }

        listRules->clear();
        if (hasMasterData)
        {
            const static vector<std::string> defaultRules {"gamemode", "maxPlayers", "name", "passw", "players", "version"};
            for (auto &rule : sd.second.rules)
            {
                if (::find(defaultRules.begin(), defaultRules.end(), rule.first) != defaultRules.end())
                    continue;
                QString ruleStr = QString::fromStdString(rule.first) + " : ";
                if (rule.second.type == 's')
                    ruleStr += QString::fromStdString(rule.second.str);
                else
                    ruleStr += QString::number(rule.second.val);
                listRules->addItem(ruleStr);
            }
        }

        lblPlayers->setText(hasMasterData
            ? QString::number(sd.second.players.size()) + " / " + QString::number(sd.second.GetMaxPlayers())
            : QStringLiteral("0 / 0"));
    }
}

int ServerInfoDialog::exec()
{
    if (!refreshThread->isRunning())
        refreshThread->start();
    return QDialog::exec();
}
