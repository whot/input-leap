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

PortalInputCapture::PortalInputCapture(EiScreen *screen, IEventQueue* events) :
    m_screen(screen),
    m_portal(xdp_portal_new()),
    m_events(events),
    m_session(nullptr)
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
        g_signal_handler_disconnect(m_session, m_sessionSignalID);
        if (m_activatedSignalID != 0)
            g_signal_handler_disconnect(m_session, m_activatedSignalID);
        if (m_deactivatedSignalID != 0)
            g_signal_handler_disconnect(m_session, m_deactivatedSignalID);
        if (m_zonesChangedSignalID != 0)
            g_signal_handler_disconnect(m_session, m_zonesChangedSignalID);
        g_object_unref(m_session);
    }

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
        LOG((CLOG_DEBUG "Cannot fake EIS socket, LIBEIS_SOCKET environment variable is unset"));
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

    g_signal_handler_disconnect(session, m_sessionSignalID);
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
    m_sessionSignalID = g_signal_connect(G_OBJECT(session), "closed",
                                         G_CALLBACK(cb_SessionClosedCB),
                                         this);
    m_activatedSignalID = g_signal_connect(G_OBJECT(m_session), "activated",
                                         G_CALLBACK(cb_ActivatedCB),
                                         this);
    m_deactivatedSignalID = g_signal_connect(G_OBJECT(m_session), "deactivated",
                                         G_CALLBACK(cb_DeactivatedCB),
                                         this);
    m_zonesChangedSignalID = g_signal_connect(G_OBJECT(m_session), "zones-changed",
                                              G_CALLBACK(cb_ZonesChangedCB),
                                              this);

    auto zones = xdp_input_capture_session_get_zones(session);

    // count the zones so we can pre-allocate our barriers vector.
    // we don't really care about failed barriers, storage is negligable so we
    // just keep them around until we're done with the zones
    auto zones_it = zones;
    auto nzones = 0;
    while (zones_it != nullptr) {
        zones_it = zones_it->next;
        nzones++;
    }

    m_barriers.clear();
    m_barriers.reserve(nzones * 4);  // our max is 4 barriers per zone

    while (zones != nullptr) {
        guint w, h;
        gint x, y;
        g_object_get(zones->data, "width", &w, "height", &h, "x", &x, "y", &y, NULL);

        // Hardcoded behaviour: our pointer barriers are always at the edges of all zones.
        // Since the implementation is supposed to reject the ones in the wrong
        // place, we can just install barriers everywhere and let EIS figure it out.
        // Also a lot easier to implement for now though it doesn't cover
        // differently-sized screens...
        m_barriers.emplace_back(*this, x, y, x + w, y);
        m_barriers.emplace_back(*this, x + w, y, x + w, y + h);
        m_barriers.emplace_back(*this, x, y, x, y + h);
        m_barriers.emplace_back(*this, x, y + h, x + w, y + h);

        zones = zones->next;
    }

    GList *list = NULL;
    for (auto const &b : m_barriers)
        list = g_list_append(list, b.getBarrier());
    xdp_input_capture_session_set_pointer_barriers(m_session,
                                                   list,
                                                    nullptr, // cancellable
                                                    [](GObject *obj, GAsyncResult *res, gpointer data) {
                                                    reinterpret_cast<PortalInputCapture*>(data)->cb_SetPointerBarriers(obj, res);
                                                    },
                                                    this);
}

void
PortalInputCapture::cb_SetPointerBarriers(GObject *object, GAsyncResult *res)
{
    g_autoptr(GError) error = NULL;

    auto failed_list = xdp_input_capture_session_set_pointer_barriers_finish(m_session, res, &error);
    if (failed_list) {
        LOG((CLOG_WARN "Failed to apply some barriers"));
        // We don't actually care about this...
    }
    g_list_free_full(failed_list, g_object_unref);
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
    LOG((CLOG_DEBUG "zones have changed!"));
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

PortalInputCapturePointerBarrier::PortalInputCapturePointerBarrier(PortalInputCapture& portal,
                                                                   int x1, int y1, int x2, int y2) :
    m_portal(portal),
    m_id(0),
    m_x1(x1),
    m_y1(y1),
    m_x2(x2),
    m_y2(y2)
{
    static unsigned int next_id = 0;

    m_id = ++next_id;
    m_barrier =
        XDP_INPUT_CAPTURE_POINTER_BARRIER(g_object_new(XDP_TYPE_INPUT_CAPTURE_POINTER_BARRIER,
                                                       "id", m_id,
                                                       "x1", x1,
                                                       "x2", x2,
                                                       "y1", y1,
                                                       "y2", y2,
                                                       NULL));
    m_activeSignalID = g_signal_connect(G_OBJECT(m_barrier), "notify::is-active",
                                         G_CALLBACK(cb_BarrierNotifyActiveCB),
                                         this);
}

PortalInputCapturePointerBarrier::~PortalInputCapturePointerBarrier()
{
    LOG((CLOG_DEBUG "Deleting barier %d", m_id));
    if (m_activeSignalID != 0)
        g_signal_handler_disconnect(m_barrier, m_activeSignalID);
    if (m_barrier != nullptr)
        g_object_unref(m_barrier);
}

void
PortalInputCapturePointerBarrier::cb_BarrierNotifyActive(XdpInputCapturePointerBarrier *barrier)
{
    assert(this->m_barrier == barrier);

    g_object_get(barrier, "is-active",  &m_isActive, NULL);
    LOG((CLOG_DEBUG "Barrier %d (%d/%d %d/%d) %s", m_id, m_x1, m_y1, m_x2, m_y2, m_isActive ? "active" : "failed"));

    // We disconnect the signal handler. Once we know the barriers is
    // valid (or not) we don't care if it gets disabled later.
    g_signal_handler_disconnect(m_barrier, m_activeSignalID);
    m_activeSignalID = 0;

    // libportal guarantees that failed pointer barriers are signalled first
    if (m_isActive)
        m_portal.enable();
}
#endif
