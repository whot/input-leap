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

#if HAVE_LIBPORTAL_INPUTCAPTURE

#include "mt/Thread.h"
#include "platform/EiScreen.h"

#include <glib.h>
#include <libportal/portal.h>

class PortalInputCapture {
public:
    PortalInputCapture(EiScreen *screen, IEventQueue *events);
    ~PortalInputCapture();
    void enable();

private:
    void glibThread();
    gboolean timeoutHandler();
    gboolean initInputCaptureSession();
    void cb_initInputCaptureSession(GObject *object, GAsyncResult *res);
    void cb_SetPointerBarriers(GObject *object, GAsyncResult *res);
    void cb_SessionStarted(GObject *object, GAsyncResult *res);
    void cb_SessionClosed(XdpSession *session);
    void cb_Activated(XdpInputCaptureSession *session, GVariant *options);
    void cb_Deactivated(XdpInputCaptureSession *session, GVariant *options);
    void cb_ZonesChanged(XdpInputCaptureSession *session, GVariant *options);

    /// g_signal_connect callback wrapper
    static void cb_SessionClosedCB(XdpSession *session, gpointer data) {
        reinterpret_cast<PortalInputCapture*>(data)->cb_SessionClosed(session);
    } ;
    static void cb_ActivatedCB(XdpInputCaptureSession *session, GVariant *options, gpointer data) {
        reinterpret_cast<PortalInputCapture*>(data)->cb_Activated(session, options);
    } ;
    static void cb_DeactivatedCB(XdpInputCaptureSession *session, GVariant *options, gpointer data) {
        reinterpret_cast<PortalInputCapture*>(data)->cb_Deactivated(session, options);
    } ;
    static void cb_ZonesChangedCB(XdpInputCaptureSession *session, GVariant *options, gpointer data) {
        reinterpret_cast<PortalInputCapture*>(data)->cb_ZonesChanged(session, options);
    } ;

    int fakeEISfd();

private:
    EiScreen *m_screen;
    IEventQueue* m_events;

    Thread* m_glibthread;
    GMainLoop *m_gmainloop;

    XdpPortal *m_portal;
    XdpInputCaptureSession *m_session;

    std::vector<guint> m_signals;

    bool m_enabled;

    std::vector<XdpInputCapturePointerBarrier*> m_barriers;
};

#endif
