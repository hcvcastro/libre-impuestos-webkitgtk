/*
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
 * Written by Henry Castro <hcvcastro@gmail.com>, Marzo 2014
 */


#include "config.h"
#include "libre-impuestos-webkitwebframe.h"

#include "AXObjectCache.h"
#include "AnimationController.h"
#include "DOMObjectCache.h"
#include "DocumentFragment.h"
#include "DocumentLoader.h"
#include "DocumentLoaderGtk.h"
#include "FrameLoader.h"
#include "FrameLoaderClientGtk.h"
#include "FrameSelection.h"
#include "FrameTree.h"
#include "FrameView.h"
#include "GCController.h"
#include "GraphicsContext.h"
#include "GtkUtilities.h"
#include "GtkVersioning.h"
#include "HTMLFrameOwnerElement.h"
#include "JSDOMBinding.h"
#include "JSDOMWindow.h"
#include "JSElement.h"
#include "PlatformContextCairo.h"
#include "LibreImpuestoPrintContext.h"
#include "RenderListItem.h"
#include "RenderTreeAsText.h"
#include "RenderView.h"
#include "ReplaceSelectionCommand.h"
#include "ScriptController.h"
#include "SubstituteData.h"
#include "TextIterator.h"
#include "WebKitAccessibleWrapperAtk.h"
#include "markup.h"
#include "webkit/WebKitDOMRangePrivate.h"
#include "webkitenumtypes.h"
#include "webkitglobalsprivate.h"
#include "webkitmarshal.h"
#include "webkitnetworkresponse.h"
#include "webkitnetworkrequestprivate.h"
#include "webkitnetworkresponseprivate.h"
#include "webkitsecurityoriginprivate.h"
#include "webkitwebframeprivate.h"
#include "webkitwebresource.h"
#include "webkitwebview.h"
#include "webkitwebviewprivate.h"
#include <JavaScriptCore/APICast.h>
#include <atk/atk.h>
#include <glib/gi18n-lib.h>
#include <wtf/text/CString.h>

using namespace WebKit;
using namespace WebCore;


static void libre_impuesto_begin_print_callback(GtkPrintOperation* op, GtkPrintContext* context, gpointer user_data)
{
    LibreImpuestoPrintContext* printContext = reinterpret_cast<LibreImpuestoPrintContext*>(user_data);

    float width = gtk_print_context_get_width(context);
    float height = gtk_print_context_get_height(context);
    FloatRect printRect = FloatRect(0, 0, width, height);

    // first begin height 0
    printContext->begin(width, 0);

    float headerHeight = 0;
    float footerHeight = 0;
    float pageHeight = height;   // height of the page adjusted by margins

    printContext->computeHeaderFooterHeight( pageHeight, headerHeight, footerHeight);

    printContext->beginPage(width, height, headerHeight, footerHeight);

    printContext->fillRows(width, height, headerHeight, footerHeight);

    printContext->computePageRects(printRect, headerHeight, footerHeight, 1.0, pageHeight);

    gtk_print_operation_set_n_pages(op, printContext->pageCount());
}

static void libre_impuesto_draw_page_callback(GtkPrintOperation*, GtkPrintContext* gtkPrintContext, gint pageNumber, LibreImpuestoPrintContext* corePrintContext)
{
    if (pageNumber >= static_cast<gint>(corePrintContext->pageCount()))
        return;

    cairo_t* cr = gtk_print_context_get_cairo_context(gtkPrintContext);
    float pageWidth = gtk_print_context_get_width(gtkPrintContext);

    PlatformContextCairo platformContext(cr);
    GraphicsContext graphicsContext(&platformContext);
    corePrintContext->spoolPage(graphicsContext, pageNumber, pageWidth);
}

static void libre_impuesto_end_print_callback(GtkPrintOperation* op, GtkPrintContext* context, gpointer user_data)
{
    LibreImpuestoPrintContext* printContext = reinterpret_cast<LibreImpuestoPrintContext*>(user_data);
    printContext->end();
}


/**
 * webkit_web_frame_libre_impuesto_print_full:
 * @header: a #WebKitWebFrame header to be printed
 * @content: a #WebKitWebFrame to be printed
 * @footer: a #WebKitWebFrame footer to be printed
 * @operation: the #GtkPrintOperation to be carried
 * @action: the #GtkPrintOperationAction to be performed
 * @error: #GError for error return
 *
 * Prints the given #WebKitWebFrame, using the given #GtkPrintOperation
 * and #GtkPrintOperationAction. This function wraps a call to
 * gtk_print_operation_run() for printing the contents of the
 * #WebKitWebFrame.
 *
 * Returns: The #GtkPrintOperationResult specifying the result of this operation.
 *
 * Since: 1.1.5
 */
GtkPrintOperationResult 
webkit_web_frame_libre_impuesto_print_full(WebKitWebFrame* header, 
					   WebKitWebFrame* content,
					   WebKitWebFrame* footer,
					   GtkPrintOperation* operation, 
					   GtkPrintOperationAction action, 
					   GError** error)
{
    g_return_val_if_fail(WEBKIT_IS_WEB_FRAME(header), GTK_PRINT_OPERATION_RESULT_ERROR);
    g_return_val_if_fail(WEBKIT_IS_WEB_FRAME(content), GTK_PRINT_OPERATION_RESULT_ERROR);
    g_return_val_if_fail(WEBKIT_IS_WEB_FRAME(footer), GTK_PRINT_OPERATION_RESULT_ERROR);

    g_return_val_if_fail(GTK_IS_PRINT_OPERATION(operation), GTK_PRINT_OPERATION_RESULT_ERROR);

    GtkWidget* topLevel = gtk_widget_get_toplevel(GTK_WIDGET(webkit_web_frame_get_web_view(content)));
    if (!widgetIsOnscreenToplevelWindow(topLevel))
        topLevel = 0;

    Frame* coreHeader = core(header);
    if (!coreHeader)
        return GTK_PRINT_OPERATION_RESULT_ERROR;

    Frame* coreContent = core(content);
    if (!coreContent)
        return GTK_PRINT_OPERATION_RESULT_ERROR;

    Frame* coreFooter = core(footer);
    if (!coreFooter)
        return GTK_PRINT_OPERATION_RESULT_ERROR;

    LibreImpuestoPrintContext printContext(coreHeader, coreContent, coreFooter);

    g_signal_connect(operation, "begin-print", G_CALLBACK(libre_impuesto_begin_print_callback), &printContext);
    g_signal_connect(operation, "draw-page", G_CALLBACK(libre_impuesto_draw_page_callback), &printContext);
    g_signal_connect(operation, "end-print", G_CALLBACK(libre_impuesto_end_print_callback), &printContext);

    return gtk_print_operation_run(operation, action, GTK_WINDOW(topLevel), error);
}
