// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <functional>
#include <memory>

namespace Kobold::Testing
{

// Minimal HTTP/1.1 server for client tests: canned responses keyed by "METHOD /path",
// one request per connection, every received request recorded.
class FakeHttpServer
{
public:
    struct Request {
        QByteArray method;
        // Path including the query string.
        QByteArray target;
        // Header names in lower case.
        QHash<QByteArray, QByteArray> headers;
        QByteArray body;
    };

    struct Response {
        int status = 200;
        QByteArray body;
        QByteArray contentType = "application/json";
    };

    FakeHttpServer()
    {
        m_server.listen(QHostAddress::LocalHost);
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                handle(socket);
            }
        });
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()));
    }

    // Path without query string, e.g. "GET /api/v1/books/A".
    void setResponse(const QByteArray &methodAndPath, const Response &response)
    {
        m_responses.insert(methodAndPath, response);
    }

    // Computes the response from the request; takes precedence over setResponse().
    void setHandler(const QByteArray &methodAndPath, std::function<Response(const Request &)> handler)
    {
        m_handlers.insert(methodAndPath, std::move(handler));
    }

    const QList<Request> &requests() const
    {
        return m_requests;
    }

private:
    void handle(QTcpSocket *socket)
    {
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }

            Request request;
            const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
            const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
            request.method = requestLine.value(0);
            request.target = requestLine.value(1);
            for (qsizetype i = 1; i < lines.size(); ++i) {
                const qsizetype colon = lines.at(i).indexOf(':');
                if (colon > 0) {
                    request.headers.insert(lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
                }
            }
            const qsizetype contentLength = request.headers.value("content-length").toLongLong();
            if (buffer->size() - (headerEnd + 4) < contentLength) {
                return;
            }
            request.body = buffer->mid(headerEnd + 4, contentLength);
            m_requests.append(request);

            const QByteArray path = request.target.left(request.target.indexOf('?') >= 0 ? request.target.indexOf('?') : request.target.size());
            const QByteArray key = request.method + ' ' + path;
            const Response response =
                m_handlers.contains(key) ? m_handlers.value(key)(request) : m_responses.value(key, Response{404, R"({"error":"Not Found"})"});

            QByteArray reply = "HTTP/1.1 " + QByteArray::number(response.status) + " Status\r\n";
            if (response.status != 204) {
                reply += "Content-Type: " + response.contentType + "\r\n";
                reply += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
            }
            reply += "Connection: close\r\n\r\n";
            if (response.status != 204) {
                reply += response.body;
            }
            socket->write(reply);
            socket->disconnectFromHost();
        });
    }

    QTcpServer m_server;
    QHash<QByteArray, Response> m_responses;
    QHash<QByteArray, std::function<Response(const Request &)>> m_handlers;
    QList<Request> m_requests;
};

} // namespace Kobold::Testing
