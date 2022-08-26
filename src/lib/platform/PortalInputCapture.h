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

class PortalInputCapturePointerBarrier {
public:
    PortalInputCapturePointerBarrier(PortalInputCapture &portal, int x1, int y1, int x2, int y2);
    ~PortalInputCapturePointerBarrier();

    bool isActive() { return m_isActive; }
    unsigned int getId() { return m_id; }
    XdpInputCapturePointerBarrier *getBarrier() { return m_barrier; }

    void cb_BarrierActive(XdpInputCapturePointerBarrier *barrier, gboolean active);

    /// g_signal_connect callback wrapper
    static void cb_BarrierActiveCB(XdpInputCapturePointerBarrier *barrier, gboolean active, gpointer data) {
        reinterpret_cast<PortalInputCapturePointerBarrier*>(data)->cb_BarrierActive(barrier, active);
    } ;

private:
    PortalInputCapture& m_portal;
    int m_x1, m_x2, m_y1, m_y2;
    unsigned int m_id;
    bool m_isActive;
    XdpInputCapturePointerBarrier *m_barrier;
    guint m_activeSignalID;
};

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

    guint m_sessionSignalID;
    guint m_activatedSignalID;
    guint m_deactivatedSignalID;
    guint m_zonesChangedSignalID;

    bool m_enabled;

    std::vector<std::unique_ptr<PortalInputCapturePointerBarrier*>> m_barriers;
};

#endif
