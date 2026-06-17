#ifndef OPENMW_TYPES_HPP
#define OPENMW_TYPES_HPP

#include <QPair>
#include <QString>

#include <components/openmw-mp/Endpoint.hpp>

typedef QPair <QString, unsigned short> AddrPair;

inline AddrPair splitServerAddress(QString addr)
{
    const mwmp::ServerEndpoint endpoint = mwmp::parseServerEndpoint(addr.toStdString());
    return { QString::fromStdString(endpoint.host), endpoint.port };
}

inline QString formatServerAddress(const AddrPair& addr)
{
    return QString::fromStdString(mwmp::formatServerEndpoint(addr.first.toStdString(), addr.second));
}

#endif //OPENMW_TYPES_HPP
