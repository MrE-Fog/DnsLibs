#include <tuple>

#include "outbound_direct_proxy.h"

namespace ag::dns {

DirectOProxy::DirectOProxy(Parameters parameters)
        : OutboundProxy(__func__, nullptr, std::move(parameters)) {
}

void DirectOProxy::reset_connections() {
    for (auto &[conn_id, conn] : std::exchange(m_connections, {})) {
        if (conn.parameters.callbacks.on_close != nullptr) {
            auto err = make_error(SocketError::AE_OUTBOUND_PROXY_ERROR, "Reset re-routed directly connection");
            conn.parameters.callbacks.on_close(conn.parameters.callbacks.arg, err);
        }
    }
}

void DirectOProxy::deinit_impl() {
}

OutboundProxy::ProtocolsSet DirectOProxy::get_supported_protocols() const {
    return (1u << utils::TP_TCP) | (1u << utils::TP_UDP);
}

std::optional<evutil_socket_t> DirectOProxy::get_fd(uint32_t conn_id) const {
    auto it = m_connections.find(conn_id);
    return (it != m_connections.end()) ? it->second.socket->get_fd() : std::nullopt;
}

Error<SocketError> DirectOProxy::send(uint32_t conn_id, Uint8View data) {
    auto it = m_connections.find(conn_id);
    return (it != m_connections.end())
            ? it->second.socket->send(data)
            : make_error(SocketError::AE_CONNECTION_ID_NOT_FOUND, fmt::to_string(conn_id));
}

bool DirectOProxy::set_timeout(uint32_t conn_id, Micros timeout) {
    auto it = m_connections.find(conn_id);
    return (it != m_connections.end()) && it->second.socket->set_timeout(timeout);
}

Error<SocketError> DirectOProxy::set_callbacks_impl(uint32_t conn_id, Callbacks cbx) {
    auto it = m_connections.find(conn_id);
    if (it == m_connections.end()) {
        return make_error(SocketError::AE_CONNECTION_ID_NOT_FOUND, fmt::to_string(conn_id));
    }

    Connection &conn = it->second;
    conn.parameters.callbacks = cbx;
    if (auto e = conn.socket->set_callbacks({(cbx.on_connected != nullptr) ? on_connected : nullptr,
                (cbx.on_read != nullptr) ? on_read : nullptr, (cbx.on_close != nullptr) ? on_close : nullptr, &conn})) {
        return e;
    }

    return {};
}

void DirectOProxy::close_connection_impl(uint32_t conn_id) {
    auto node = m_connections.extract(conn_id);
    if (node.empty()) {
        return;
    }

    Connection &conn = node.mapped();
    [[maybe_unused]] auto e = conn.socket->set_callbacks({});
}

Error<SocketError> DirectOProxy::connect_to_proxy(uint32_t conn_id, const ConnectParameters &parameters) {
    return this->connect_through_proxy(conn_id, parameters);
}

Error<SocketError> DirectOProxy::connect_through_proxy(
        uint32_t conn_id, const ConnectParameters &parameters) {
    Connection &conn = m_connections
                               .emplace(conn_id,
                                       Connection{
                                               this,
                                               conn_id,
                                               m_parameters.make_socket.func(
                                                       m_parameters.make_socket.arg, parameters.proto, std::nullopt),
                                               parameters,
                                       })
                               .first->second;
    auto err = conn.socket->connect({
            parameters.loop,
            parameters.peer,
            {on_connected, on_read, on_close, &conn},
            parameters.timeout,
    });
    if (err) {
        m_connections.erase(conn_id);
    }

    return err;
}

void DirectOProxy::on_connected(void *arg) {
    auto *conn = (Connection *) arg;
    conn->parameters.callbacks.on_connected(conn->parameters.callbacks.arg, conn->id);
}

void DirectOProxy::on_read(void *arg, Uint8View data) {
    auto *conn = (Connection *) arg;
    conn->parameters.callbacks.on_read(conn->parameters.callbacks.arg, data);
}

void DirectOProxy::on_close(void *arg, Error<SocketError> error) {
    auto *conn = (Connection *) arg;
    conn->parameters.callbacks.on_close(conn->parameters.callbacks.arg, std::move(error));
}

} // namespace ag::dns
