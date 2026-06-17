#ifndef OPENMW_PINGUPDATER_HPP
#define OPENMW_PINGUPDATER_HPP

#include <QObject>
#include <QAbstractTableModel>
#include <QMutex>
#include <QVector>

#include "Types.hpp"

struct PingRequest
{
    QAbstractTableModel *model;
    int row;
    AddrPair address;
};

class PingUpdater : public QObject
{
    Q_OBJECT
public:
    void addServer(QAbstractTableModel *model, int row, const AddrPair &addrPair);
public slots:
    void stop();
    void process();
signals:
    void start();
    void updateModel(QAbstractTableModel *model, int row, unsigned ping);
    void finished();
private:
    QVector<PingRequest> servers;
    QMutex mutex;
    bool run = false;
};


#endif //OPENMW_PINGUPDATER_HPP
