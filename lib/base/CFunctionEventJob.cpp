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

#include "CFunctionEventJob.h"

//
// CFunctionJob
//

CFunctionEventJob::CFunctionEventJob(
				void (*func)(const CEvent&, void*), void* arg) :
	m_func(func),
	m_arg(arg)
{
	// do nothing
}

CFunctionEventJob::~CFunctionEventJob()
{
	// do nothing
}

void
CFunctionEventJob::run(const CEvent& event)
{
	if (m_func != NULL) {
		m_func(event, m_arg);
	}
}
