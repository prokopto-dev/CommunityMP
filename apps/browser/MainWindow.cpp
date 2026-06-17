#include "MainWindow.hpp"
#include "QueryHelper.hpp"
#include "PingHelper.hpp"
#include "ServerInfoDialog.hpp"
#include "netutils/Utils.hpp"
#include <components/files/configurationmanager.hpp>
#include <components/openmw-mp/Branding.hpp>
#include <components/openmw-mp/ClientSettings.hpp>
#include <components/settings/settings.hpp>
#include <qdebug.h>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <exception>
#include <optional>

using namespace Process;
using namespace std;

namespace
{
    struct JoinCredentials
    {
        QString accountName;
        QString serverPassword;
    };

    constexpr int maxAccountNameLength = 35;
    constexpr int maxCredentialLength = 256;

    bool hasServerAddress(const ServerModel& model, const QString& addr)
    {
        for (const ServerData& server : model.myData)
        {
            if (server.addr.compare(addr, Qt::CaseInsensitive) == 0)
                return true;
        }

        return false;
    }

    void saveLastPlayerName(const QString& playerName)
    {
        try
        {
            const QByteArray playerNameUtf8 = playerName.toUtf8();
            Settings::Manager::setString("playerName", "General", playerNameUtf8.constData());
            mwmp::ClientSettings::save();
        }
        catch (const std::exception& e)
        {
            qWarning() << "Failed to save last" << mwmp::Branding::productName << "account username:" << e.what();
        }
    }

    std::optional<JoinCredentials> showJoinDialog(QWidget* parent, const QString& address, bool requiresServerPassword)
    {
        QDialog dialog(parent);
        dialog.setWindowTitle(QObject::tr("Join Server"));
        dialog.setMinimumWidth(430);

        auto* layout = new QVBoxLayout(&dialog);

        auto* intro = new QLabel(QObject::tr(
            "Choose the server account username for %1. The game sign-in panel asks for that account's password "
            "before connecting; character names are loaded separately.").arg(address));
        intro->setWordWrap(true);
        layout->addWidget(intro);

        auto* form = new QFormLayout();
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        auto* accountNameEdit = new QLineEdit(QString::fromStdString(Settings::Manager::getString("playerName", "General")));
        accountNameEdit->setMaxLength(maxAccountNameLength);
        accountNameEdit->setPlaceholderText(QObject::tr("Account username"));
        form->addRow(QObject::tr("Account username:"), accountNameEdit);

        auto* serverPasswordEdit = new QLineEdit();
        serverPasswordEdit->setMaxLength(maxCredentialLength);
        serverPasswordEdit->setEchoMode(QLineEdit::Password);
        serverPasswordEdit->setPlaceholderText(QObject::tr("Join password"));
        if (requiresServerPassword)
            form->addRow(QObject::tr("Join password:"), serverPasswordEdit);

        layout->addLayout(form);

        if (requiresServerPassword)
        {
            auto* passwordNote = new QLabel(QObject::tr(
                "This password unlocks the server connection only. It is separate from your account password."));
            passwordNote->setWordWrap(true);
            layout->addWidget(passwordNote);
        }

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Join"));
        layout->addWidget(buttons);

        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
            [&dialog, accountNameEdit, serverPasswordEdit, requiresServerPassword]() {
            if (accountNameEdit->text().trimmed().isEmpty())
            {
                QMessageBox::warning(&dialog, QObject::tr("Missing account username"),
                    QObject::tr("Enter an account username before joining a server."));
                accountNameEdit->setFocus();
                return;
            }

            if (requiresServerPassword && serverPasswordEdit->text().isEmpty())
            {
                QMessageBox::warning(&dialog, QObject::tr("Missing server password"),
                    QObject::tr("Enter this server's password before joining."));
                serverPasswordEdit->setFocus();
                return;
            }

            dialog.accept();
        });
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (requiresServerPassword && !accountNameEdit->text().trimmed().isEmpty())
            serverPasswordEdit->setFocus();
        else
        {
            accountNameEdit->setFocus();
            accountNameEdit->selectAll();
        }

        if (dialog.exec() != QDialog::Accepted)
            return std::nullopt;

        return JoinCredentials{ accountNameEdit->text().trimmed(), serverPasswordEdit->text() };
    }
}

MainWindow::MainWindow(QWidget *parent)
{
    setupUi(this);
    setWindowTitle(QString::fromUtf8(mwmp::Branding::productName) + QStringLiteral(" Server Browser"));

    mGameInvoker = new ProcessInvoker();

    browser = new ServerModel;
    favorites = new ServerModel;
    proxyModel = new MySortFilterProxyModel(this);
    proxyModel->setSourceModel(browser);
    tblServerBrowser->setModel(proxyModel);
    tblFavorites->setModel(proxyModel);

    tblServerBrowser->hideColumn(ServerData::ADDR);
    tblFavorites->hideColumn(ServerData::ADDR);

    PingHelper::Get().SetModel((ServerModel*)proxyModel->sourceModel());
    queryHelper = new QueryHelper(proxyModel->sourceModel());
    connect(queryHelper, &QueryHelper::started, [this](){actionRefresh->setEnabled(false);});
    connect(queryHelper, &QueryHelper::finished, [this](){actionRefresh->setEnabled(true);});

    connect(tabWidget, SIGNAL(currentChanged(int)), this, SLOT(tabSwitched(int)));
    connect(actionAdd, SIGNAL(triggered(bool)), this, SLOT(addServer()));
    connect(actionAdd_by_IP, SIGNAL(triggered(bool)), this, SLOT(addServerByIP()));
    connect(actionDelete, SIGNAL(triggered(bool)), this, SLOT(deleteServer()));
    connect(actionRefresh, SIGNAL(triggered(bool)), queryHelper, SLOT(refresh()));
    connect(actionPlay, SIGNAL(triggered(bool)), this, SLOT(play()));
    connect(tblServerBrowser, SIGNAL(clicked(QModelIndex)), this, SLOT(serverSelected()));
    connect(tblFavorites, SIGNAL(clicked(QModelIndex)), this, SLOT(serverSelected()));
    connect(tblFavorites, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(play()));
    connect(tblServerBrowser, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(play()));
    connect(cBoxNotFull, SIGNAL(toggled(bool)), this, SLOT(notFullSwitch(bool)));
    connect(cBoxWithPlayers, SIGNAL(toggled(bool)), this, SLOT(havePlayersSwitch(bool)));
    connect(cBBoxWOPass, SIGNAL(toggled(bool)), this, SLOT(noPasswordSwitch(bool)));
    connect(comboLatency, SIGNAL(currentIndexChanged(int)), this, SLOT(maxLatencyChanged(int)));
    connect(leGamemode, SIGNAL(textChanged(const QString &)), this, SLOT(gamemodeChanged(const QString &)));
    loadFavorites();
    queryHelper->refresh();
}

MainWindow::~MainWindow()
{
    delete queryHelper;
    delete mGameInvoker;
}

void MainWindow::addServerAndUpdate(const QString &addr)
{
    const AddrPair server = splitServerAddress(addr);
    if (server.first.isEmpty())
        return;

    const QString normalizedAddr = formatServerAddress(server);
    if (hasServerAddress(*favorites, normalizedAddr))
        return;

    ServerData serverData;
    serverData.addr = normalizedAddr;
    serverData.SetName(normalizedAddr.toUtf8());
    serverData.ping = PING_UNREACHABLE;

    favorites->insertRow(0);
    favorites->setServerData(0, serverData);

    PingHelper::Get().Add(favorites, 0, server);
}

void MainWindow::addServer()
{
    int id = tblServerBrowser->selectionModel()->currentIndex().row();

    if (id >= 0)
    {
        int sourceId = proxyModel->mapToSource(proxyModel->index(id, ServerData::ADDR)).row();
        const ServerData& serverData = browser->myData[sourceId];
        if (hasServerAddress(*favorites, serverData.addr))
            return;

        favorites->insertRow(favorites->rowCount());
        favorites->setServerData(favorites->rowCount() - 1, serverData);
    }
}

void MainWindow::addServerByIP()
{
    bool ok;
    QString text = QInputDialog::getText(this, tr("Add Server by address"), tr("Address:"), QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty())
        addServerAndUpdate(text);
}

void MainWindow::deleteServer()
{
    if (tabWidget->currentIndex() != 1)
        return;
    int id = tblFavorites->selectionModel()->currentIndex().row();
    if (id >= 0)
    {
        int sourceId = proxyModel->mapToSource(proxyModel->index(id, ServerData::ADDR)).row();
        favorites->removeRow(sourceId);
        if (favorites->myData.isEmpty())
        {
            actionPlay->setEnabled(false);
            actionDelete->setEnabled(false);
        }
    }
}

void MainWindow::play()
{
    QTableView *curTable = tabWidget->currentIndex() ? tblFavorites : tblServerBrowser;
    int id = curTable->selectionModel()->currentIndex().row();
    if (id < 0)
        return;


    ServerModel *sm = ((ServerModel*)proxyModel->sourceModel());

    int sourceId = proxyModel->mapToSource(proxyModel->index(id, ServerData::ADDR)).row();
    ServerInfoDialog infoDialog(sm->myData[sourceId].addr, this);

    if (!infoDialog.exec())
        return;

    if (!infoDialog.isUpdated())
        return;

    QStringList arguments;
    arguments.append(QLatin1String("--client"));
    arguments.append(QLatin1String("--connect=") + sm->myData[sourceId].addr.toLatin1());

    const auto credentials = showJoinDialog(this, sm->myData[sourceId].addr, sm->myData[sourceId].GetPassword() == 1);
    if (!credentials)
        return;

    saveLastPlayerName(credentials->accountName);
    arguments.append(QStringLiteral("--name=") + credentials->accountName);

    if (sm->myData[sourceId].GetPassword() == 1)
        arguments.append(QLatin1String("--password=") + credentials->serverPassword.toLatin1());

    if (mGameInvoker->startProcess(QLatin1String("communitymp"), arguments, true))
        return qApp->quit();
}

void MainWindow::tabSwitched(int index)
{
    if (index == 0)
    {
        proxyModel->setSourceModel(browser);
        PingHelper::Get().SetModel(browser);
        actionDelete->setEnabled(false);
    }
    else
    {
        proxyModel->setSourceModel(favorites);
        PingHelper::Get().SetModel(favorites);
    }
    actionPlay->setEnabled(false);
    actionAdd->setEnabled(false);
}

void MainWindow::serverSelected()
{
    actionPlay->setEnabled(true);
    if (tabWidget->currentIndex() == 0)
        actionAdd->setEnabled(true);
    if (tabWidget->currentIndex() == 1)
        actionDelete->setEnabled(true);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    Files::ConfigurationManager cfgMgr;
    QString cfgPath = QString::fromStdString((cfgMgr.getUserConfigPath() / "favorites.dat").string());

    QJsonArray saveData;
    for (auto server : favorites->myData)
        saveData.push_back(server.addr);

    QFile file(cfgPath);

    if (!file.open(QIODevice::WriteOnly))
    {
        qDebug() << "Cannot save " << cfgPath;
        return;
    }

    file.write(QJsonDocument(saveData).toJson());
    file.close();
}


void MainWindow::loadFavorites()
{
    Files::ConfigurationManager cfgMgr;
    QString cfgPath = QString::fromStdString((cfgMgr.getUserConfigPath() / "favorites.dat").string());

    QFile file(cfgPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qDebug() << "Cannot open " << cfgPath;
        return;
    }

    QJsonDocument jsonDoc(QJsonDocument::fromJson(file.readAll()));

    for (auto server : jsonDoc.array())
        addServerAndUpdate(server.toString());

    file.close();
}

void MainWindow::notFullSwitch(bool state)
{
    proxyModel->filterFullServer(state);
}

void MainWindow::havePlayersSwitch(bool state)
{
    proxyModel->filterEmptyServers(state);
}

void MainWindow::noPasswordSwitch(bool state)
{
    proxyModel->filterPassworded(state);
}

void MainWindow::maxLatencyChanged(int index)
{
    int maxLatency = index * 50;
    proxyModel->pingLessThan(maxLatency);

}

void MainWindow::gamemodeChanged(const QString &text)
{
    proxyModel->setFilterFixedString(text);
    proxyModel->setFilterKeyColumn(ServerData::MODNAME);
}
