/*
 * InputLeap -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2003 Chris Schoeneman
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

#include "platform/EiScreen.h"
#include "inputleap/KeyState.h"
#include "common/stdmap.h"
#include "common/stdvector.h"

class IEventQueue;

//! Ei key state
/*!
A key state for Ei
*/
class EiKeyState : public KeyState {
public:
    EiKeyState(EiScreen *impl, IEventQueue* events);
    ~EiKeyState();

    void init(int fd, size_t len);
    void initDefaultKeymap();

    // IKeyState overrides
    bool fakeCtrlAltDel() override;
    KeyModifierMask pollActiveModifiers() const override;
    int32_t pollActiveGroup() const override;
    void pollPressedKeys(KeyButtonSet& pressedKeys) const override;

protected:
    // KeyState overrides
    void getKeyMap(inputleap::KeyMap& keyMap) override;
    void fakeKey(const Keystroke& keystroke) override;

private:
    uint32_t convertModMask(uint32_t xkb_mask);
    void assignGeneratedModifiers(uint32_t keycode, inputleap::KeyMap::KeyItem &item);

    EiScreen *m_impl;

    struct xkb_context *m_xkb;
    struct xkb_keymap  *m_xkb_keymap;
};
