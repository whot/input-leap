/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2004 Chris Schoeneman
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file COPYING that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef CEVENT_H
#define CEVENT_H

#include "BasicTypes.h"

//! Event
/*!
A \c CEvent holds an event type and a pointer to event data.
*/
class CEvent {
public:
	typedef UInt32 Type;
	enum {
		kUnknown,	//!< The event type is unknown
		kQuit,		//!< The quit event
		kSystem,	//!< The data points to a system event type
		kTimer,		//!< The data points to timer info
		kLast		//!< Must be last
	};

	CEvent();

	//! Create \c CEvent with data
	/*!
	The \p type must have been registered using \c registerType().
	The \p data must be POD (plain old data) which means it cannot
	have a destructor or be composed of any types that do.  \p target
	is the intended recipient of the event.
	*/
	CEvent(Type type, void* target = NULL, void* data = NULL);

	//! @name manipulators
	//@{

	//@}
	//! @name accessors
	//@{

	//! Get event type
	/*!
	Returns the event type.
	*/
	Type				getType() const;

	//! Get the event target
	/*!
	Returns the event target.
	*/
	void*				getTarget() const;

	//! Get the event data
	/*!
	Returns the event data.
	*/
	void*				getData() const;

	//! Creates a new event type
	/*!
	Returns a unique event type id.
	*/
	static Type			registerType();

	//! Creates a new event type
	/*!
	If \p type contains \c kUnknown then it is set to a unique event
	type id otherwise it is left alone.  The final value of \p type
	is returned.
	*/
	static Type			registerTypeOnce(Type& type);

	//! Release event data
	/*!
	Deletes event data for the given event.
	*/
	static void			deleteData(const CEvent&);

	//@}

private:
	Type				m_type;
	void*				m_target;
	void*				m_data;
	static Type			s_nextType;
};

#endif
