#include <phpcpp.h>
#include "azure_c_shared_utility/platform.h"
#include "azure_c_shared_utility/tlsio.h"
#include "azure_c_shared_utility/socketio.h"
#include "azure_uamqp_c/uamqp.h"
#include "Connection.h"
#include "Session.h"
#include "Producer.h"
#include "Consumer.h"
#include "Message.h"

void Connection::__construct(Php::Parameters &params)
{
    host    = params[0].stringValue();
    port    = params[1].numericValue();
    useTls  = params[2].boolValue();
    keyName = params[3].stringValue();
    key     = params[4].stringValue();
    debug   = params.size() == 6 ? params[5].boolValue() : false;
    clientname    = params.size() == 7 ? params[6].stringValue(): "some";
}

void Connection::connect()
{
    if (isConnected) {
        return;
    }

    bool useAuth = !keyName.empty() && !key.empty();

    if (platform_init() != 0) {
        //throw Php::Exception("Could not run platform_init");
    }

    if (useTls) {
        tls_io_config = { host.c_str(), port };
        /* create the TLS IO */
        tlsio_interface = platform_get_default_tlsio();
        tls_io = xio_create(tlsio_interface, &tls_io_config);
    } else {
        socketio_config = { host.c_str(), port, NULL };
        socket_io = xio_create(socketio_get_interface_description(), &socketio_config);
    }

    if (useAuth) {
        sasl_plain_config = { keyName.c_str(), key.c_str(), NULL };
        /* create SASL PLAIN handler */
        sasl_mechanism_handle = saslmechanism_create(saslplain_get_interface(), &sasl_plain_config);
        /* create the SASL client IO using the TLS IO or SOCKET OI */
        if (useTls) {
            sasl_io_config.underlying_io = tls_io;
        } else {
            sasl_io_config.underlying_io = socket_io;
        }
        sasl_io_config.sasl_mechanism = sasl_mechanism_handle;
        sasl_io = xio_create(saslclientio_get_interface_description(), &sasl_io_config);
    }

    /* create the connection */
    connection = connection_create(useAuth ? sasl_io : socket_io, host.c_str(), clientname.c_str(), NULL, NULL);
    if (isDebugOn()) {
        connection_set_trace(connection, true);
    }

    // Session
    session = new Session(this);

    isConnected = true;
}

void Connection::publish(Php::Parameters &params)
{
    connect();

    std::string resourceName = params[0].stringValue();
    Message *message = (Message*) params[1].implementation();

    Producer *producer = new Producer(session, resourceName);
    producer->publish(message);
}

void Connection::setCallback(Php::Parameters &params)
{
    connect();

    std::string resourceName = params[0].stringValue();
    Php::Value callback = params[1];
    Php::Value loopFn = params[2];

    consumer = new Consumer(session, resourceName);
    consumer->setCallback(callback, loopFn);
}

void Connection::consume()
{
    if (consumer != NULL) {
        consumer->consume();
    }
}

std::string Connection::getHost()
{
    return host;
}

CONNECTION_HANDLE Connection::getConnectionHandler()
{
    return connection;
}

void Connection::doWork()
{
    connection_dowork(connection);
}

bool Connection::isDebugOn()
{
    return debug;
}

void Connection::close()
{
    if (consumer != NULL && !consumer->wasCloseRequested()) {
        consumer->close();
    }

    if (!closeRequested) {
        closeRequested = true;
        connection_destroy(connection);
        xio_destroy(sasl_io);
        xio_destroy(tls_io);
        saslmechanism_destroy(sasl_mechanism_handle);
        platform_deinit();
    }
}
