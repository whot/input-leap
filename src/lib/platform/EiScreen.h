/*
 * barrier -- mouse and keyboard sharing utility
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

#include <mutex>

#include "inputleap/PlatformScreen.h"
#include "inputleap/KeyMap.h"
#include "common/stdset.h"
#include "common/stdvector.h"

class EiClipboard;
class EiKeyState;
class PortalRemoteDesktop;

//! Implementation of IPlatformScreen for X11
class EiScreen : public PlatformScreen {
public:
    EiScreen(bool isPrimary, IEventQueue* events, bool use_portal);
    ~EiScreen();

    //! @name manipulators
    //@{

    //@}

    // IScreen overrides
    void* getEventTarget() const override;
    bool getClipboard(ClipboardID id, IClipboard*) const override;
    void getShape(int32_t& x, int32_t& y,
                          int32_t& width, int32_t& height) const override;
    void getCursorPos(int32_t& x, int32_t& y) const override;

    // IPrimaryScreen overrides
    void reconfigure(uint32_t activeSides) override;
    void warpCursor(int32_t x, int32_t y) override;
    uint32_t registerHotKey(KeyID key, KeyModifierMask mask) override;
    void unregisterHotKey(uint32_t id) override;
    void fakeInputBegin() override;
    void fakeInputEnd() override;
    int32_t getJumpZoneSize() const override;
    bool isAnyMouseButtonDown(uint32_t& buttonID) const override;
    void getCursorCenter(int32_t& x, int32_t& y) const override;

    // ISecondaryScreen overrides
    void fakeMouseButton(ButtonID id, bool press) override;
    void fakeMouseMove(int32_t x, int32_t y) override;
    void fakeMouseRelativeMove(int32_t dx, int32_t dy) const override;
    void fakeMouseWheel(int32_t xDelta, int32_t yDelta) const override;

    // IPlatformScreen overrides
    void enable() override;
    void disable() override;
    void enter() override;
    bool leave() override;
    bool setClipboard(ClipboardID, const IClipboard*) override;
    void checkClipboards() override;
    void openScreensaver(bool notify) override;
    void closeScreensaver() override;
    void screensaver(bool activate) override;
    void resetOptions() override;
    void setOptions(const OptionsList& options) override;
    void setSequenceNumber(uint32_t) override;
    bool isPrimary() const override;

    void fakeKey(uint32_t keycode, bool is_down) const;

protected:
    // IPlatformScreen overrides
    void handleSystemEvent(const Event&, void*) override;
    void handleConnectedToEISEvent(const Event& sysevent, void* data);
    void updateButtons() override;
    IKeyState*  getKeyState() const override;

    void updateShape();
    void addDevice(struct ei_device* device);
    void removeDevice(struct ei_device* device);

private:
    // true if screen is being used as a primary screen, false otherwise
    bool m_isPrimary;
    IEventQueue* m_events;

    // keyboard stuff
    EiKeyState* m_keyState;

    std::vector<struct ei_device *> m_ei_devices;

    struct ei *m_ei;
    struct ei_seat *m_ei_seat;
    struct ei_device *m_ei_pointer;
    struct ei_device *m_ei_keyboard;
    struct ei_device *m_ei_abs;

    uint32_t m_x, m_y, m_w, m_h;

    mutable std::mutex mutex_;

    PortalRemoteDesktop *m_PortalRemoteDesktop;
};
