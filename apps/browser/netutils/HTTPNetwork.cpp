#include "HTTPNetwork.hpp"

#include <QByteArray>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace
{
    QUrl makeUrl(const std::string& address, unsigned short port, const char* uri)
    {
        const QString uriText = QString::fromUtf8(uri != nullptr ? uri : "");
        const QUrl directUrl(uriText);
        if (directUrl.isValid() && !directUrl.scheme().isEmpty())
            return directUrl;

        const QString addressText = QString::fromStdString(address);
        QUrl baseUrl(addressText);
        if (!baseUrl.isValid() || baseUrl.scheme().isEmpty())
        {
            baseUrl = QUrl();
            baseUrl.setScheme("http");
            baseUrl.setHost(addressText);
            baseUrl.setPort(port);
        }
        else if (baseUrl.port() == -1)
            baseUrl.setPort(port);

        if (!uriText.isEmpty())
        {
            if (uriText.startsWith('?'))
                baseUrl.setQuery(uriText.mid(1));
            else
                baseUrl = baseUrl.resolved(QUrl(uriText.startsWith('/') ? uriText : "/" + uriText));
        }

        return baseUrl;
    }

    std::string errorName(QNetworkReply::NetworkError error)
    {
        switch (error)
        {
            case QNetworkReply::NoError:
                return {};
            case QNetworkReply::HostNotFoundError:
            case QNetworkReply::ProtocolInvalidOperationError:
            case QNetworkReply::UnknownNetworkError:
                return "UNKNOWN_ADDRESS";
            case QNetworkReply::ConnectionRefusedError:
                return "FAIL_CONNECT";
            case QNetworkReply::RemoteHostClosedError:
            case QNetworkReply::NetworkSessionFailedError:
            case QNetworkReply::TimeoutError:
                return "LOST_CONNECTION";
            default:
                return "FAIL_CONNECT";
        }
    }

    std::string finishReply(QNetworkReply* reply, QTimer& timeoutTimer)
    {
        if (timeoutTimer.isActive())
            timeoutTimer.stop();

        const std::string mappedError = errorName(reply->error());
        if (!mappedError.empty())
        {
            reply->deleteLater();
            return mappedError;
        }

        const QByteArray response = reply->readAll();
        reply->deleteLater();

        if (response.isEmpty())
            return "NO_CONTENT";

        return std::string(response.constData(), static_cast<std::size_t>(response.size()));
    }

    std::string runRequest(QNetworkReply* reply)
    {
        if (reply == nullptr)
            return "UNKNOWN_ADDRESS";

        QEventLoop eventLoop;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
        QObject::connect(&timeoutTimer, &QTimer::timeout, reply, &QNetworkReply::abort);
        QObject::connect(&timeoutTimer, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
        timeoutTimer.start(30000);
        eventLoop.exec();

        return finishReply(reply, timeoutTimer);
    }

    QNetworkRequest makeRequest(const std::string& address, unsigned short port, const char* uri, const char* contentType)
    {
        const QUrl url = makeUrl(address, port, uri);
        if (!url.isValid() || url.host().isEmpty())
            return QNetworkRequest();

        QNetworkRequest request(url);
        if (contentType != nullptr && contentType[0] != '\0')
            request.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromUtf8(contentType));

        return request;
    }
}

HTTPNetwork::HTTPNetwork(std::string addr, unsigned short port) : address(std::move(addr)), port(port)
{
}

HTTPNetwork::~HTTPNetwork() = default;

std::string HTTPNetwork::getData(const char *uri)
{
    QNetworkAccessManager manager;
    const QNetworkRequest request = makeRequest(address, port, uri, nullptr);
    if (!request.url().isValid())
        return "UNKNOWN_ADDRESS";

    return runRequest(manager.get(request));
}

std::string HTTPNetwork::getDataPOST(const char *uri, const char* body, const char* contentType)
{
    QNetworkAccessManager manager;
    const QNetworkRequest request = makeRequest(address, port, uri, contentType);
    if (!request.url().isValid())
        return "UNKNOWN_ADDRESS";

    return runRequest(manager.post(request, QByteArray(body != nullptr ? body : "")));
}

std::string HTTPNetwork::getDataPUT(const char *uri, const char* body, const char* contentType)
{
    QNetworkAccessManager manager;
    const QNetworkRequest request = makeRequest(address, port, uri, contentType);
    if (!request.url().isValid())
        return "UNKNOWN_ADDRESS";

    return runRequest(manager.put(request, QByteArray(body != nullptr ? body : "")));
}
