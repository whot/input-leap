/*
 * InputLeap -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2002 Chris Schoeneman
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "platform/PortalInputCapture.h"

#if HAVE_LIBPORTAL_INPUTCAPTURE

#include "base/Log.h"
#include "platform/PortalInputCapture.h"

#include <sys/un.h> // for EIS fd hack, remove
#include <sys/socket.h> // for EIS fd hack, remove

enum signals {
    SESSION_CLOSED,
    DISABLED,
    ACTIVATED,
    DEACTIVATED,
    ZONES_CHANGED,
    _N_SIGNALS,
};

PortalInputCapture::PortalInputCapture(EiScreen *screen, IEventQueue* events) :
    m_screen(screen),
    m_portal(xdp_portal_new()),
    m_events(events),
    m_session(nullptr),
    m_signals(_N_SIGNALS)
{
    m_gmainloop = g_main_loop_new(NULL, true);
    m_glibthread = new Thread([this](){ glibThread(); });
    g_idle_add([](gpointer data) -> gboolean {
               return reinterpret_cast<PortalInputCapture*>(data)->initInputCaptureSession();
               },
               this);
}

PortalInputCapture::~PortalInputCapture()
{
    if (g_main_loop_is_running(m_gmainloop))
        g_main_loop_quit(m_gmainloop);

    if (m_glibthread != nullptr) {
        m_glibthread->cancel();
        m_glibthread->wait();
        delete m_glibthread;
        m_glibthread = NULL;

        g_main_loop_unref(m_gmainloop);
        m_gmainloop = NULL;
    }

    if (m_session) {
        for (auto sigid: m_signals) {
            if (sigid != 0)
                g_signal_handler_disconnect(m_session, sigid);
        }
        g_object_unref(m_session);
    }

    for (auto b : m_barriers)
        g_object_unref(b);
    m_barriers.clear();
    g_object_unref(m_portal);
}

gboolean
PortalInputCapture::timeoutHandler()
{
    return true; // keep re-triggering
}

int PortalInputCapture::fakeEISfd()
{
    auto path = std::getenv("LIBEI_SOCKET");

    if (!path) {
        LOG((CLOG_DEBUG "Cannot fake EIS socket, LIBEI_SOCKET environment variable is unset"));
        return -1;
    }

    auto sock = socket(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK, 0);

    // Dealing with the socket directly because nothing in lib/... supports
    // AF_UNIX and I'm too lazy to fix all this for a temporary hack
    int fd = sock;
    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
        .sun_path = {0},
    };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    auto result = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (result != 0)
        LOG((CLOG_DEBUG "Faked EIS fd failed: %s", strerror(errno)));

    return sock;
}

void
PortalInputCapture::cb_SessionClosed(XdpSession *session)
{
    LOG((CLOG_ERR "Our InputCapture session was closed, exiting."));
    g_main_loop_quit(m_gmainloop);
    m_events->addEvent(Event(Event::kQuit));

    g_signal_handler_disconnect(session, m_signals[SESSION_CLOSED]);
    m_signals[SESSION_CLOSED] = 0;
}

void
PortalInputCapture::cb_initInputCaptureSession(GObject *object, GAsyncResult *res)
{
    LOG((CLOG_DEBUG "Session ready"));
    g_autoptr(GError) error = NULL;

    auto session = xdp_portal_create_input_capture_session_finish(XDP_PORTAL(object), res, &error);
    if (!session) {
        LOG((CLOG_ERR "Failed to initialize InputCapture session, quitting: %s", error->message));
        g_main_loop_quit(m_gmainloop);
        m_events->addEvent(Event(Event::kQuit));
        return;
    }

    m_session = session;

    auto fd = xdp_input_capture_session_connect_to_eis(session);
    if (fd < 0) {
            LOG((CLOG_ERR "Failed to connect to EIS: %s", strerror(-fd)));

            // FIXME: Development hack to avoid having to assemble all parts just for
            // testing this code.
            fd = fakeEISfd();

            if (fd < 0) {
                g_main_loop_quit(m_gmainloop);
                m_events->addEvent(Event(Event::kQuit));
                return;
            }
    }
    // Socket ownership is transferred to the EiScreen
    m_events->addEvent(Event(m_events->forEiScreen().connectedToEIS(),
                             m_screen->getEventTarget(),
                             new int(fd)));

    // FIXME: the lambda trick doesn't work here for unknown reasons, we need
    // the static function
    m_signals[SESSION_CLOSED] = g_signal_connect(G_OBJECT(session), "closed",
                                                G_CALLBACK(cb_SessionClosedCB),
                                                this);
    m_signals[DISABLED] = g_signal_connect(G_OBJECT(session), "disabled",
                                           G_CALLBACK(cb_DisabledCB),
                                           this);
    m_signals[ACTIVATED] =g_signal_connect(G_OBJECT(m_session), "activated",
                                           G_CALLBACK(cb_ActivatedCB),
                                           this);
    m_signals[DEACTIVATED] = g_signal_connect(G_OBJECT(m_session), "deactivated",
                                              G_CALLBACK(cb_DeactivatedCB),
                                              this);
    m_signals[ZONES_CHANGED] = g_signal_connect(G_OBJECT(m_session), "zones-changed",
                                                G_CALLBACK(cb_ZonesChangedCB),
                                                this);

    cb_ZonesChanged(m_session, nullptr);
}

void
PortalInputCapture::cb_SetPointerBarriers(GObject *object, GAsyncResult *res)
{
    g_autoptr(GError) error = NULL;

    auto failed_list = xdp_input_capture_session_set_pointer_barriers_finish(m_session, res, &error);
    if (failed_list) {
        auto it = failed_list;
        while (it) {
            guint id;
            g_object_get(it->data, "id", &id, NULL);
            LOG((CLOG_WARN "Failed to apply barrier %d", id));

            for (auto elem = m_barriers.begin(); elem != m_barriers.end(); elem++) {
                if (*elem == it->data) {
                    g_object_unref(*elem);
                    m_barriers.erase(elem);
                    break;
                }
            }
            it = it->next;
        }
    }
    g_list_free_full(failed_list, g_object_unref);

    enable();

}

gboolean
PortalInputCapture::initInputCaptureSession(void)
{
    LOG((CLOG_DEBUG "Setting up the InputCapture session"));
    xdp_portal_create_input_capture_session(m_portal,
                                            nullptr, // parent
                                            static_cast<XdpCapability>(XDP_CAPABILITY_KEYBOARD | XDP_CAPABILITY_POINTER_ABSOLUTE | XDP_CAPABILITY_POINTER_RELATIVE),
                                            nullptr, // cancellable
                                            [](GObject *obj, GAsyncResult *res, gpointer data) {
                                            reinterpret_cast<PortalInputCapture*>(data)->cb_initInputCaptureSession(obj, res);
                                            },
                                            this);

    return false;
}

void
PortalInputCapture::enable()
{
    if (!m_enabled) {
        LOG((CLOG_DEBUG "Enabling the InputCapture session"));
        xdp_input_capture_session_enable(m_session);
        m_enabled = true;
    }
}

void
PortalInputCapture::cb_Disabled(XdpInputCaptureSession *session)
{
    LOG((CLOG_DEBUG "We are disabled!"));

    m_enabled = false;

    // FIXME: need some better heuristics here of when we want to enable again
    // But we don't know *why* we got disabled (and it's doubtfull we ever will), so
    // we just assume that the zones will change or something and we can re-enable again
    // ... very soon
    g_timeout_add(1000,
                  [](gpointer data) -> gboolean {
                      reinterpret_cast<PortalInputCapture*>(data)->enable();
                  return false;
                  },
                  this);
}

void
PortalInputCapture::cb_Activated(XdpInputCaptureSession *session, GVariant *options)
{
    LOG((CLOG_DEBUG "We are active!"));
}

void
PortalInputCapture::cb_Deactivated(XdpInputCaptureSession *session, GVariant *options)
{
    LOG((CLOG_DEBUG "We are deactivated!"));
}

void
PortalInputCapture::cb_ZonesChanged(XdpInputCaptureSession *session, GVariant *options)
{
    for (auto b : m_barriers)
        g_object_unref(b);
    m_barriers.clear();

    auto zones = xdp_input_capture_session_get_zones(session);
    while (zones != nullptr) {
        guint w, h;
        gint x, y;
        g_object_get(zones->data, "width", &w, "height", &h, "x", &x, "y", &y, NULL);

        LOG((CLOG_DEBUG "Zone at %dx%d@%d,%d", w, h, x, y));

        // Hardcoded behaviour: our pointer barriers are always at the edges of all zones.
        // Since the implementation is supposed to reject the ones in the wrong
        // place, we can just install barriers everywhere and let EIS figure it out.
        // Also a lot easier to implement for now though it doesn't cover
        // differently-sized screens...
        m_barriers.push_back(XDP_INPUT_CAPTURE_POINTER_BARRIER(
                                g_object_new(XDP_TYPE_INPUT_CAPTURE_POINTER_BARRIER,
                                             "id", m_barriers.size(),
                                             "x1", x,
                                             "y1", y,
                                             "x2", x + w,
                                             "y2", y,
                                             NULL)));
        m_barriers.push_back(XDP_INPUT_CAPTURE_POINTER_BARRIER(
                             g_object_new(XDP_TYPE_INPUT_CAPTURE_POINTER_BARRIER,
                                          "id", m_barriers.size(),
                                          "x1", x + w,
                                          "y1", y,
                                          "x2", x + w,
                                          "y2", y + h,
                                          NULL)));
        m_barriers.push_back(XDP_INPUT_CAPTURE_POINTER_BARRIER(
                             g_object_new(XDP_TYPE_INPUT_CAPTURE_POINTER_BARRIER,
                                          "id", m_barriers.size(),
                                          "x1", x,
                                          "y1", y,
                                          "x2", x,
                                          "y2", y + h,
                                          NULL)));
        m_barriers.push_back(XDP_INPUT_CAPTURE_POINTER_BARRIER(
                             g_object_new(XDP_TYPE_INPUT_CAPTURE_POINTER_BARRIER,
                                          "id", m_barriers.size(),
                                          "x1", x,
                                          "y1", y + h,
                                          "x2", x + w,
                                          "y2", y + h,
                                          NULL)));
        zones = zones->next;
    }

    GList *list = NULL;
    for (auto const &b : m_barriers)
        list = g_list_append(list, b);
    xdp_input_capture_session_set_pointer_barriers(m_session,
                                                   list,
                                                    nullptr, // cancellable
                                                    [](GObject *obj, GAsyncResult *res, gpointer data) {
                                                    reinterpret_cast<PortalInputCapture*>(data)->cb_SetPointerBarriers(obj, res);
                                                    },
                                                    this);
}

void
PortalInputCapture::glibThread()
{
    auto context = g_main_loop_get_context(m_gmainloop);

    LOG((CLOG_DEBUG "GLib thread running"));

    while (g_main_loop_is_running(m_gmainloop)) {
        Thread::testCancel();
        g_main_context_iteration(context, true);
    }

    LOG((CLOG_DEBUG "Shutting down GLib thread"));
}

#endif
