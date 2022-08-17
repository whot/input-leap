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

#pragma once

#include "config.h"

#include "mt/Thread.h"
#include "platform/EiScreen.h"

#include <glib.h>
#include <libportal/portal.h>

class PortalRemoteDesktop {
public:
    PortalRemoteDesktop(EiScreen *screen, IEventQueue *events);
    ~PortalRemoteDesktop();

private:
    void glibThread();
    gboolean timeoutHandler();
    gboolean initRemoteDesktopSession();
    void cb_initRemoteDesktopSession(GObject *object, GAsyncResult *res);
    void cb_SessionStarted(GObject *object, GAsyncResult *res);
    void cb_SessionClosed(XdpSession *session);

    /// g_signal_connect callback wrapper
    static void cb_SessionClosedCB(XdpSession *session, gpointer data) {
        reinterpret_cast<PortalRemoteDesktop*>(data)->cb_SessionClosed(session);
    } ;

    int fakeEISfd();

private:
    EiScreen *m_screen;
    IEventQueue* m_events;

    Thread* m_glibthread;
    GMainLoop *m_gmainloop;

    XdpPortal *m_portal;
    XdpSession *m_session;

    guint m_sessionSignalID;
};
