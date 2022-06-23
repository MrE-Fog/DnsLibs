#include <algorithm>

#include "common/logger.h"
#include "common/version.h"
#include "net/default_verifier.h"
#include "proxy/dnsproxy.h"

#include "dns64.h"
#include "dns_forwarder.h"
#include "dnsproxy_listener.h"

using namespace std::chrono;

namespace ag {

const ErrString DnsProxy::LISTENER_ERROR = "Listener failure";

static const DnsProxySettings DEFAULT_PROXY_SETTINGS = {
    .upstreams = {
        { .address = "8.8.8.8:53", .id = 1 },
        { .address = "8.8.4.4:53", .id = 2 },
    },
    .fallbacks = {},
    .fallback_domains = {
                         // Common domains
                         "*.local",
                         "*.lan",
                         // Wi-Fi calling ePDG's
                         "epdg.epc.aptg.com.tw",
                         "epdg.epc.att.net",
                         "epdg.mobileone.net.sg",
                         "primgw.vowifina.spcsdns.net",
                         "swu-loopback-epdg.qualcomm.com",
                         "vowifi.jio.com",
                         "weconnect.globe.com.ph",
                         "wlan.three.com.hk",
                         "wo.vzwwo.com",
                         "epdg.epc.*.pub.3gppnetwork.org",
                         "ss.epdg.epc.*.pub.3gppnetwork.org",
                         "dengon.docomo.ne.jp",
                         // Router hosts
                         "dlinkap",
                         "dlinkrouter",
                         "edimax.setup",
                         "fritz.box",
                         "gateway.2wire.net",
                         "miwifi.com",
                         "my.firewall",
                         "my.keenetic.net",
                         "netis.cc",
                         "pocket.wifi",
                         "router.asus.com",
                         "repeater.asus.com",
                         "routerlogin.com",
                         "routerlogin.net",
                         "tendawifi.com",
                         "tendawifi.net",
                         "tplinklogin.net",
                         "tplinkwifi.net",
                         "tplinkrepeater.net",
                         },
    .dns64 = std::nullopt,
    .blocked_response_ttl_secs = 3600,
    .filter_params = {},
    .listeners = {},
    .outbound_proxy = std::nullopt,
    .block_ipv6 = false,
    .ipv6_available = true,
    .adblock_rules_blocking_mode = DnsProxyBlockingMode::REFUSED,
    .hosts_rules_blocking_mode = DnsProxyBlockingMode::ADDRESS,
    .dns_cache_size = 1000,
    .optimistic_cache = true,
    .enable_dnssec_ok = false,
    .enable_retransmission_handling = false,
    .block_ech = false,
};

const DnsProxySettings &DnsProxySettings::get_default() {
    return DEFAULT_PROXY_SETTINGS;
}

struct DnsProxy::Impl {
    Logger log{"DNS proxy"};
    DnsForwarder forwarder;
    DnsProxySettings settings;
    DnsProxyEvents events;
    std::vector<ListenerPtr> listeners;
};

DnsProxy::DnsProxy()
        : m_pimpl(new DnsProxy::Impl) {
}

DnsProxy::~DnsProxy() = default;

std::pair<bool, ErrString> DnsProxy::init(DnsProxySettings settings, DnsProxyEvents events) {
    std::unique_ptr<Impl> &proxy = m_pimpl;

    infolog(proxy->log, "Initializing proxy module...");

    proxy->settings = std::move(settings);
    proxy->events = std::move(events);

    for (UpstreamOptions &opts : proxy->settings.fallbacks) {
        opts.ignore_proxy_settings = true;
    }

    auto [result, err_or_warn] = proxy->forwarder.init(proxy->settings, proxy->events);
    if (!result) {
        this->deinit();
        return {false, err_or_warn};
    }

    if (!proxy->settings.listeners.empty()) {
        infolog(proxy->log, "Initializing listeners...");
        proxy->listeners.reserve(proxy->settings.listeners.size());
        for (auto &listener_settings : proxy->settings.listeners) {
            auto [listener, error] = DnsProxyListener::create_and_listen(listener_settings, this);
            if (error.has_value()) {
                errlog(proxy->log, "Failed to initialize a listener ({}): {}", listener_settings.str(), error.value());
                this->deinit();
                return {false, LISTENER_ERROR};
            } else {
                // In case the port was 0 in settings, save the actual port the listener's bound to.
                listener_settings.port = listener->get_listen_address().second.port();
                proxy->listeners.push_back(std::move(listener));
            }
        }
    }

    infolog(proxy->log, "Proxy module initialized");
    return {true, std::move(err_or_warn)};
}

void DnsProxy::deinit() {
    std::unique_ptr<Impl> &proxy = m_pimpl;
    infolog(proxy->log, "Deinitializing proxy module...");

    infolog(proxy->log, "Shutting down listeners...");
    for (auto &listener : proxy->listeners) {
        listener->shutdown();
    }
    // Must wait for all listeners to shut down before destroying the forwarder
    // (there may still be requests in progress after a shutdown() call)
    for (auto &listener : proxy->listeners) {
        listener->await_shutdown();
    }
    infolog(proxy->log, "Shutting down listeners done");

    proxy->forwarder.deinit();
    proxy->settings = {};
    infolog(proxy->log, "Proxy module deinitialized");
}

const DnsProxySettings &DnsProxy::get_settings() const {
    return m_pimpl->settings;
}

Uint8Vector DnsProxy::handle_message(Uint8View message, const DnsMessageInfo *info) {
    std::unique_ptr<Impl> &proxy = m_pimpl;

    Uint8Vector response = proxy->forwarder.handle_message(message, info);

    return response;
}

const char *DnsProxy::version() {
    return AG_DNSLIBS_VERSION;
}

} // namespace ag