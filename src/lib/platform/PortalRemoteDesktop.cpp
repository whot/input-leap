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

#include "base/Log.h"
#include "platform/PortalRemoteDesktop.h"

#include <sys/un.h> // for EIS fd hack, remove
#include <sys/socket.h> // for EIS fd hack, remove


PortalRemoteDesktop::PortalRemoteDesktop(EiScreen *screen,
                                         IEventQueue* events) :
    m_screen(screen),
    m_portal(xdp_portal_new()),
    m_events(events)
{
    m_gmainloop = g_main_loop_new(NULL, true);
    m_glibthread = new Thread([this](){ glibThread(); });
    g_idle_add([](gpointer data) -> gboolean {
               return reinterpret_cast<PortalRemoteDesktop*>(data)->initRemoteDesktopSession();
               },
               this);
}

PortalRemoteDesktop::~PortalRemoteDesktop()
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

    g_signal_handler_disconnect(m_session, m_sessionSignalID);
    g_object_unref(m_session);
    g_object_unref(m_portal);
}

gboolean
PortalRemoteDesktop::timeoutHandler()
{
    return true; // keep re-triggering
}

int PortalRemoteDesktop::fakeEISfd()
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
PortalRemoteDesktop::cb_SessionClosed(XdpSession *session)
{
    LOG((CLOG_ERR "Our RemoteDesktop session was closed, exiting."));
    g_main_loop_quit(m_gmainloop);
    m_events->addEvent(Event(Event::kQuit));

    g_signal_handler_disconnect(session, m_sessionSignalID);
}

void
PortalRemoteDesktop::cb_SessionStarted(GObject *object, GAsyncResult *res)
{
    auto session = XDP_SESSION(object);

    // ConnectToEIS requires version 2 of the xdg-desktop-portal (and the same
    // version in the impl.portal), i.e. you'll need an updated compositor on
    // top of everything...
    auto fd = -1;
#if HAVE_LIBPORTAL_SESSION_CONNECT_TO_EIS
    fd = xdp_session_connect_to_eis(session);
#endif
    if (fd < 0) {
        if (fd == -ENOTSUP)
            LOG((CLOG_ERR "The XDG desktop portal does not support EI.", strerror(-fd)));
        else
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

    g_autoptr(GError) error = NULL;
    auto success = xdp_session_start_finish(session, res, &error);
    if (!success) {
        close(fd);
        LOG((CLOG_ERR "Failed to start session"));
        g_main_loop_quit(m_gmainloop);
        m_events->addEvent(Event(Event::kQuit));
    }

    // Socket ownership is transferred to the EiScreen
    m_events->addEvent(Event(m_events->forEiScreen().connectedToEIS(),
                             m_screen->getEventTarget(),
                             new int(fd)));
}

void
PortalRemoteDesktop::cb_initRemoteDesktopSession(GObject *object, GAsyncResult *res)
{
    LOG((CLOG_DEBUG "Session ready"));
    g_autoptr(GError) error = NULL;

    auto session = xdp_portal_create_remote_desktop_session_finish(XDP_PORTAL(object), res, &error);
    if (!session) {
        LOG((CLOG_ERR "Failed to initialize RemoteDesktop session, quitting: %s", error->message));
        g_main_loop_quit(m_gmainloop);
        m_events->addEvent(Event(Event::kQuit));
        return;
    }

    m_session = session;

    // FIXME: the lambda trick doesn't work here for unknown reasons, we need
    // the static function
    m_sessionSignalID = g_signal_connect(G_OBJECT(session), "closed",
                                         G_CALLBACK(cb_SessionClosedCB),
                                         this);

    xdp_session_start(session,
                      nullptr, // parent
                      nullptr, // cancellable
                      [](GObject *obj, GAsyncResult *res, gpointer data) {
                          reinterpret_cast<PortalRemoteDesktop*>(data)->cb_SessionStarted(obj, res);
                      },
                      this);
}

gboolean
PortalRemoteDesktop::initRemoteDesktopSession(void)
{
    LOG((CLOG_DEBUG "Setting up the RemoteDesktop session"));
    xdp_portal_create_remote_desktop_session(m_portal,
                                             static_cast<XdpDeviceType>(XDP_DEVICE_POINTER | XDP_DEVICE_KEYBOARD),
                                             XDP_OUTPUT_NONE,
                                             XDP_REMOTE_DESKTOP_FLAG_NONE,
                                             XDP_CURSOR_MODE_HIDDEN,
                                             nullptr, // cancellable
                                             [](GObject *obj, GAsyncResult *res, gpointer data) {
                                                 reinterpret_cast<PortalRemoteDesktop*>(data)->cb_initRemoteDesktopSession(obj, res);
                                             },
                                             this);

    return false;
}

void
PortalRemoteDesktop::glibThread()
{
    auto context = g_main_loop_get_context(m_gmainloop);

    LOG((CLOG_DEBUG "GLib thread running"));

    while (g_main_loop_is_running(m_gmainloop)) {
        Thread::testCancel();

        if (g_main_context_iteration(context, true)) {
            LOG((CLOG_DEBUG "Glib events!!"));
        }
    }

    LOG((CLOG_DEBUG "Shutting down GLib thread"));
}
