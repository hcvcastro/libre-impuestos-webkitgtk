/*
 * Copyright (C) 2007 Holger Hans Peter Freyther
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 *
 * Written by Henry Castro <hcvcastro@gmail.com>
 */

#ifndef libre_impuesto_webkitwebframe_h
#define libre_impuesto_webkitwebframe_h

#include <glib-object.h>
#include <gtk/gtk.h>

/* #include <JavaScriptCore/API/JSBase.h> */

#include <webkit/webkitdefines.h>
#include <webkit/webkitdomdefines.h>
#include <webkit/webkitnetworkrequest.h>
#include <webkit/webkitwebdatasource.h>

G_BEGIN_DECLS

WEBKIT_API GtkPrintOperationResult
webkit_web_frame_libre_impuesto_print_full      (WebKitWebFrame *header,
						WebKitWebFrame *content,
						WebKitWebFrame *footer,
				    		GtkPrintOperation    *operation,
		                                GtkPrintOperationAction action,
                                    		GError              **error);

G_END_DECLS

#endif


